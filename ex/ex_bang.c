/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1994, 1995
 *	Keith Bostic.  All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "$Id: ex_bang.c,v 10.1 1995/04/13 17:22:03 bostic Exp $ (Berkeley) $Date: 1995/04/13 17:22:03 $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "compat.h"
#include <db.h>
#include <regex.h>

#include "common.h"

/*
 * ex_bang -- :[line [,line]] ! command
 *
 * Pass the rest of the line after the ! character to the program named by
 * the O_SHELL option.
 *
 * Historical vi did NOT do shell expansion on the arguments before passing
 * them, only file name expansion.  This means that the O_SHELL program got
 * "$t" as an argument if that is what the user entered.  Also, there's a
 * special expansion done for the bang command.  Any exclamation points in
 * the user's argument are replaced by the last, expanded ! command.
 *
 * There's some fairly amazing slop in this routine to make the different
 * ways of getting here display the right things.  It took a long time to
 * get it right (wrong?), so be careful.
 */
int
ex_bang(sp, cmdp)
	SCR *sp;
	EXCMD *cmdp;
{
	enum filtertype ftype;
	ARGS *ap;
	EX_PRIVATE *exp;
	MARK rm;
	recno_t lno;
	size_t blen;
	int notused, rval;
	char *bp, *msg;

	NEEDFILE(sp, cmdp->cmd);

	ap = cmdp->argv[0];
	if (ap->len == 0) {
		ex_message(sp, cmdp->cmd->usage, EXM_USAGE);
		return (1);
	}

	/* Set the last bang command. */
	exp = EXP(sp);
	if (exp->lastbcomm != NULL)
		free(exp->lastbcomm);
	if ((exp->lastbcomm = strdup(ap->bp)) == NULL) {
		msgq(sp, M_SYSERR, NULL);
		return (1);
	}

	/*
	 * If the command was modified by the expansion, we redisplay it.
	 * Redisplaying it in vi mode is tricky, and handled separately
	 * in each case below.  If we're in ex mode, it's easy, so we just
	 * do it here.
	 */
	bp = NULL;
	if (F_ISSET(cmdp, E_MODIFY) && !F_ISSET(sp, S_EX_SILENT)) {
		if (F_ISSET(sp, S_EX)) {
			F_SET(sp, S_EX_WROTE);
			(void)ex_printf(EXCOOKIE, "!%s\n", ap->bp);
			(void)ex_fflush(EXCOOKIE);
		}
		/*
		 * Vi: Display the command if modified.  Historic vi displayed
		 * the command if it was modified due to file name and/or bang
		 * expansion.  If piping lines, it was immediately overwritten
		 * by any error or line change reporting.  We don't the user to
		 * have to page through the responses, so we only post it until
		 * it's erased by something else.  Otherwise, pass it on to the
		 * ex_exec_proc routine to display after the screen has been
		 * cleaned up.
		 */
		if (F_ISSET(sp, S_VI)) {
			GET_SPACE_RET(sp, bp, blen, ap->len + 3);
			bp[0] = '!';
			memmove(bp + 1, ap->bp, ap->len);
			bp[ap->len + 1] = '\n';
			bp[ap->len + 2] = '\0';
		}
	}

	/*
	 * If addresses were specified, pipe lines from the file through the
	 * command.
	 *
	 * Historically, vi lines were replaced by both the stdout and stderr
	 * lines of the command, but ex by only the stdout lines.  This makes
	 * no sense to me, so nvi makes it consistent for both, and matches
	 * vi's historic behavior.
	 */
	if (cmdp->addrcnt != 0) {
		/* Autoprint is set historically, even if the command fails. */
		F_SET(cmdp, E_AUTOPRINT);

		/*
		 * Vi gets a busy message.
		 *
		 * !!!
		 * Vi doesn't want the <newline> at the end of the message.
		 */
		if (bp != NULL) {
			bp[ap->len + 1] = '\0';
			(void)sp->gp->scr_busy(sp, bp, 1);
		}

		/*
		 * !!!
		 * Historical vi permitted "!!" in an empty file.  When it
		 * happens, we get called with two addresses of 1,1 and a
		 * bad attitude.  The simple solution is to turn it into a
		 * FILTER_READ operation, but that means that we don't put
		 * an empty line into the default cut buffer as did historic
		 * vi.  Tough.
		 */
		ftype = FILTER;
		if (cmdp->addr1.lno == 1 && cmdp->addr2.lno == 1) {
			if (file_lline(sp, &lno))
				return (1);
			if (lno == 0) {
				cmdp->addr1.lno = cmdp->addr2.lno = 0;
				ftype = FILTER_READ;
			}
		}
		rval = filtercmd(sp,
		    &cmdp->addr1, &cmdp->addr2, &rm, ap->bp, ftype);

		/*
		 * If in vi mode, move to the first nonblank.
		 *
		 * !!!
		 * Historic vi wasn't consistent in this area -- if you used
		 * a forward motion it moved to the first nonblank, but if you
		 * did a backward motion it didn't.  And, if you followed a
		 * backward motion with a forward motion, it wouldn't move to
		 * the nonblank for either.  Going to the nonblank generally
		 * seems more useful, so we do it.
		 */
		sp->lno = rm.lno;
		sp->cno = rm.cno;
		if (rval == 0 && F_ISSET(sp, S_VI)) {
			sp->cno = 0;
			(void)nonblank(sp, sp->lno, &sp->cno);
		}
		goto ret2;
	}

	/*
	 * If no addresses were specified, run the command.  If the file
	 * has been modified and autowrite is set, write the file back.
	 * If the file has been modified, autowrite is not set and the
	 * warn option is set, tell the user about the file.
	 */
	msg = NULL;
	if (F_ISSET(sp->ep, F_MODIFIED))
		if (O_ISSET(sp, O_AUTOWRITE)) {
			if (file_aw(sp, FS_ALL)) {
				rval = 1;
				goto ret1;
			}
		} else if (O_ISSET(sp, O_WARN) && !F_ISSET(sp, S_EX_SILENT))
			msg = "File modified since last write.\n";

	/* Run the command. */
	rval = ex_exec_proc(sp, ap->bp, bp, msg);

	/* Vi requires user permission to continue. */
	if (F_ISSET(sp, S_VI))
		F_SET(sp, S_CONTINUE);

ret2:	if (F_ISSET(sp, S_EX)) {
		/*
		 * Put ex error messages out so they aren't confused with
		 * the autoprint output.
		 */
		if (rval)
			(void)sp->gp->scr_msgflush(sp, &notused);

		/* Ex terminates with a bang, even if the command fails. */
		if (!F_ISSET(sp, S_EX_SILENT))
			(void)write(STDOUT_FILENO, "!\n", 2);
	}

	/* Free the extra space. */
ret1:	if (bp != NULL)
		FREE_SPACE(sp, bp, blen);

	/*
	 * XXX
	 * The ! commands never return an error, so that autoprint always
	 * happens in the ex parser.
	 */
	return (0);
}
