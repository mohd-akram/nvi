/*-
 * Copyright (c) 1992 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "$Id: ex.c,v 5.78 1993/03/25 14:59:35 bostic Exp $ (Berkeley) $Date: 1993/03/25 14:59:35 $";
#endif /* not lint */

#include <sys/param.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vi.h"
#include "excmd.h"
#include "log.h"
#include "options.h"
#include "screen.h"
#include "term.h"
#include "vcmd.h"
#include "pathnames.h"

u_char *defcmdarg[2];

static int	 fileexpand __P((SCR *, EXF *, glob_t *, u_char *, int));
static u_char	*linespec __P((SCR *, EXF *, u_char *, EXCMDARG *));

#define	DEFCOM	".+1"

/*
 * ex --
 *	Read an ex command and execute it.
 */
int
ex(sp, ep)
	SCR *sp;
	EXF *ep;
{
	size_t len;
	u_char *p, defcom[sizeof(DEFCOM)];

	if (ex_init(sp, ep))
		return (1);
	status(sp, ep, ep->lno);
	do {
		if (ex_gb(sp, 1, &p, &len, GB_MAPCOMMAND))
			continue;
		if (*p) {
			(void)fputc('\n', sp->stdfp);
			(void)ex_cstring(sp, ep, p, len, 0);
		} else {
			(void)fputc('\r', sp->stdfp);
			(void)fflush(sp->stdfp);
			memmove(defcom, DEFCOM, sizeof(DEFCOM));
			(void)ex_cstring(sp, ep,
			    defcom, sizeof(DEFCOM) - 1, 0);
		}
	} while (F_ISSET(sp, S_MODE_EX) && !F_ISSET(sp, S_FILE_CHANGED));
	return (ex_end(sp));
}

/*
 * ex_cfile --
 *	Execute ex commands from a file.
 */
int
ex_cfile(sp, ep, filename, noexisterr)
	SCR *sp;
	EXF *ep;
	char *filename;
	int noexisterr;
{
	struct stat sb;
	int fd, len, rval;
	u_char *bp;

	if ((fd = open(filename, O_RDONLY, 0)) < 0) {
		if (noexisterr)
			goto e1;
		return (0);
	}
	if (fstat(fd, &sb))
		goto e2;

	if (sb.st_size > SIZE_T_MAX) {
		errno = E2BIG;
		goto e2;
	}

	if ((bp = malloc(sb.st_size)) == NULL)
		goto e2;

	len = read(fd, bp, (int)sb.st_size);
	if (len == -1 || len != sb.st_size) {
		if (len != sb.st_size)
			errno = EIO;
		goto e3;
	}
	bp[sb.st_size] = '\0';

	rval = ex_cstring(sp, ep, bp, len, 1);
	/*
	 * XXX
	 * THE UNDERLYING EXF MAY HAVE CHANGED (but we don't care).
	 */
	free(bp);
	(void)close(fd);
	return (rval);

e3:	free(bp);
e2:	(void)close(fd);
e1:	msgq(sp, M_ERROR, "%s: %s.", filename, strerror(errno));
	return (1);
}

/*
 * ex_cstring --
 *	Execute EX commands from a string.  The commands may be separated
 *	by newlines or by | characters, and may be quoted.
 */
int
ex_cstring(sp, ep, cmd, len, doquoting)
	SCR *sp;
	EXF *ep;
	u_char *cmd;
	int len, doquoting;
{
	u_int saved_mode;
	int cnt, rval;
	u_char *p, *t;

	if (doquoting)
		QINIT;
	/*
	 * Walk the string, checking for '\' or '^V' quotes and '|' or
	 * '\n' separated commands.  The string "^V\n" is a single '^V'.
	 */
	rval = 0;
	saved_mode = F_ISSET(sp, S_MODE_EX | S_MODE_VI | S_FILE_CHANGED);
	for (p = t = cmd, cnt = 0;; ++cnt, ++t, --len) {
		if (len == 0)
			goto cend;
		if (doquoting && cnt == sp->gb_len)
			gb_inc(sp);
		switch(*t) {
		case '|':
		case '\n':
cend:			if (p > cmd) {
				*p = '\0';	/* XXX: 8BIT */
				/*
				 * Errors are ignored, although error
				 * messages will be displayed later.
				 */
				if (ex_cmd(sp, ep, cmd))
					rval = 1;
				p = cmd;
			}
			if (len == 0)
				return (rval);
			if (saved_mode != F_ISSET(sp,
			    S_MODE_EX | S_MODE_VI | S_FILE_CHANGED)) {
				msgq(sp, M_ERROR,
		    "File or status changed, remaining input discarded.");
				return (1);
			}
			if (doquoting)
				QINIT;
			break;
		case '\\':
		case ctrl('V'):
			if (doquoting && len > 1 && t[1] != '\n') {
				QSET(cnt);
				++t;
				--len;
			}
			/* FALLTHROUGH */
		default:
			*p++ = *t;
			break;
		}
	}
	/* NOTREACHED */
}

static EXCMDARG parg = { NULL, 2 };
static EXCMDLIST *lastcmd = &cmds[C_PRINT];

/*
 * ex_cmd --
 *	Parse and execute an ex command.  
 */
int
ex_cmd(sp, ep, exc)
	SCR *sp;
	EXF *ep;
	u_char *exc;
{
	EXCMDARG cmd;
	EXCMDLIST *cp;
	recno_t lcount, num;
	long flagoff;
	u_int saved_mode;
	int cmdlen, flags, uselastcmd;
	u_char *p, *endp;

#if DEBUG && 1
	TRACE(sp, "ex: {%s}\n", exc);
#endif
	/*
	 * Permit a single extra colon at the start of the line, for
	 * historical reasons.
	 */
	if (*exc == ':')
		++exc;

	/* Ignore command lines that start with a double-quote. */
	if (*exc == '"')
		return (0);

	/* Skip whitespace. */
	for (; isspace(*exc); ++exc);

	/* Initialize the argument structure. */
	memset(&cmd, 0, sizeof(EXCMDARG));
	cmd.buffer = OOBCB;

	/*
	 * Parse line specifiers if the command uses addresses.  New command
	 * line position is returned, or NULL on error.  
	 */
	if ((exc = linespec(sp, ep, exc, &cmd)) == NULL)
		return (1);

	/* Skip whitespace. */
	for (; isspace(*exc); ++exc);

	/*
	 * If no command, ex does the last specified of p, l, or #, and vi
	 * moves to the line.  Otherwise, find out how long the command name
	 * is.  There are a few commands that aren't alphabetic, but they
	 * are all single character commands.
	 */
	if (*exc) {
		if (strchr("!#&<=>@", *exc)) {
			p = exc;
			exc++;
			cmdlen = 1;
		} else {
			for (p = exc; isalpha(*exc); ++exc);
			cmdlen = exc - p;
		}
		for (cp = cmds; cp->name && memcmp(p, cp->name, cmdlen); ++cp);
		if (cp->name == NULL) {
			msgq(sp, M_ERROR,
			    "The %.*s command is unknown.", cmdlen, p);
			return (1);
		}
		uselastcmd = 0;

		/* Some commands are turned off. */
		if (cp->flags & E_NOPERM) {
			msgq(sp, M_ERROR,
			    "The %.*s command is not currently supported.",
			    cmdlen, p);
			return (1);
		}

		/* Some commands aren't okay in globals. */
		if (F_ISSET(sp, S_IN_GLOBAL) && cp->flags & E_NOGLOBAL) {
			msgq(sp, M_ERROR,
"The %.*s command can't be used as part of a global command.", cmdlen, p);
			return (1);
		}
	} else {
		cp = lastcmd;
		uselastcmd = 1;
	}
	flags = cp->flags;

	/*
	 * File state must be checked throughout this code, because it is
	 * called when reading the .exrc file and similar things.  There's
	 * this little chicken and egg problem -- if we read the file first,
	 * we won't know how to display it.  If we read/set the exrc stuff
	 * first, we can't allow any command that requires file state.
	 * Historic vi generally took the easy way out and dropped core.
 	 */
	if (flags & E_NORC && ep == NULL) {
		msgq(sp, M_ERROR,
	"The %s command requires a file to already have been read in.",
		    cp->name);
		return (1);
	}

	/*
	 * Set the default addresses.  It's an error to specify an address for
	 * a command that doesn't take them.  If two addresses are specified
	 * for a command that only takes one, lose the first one.  Two special
	 * cases here, some commands take 0 or 2 addresses.  For most of them
	 * (the E_ADDR2_ALL flag), 0 defaults to the entire file.  For one
	 * (the `!' command, the E_ADDR2_NONE flag), 0 defaults to no lines.
	 *
	 * Also, if the file is empty, some commands want to use an address of
	 * 0, i.e. the entire file is 0 to 0, and the default first address is
	 * 0.  Otherwise, an entire file is 1 to N and the default line is 1.
	 * Note, we also add the E_ZERO flag to the command flags, for the case
	 * where the 0 address is only valid if it's a default address.
	 */
	flagoff = 0;
	switch(flags & (E_ADDR1|E_ADDR2|E_ADDR2_ALL|E_ADDR2_NONE)) {
	case E_ADDR1:				/* One address: */
		switch(cmd.addrcnt) {
		case 0:				/* Default cursor/empty file. */
			cmd.addrcnt = 1;
			if (flags & E_ZERODEF && file_lline(sp, ep) == 0) {
				cmd.addr1.lno = 0;
				flags |= E_ZERO;
			} else
				cmd.addr1.lno = ep->lno;
			cmd.addr1.cno = ep->cno;
			break;
		case 1:
			break;
		case 2:				/* Lose the first address. */
			cmd.addrcnt = 1;
			cmd.addr1 = cmd.addr2;
		}
		break;
	case E_ADDR2_NONE:			/* Zero/two addresses: */
		if (cmd.addrcnt == 0)		/* Default to nothing. */
			break;
		goto two;
	case E_ADDR2_ALL:			/* Zero/two addresses: */
		if (cmd.addrcnt == 0) {		/* Default entire/empty file. */
			cmd.addrcnt = 2;
			cmd.addr2.lno = file_lline(sp, ep);
			if (flags & E_ZERODEF && cmd.addr2.lno == 0) {
				cmd.addr1.lno = 0;
				flags |= E_ZERO;
			} else
				cmd.addr1.lno = 1;
			cmd.addr1.cno = cmd.addr2.cno = 0;
			cmd.flags |= E_ADDR2_ALL;
			break;
		}
		/* FALLTHROUGH */
	case E_ADDR2:				/* Two addresses: */
two:		switch(cmd.addrcnt) {
		case 0:				/* Default cursor/empty file. */
			cmd.addrcnt = 2;
			if (flags & E_ZERODEF && ep->lno == 1 &&
			    file_lline(sp, ep) == 0) {
				cmd.addr1.lno = cmd.addr2.lno = 0;
				flags |= E_ZERO;
			} else
				cmd.addr1.lno = cmd.addr2.lno = ep->lno;
			cmd.addr1.cno = cmd.addr2.cno = ep->cno;
			break;
		case 1:				/* Default to first address. */
			cmd.addrcnt = 2;
			cmd.addr2 = cmd.addr1;
			break;
		case 2:
			break;
		}
		break;
	default:
		if (cmd.addrcnt)		/* Error. */
			goto usage;
	}
		
	/*
	 * If the rest of the string is parsed by the command itself, we
	 * don't even skip leading white-space, it's significant for some
	 * commands.  However, require that there be *something*.
	 */
	p = (u_char *)cp->syntax;
	if (*p == 's') {
		for (p = exc; *p && isspace(*p); ++p);
		cmd.string = *p ? exc : NULL;
		goto addr2;
	}
	for (lcount = 0; *p; ++p) {
		for (; isspace(*exc); ++exc);		/* Skip whitespace. */
		if (!*exc)
			break;

		switch (*p) {
		case '!':				/* ! */
			if (*exc == '!') {
				++exc;
				cmd.flags |= E_FORCE;
			}
			break;
		case '+':				/* +cmd */
			if (*exc != '+')
				break;
			for (cmd.plus = ++exc; !isspace(*exc); ++exc);
			if (*exc)
				*exc++ = '\0';
			break;
		case '1':				/* #, l, p */
			for (; *exc == '+' || *exc == '-'; ++exc)
				switch (*exc) {
				case '+':
					++flagoff;
					break;
				case '-':
					--flagoff;
					break;
				}
			for (; *exc == '#' || *exc == 'l' || *exc == 'p'; ++exc)
				switch (*exc) {
				case '#':
					cmd.flags |= E_F_HASH;
					break;
				case 'l':
					cmd.flags |= E_F_LIST;
					break;
				case 'p':
					cmd.flags |= E_F_PRINT;
					break;
				}
			for (; *exc == '+' || *exc == '-'; ++exc)
				switch (*exc) {
				case '+':
					++flagoff;
					break;
				case '-':
					--flagoff;
					break;
				}
			break;
		case '2':				/* -, ., +, ^ */
			for (;; ++exc)
				switch (*exc) {
				case '-':
					cmd.flags |= E_F_DASH;
					break;
				case '.':
					cmd.flags |= E_F_DOT;
					break;
				case '+':
					cmd.flags |= E_F_PLUS;
					break;
				case '^':
					cmd.flags |= E_F_CARAT;
					break;
				default:
					goto end2;
				}
end2:			break;
#ifdef XXX_THIS_NO_LONGER_USED
		case '>':				/*  >> */
			if (exc[0] == '>' && exc[1] == '>') {
				cmd.flags |= E_APPEND;
				exc += 2;
			}
			break;
#endif
		case 'b':				/* buffer */
			cmd.buffer = *exc++;
			break;
		case 'c':				/* count */
			if (isdigit(*exc)) {
				lcount = USTRTOL(exc, &endp, 10);
				if (lcount == 0) {
					msgq(sp, M_ERROR,
					    "Count may not be zero.");
					return (1);
				}
				exc = endp;
				/*
				 * Fix up the addresses.  Count's only occur
				 * with commands taking two addresses.  Replace
				 * the first with the second and recompute the
				 * second.
				 */
				cmd.addr1 = cmd.addr2;
				cmd.addr2.lno = cmd.addr1.lno + lcount;
			}
			break;
		case 'l':				/* line */
			if (isdigit(*exc)) {
				cmd.lineno = USTRTOL(exc, &endp, 10);
				exc = endp;
			}
			break;
		case 'f':				/* file */
			if (buildargv(sp, ep, exc, 1, &cmd))
				return (1);
			goto countchk;
		case 's':				/* string */
			cmd.string = exc;
			goto addr2;
		case 'w':				/* word */
			if (buildargv(sp, ep, exc, 0, &cmd))
				return (1);
countchk:		if (*++p != 'N') {		/* N */
				/*
				 * If a number is specified, must either be
				 * 0 or that number, if optional, and that
				 * number, if required.
				 */
				num = *p - '0';
				if ((*++p != 'o' || cmd.argc != 0) &&
				    cmd.argc != num)
					goto usage;
			}
			goto addr2;
		default:
			msgq(sp, M_ERROR,
			    "Internal syntax table error (%s).", cp->name);
		}
	}

	/*
	 * Shouldn't be anything left, and no more required fields.
	 * That means neither 'l' or 'r' in the syntax.
	 */
	for (; *exc && isspace(*exc); ++exc);
	if (*exc || USTRPBRK(p, "lr")) {
usage:		msgq(sp, M_ERROR, "Usage: %s.", cp->usage);
		return (1);
	}

	/* Verify that the addresses are legal. */
addr2:	switch(cmd.addrcnt) {
	case 2:
		num = cmd.addr2.lno;
		if (num < 0) {
			msgq(sp, M_ERROR,
			    "%lu is an invalid address.", num);
			return (1);
		}
		lcount = file_lline(sp, ep);
		if (num > lcount) {
			if (lcount == 0)
				msgq(sp, M_ERROR, "The file is empty.");
			else
				msgq(sp, M_ERROR,
				    "Only %lu line%s in the file",
				    lcount, lcount > 1 ? "s" : "");
			return (1);
		}
		/* FALLTHROUGH */
	case 1:
		num = cmd.addr1.lno;
		/*
		 * If it's a "default vi command", zero is okay.  Historic
		 * vi allowed this, note, it's also the hack that allows
		 * "vi + nonexistent_file" to work.
		 */
		if (num == 0 &&
		    (!F_ISSET(sp, S_MODE_VI) || uselastcmd != 1) &&
		    !(flags & E_ZERO)) {
			msgq(sp, M_ERROR,
			    "The %s command doesn't permit an address of 0.",
			    cp->name);
			return (1);
		}
		if (num < 0) {
			msgq(sp, M_ERROR,
			    "%lu is an invalid address.", num);
			return (1);
		}
		lcount = file_lline(sp, ep);
		if (num > lcount) {
			msgq(sp, M_ERROR, "Only %lu line%s in the file",
			    lcount, lcount > 1 ? "s" : "");
			return (1);
		}
		break;
	}

	/* If doing a default command, vi just moves to the line. */
	if (F_ISSET(sp, S_MODE_VI) && uselastcmd) {
		ep->lno = cmd.addr1.lno ? cmd.addr1.lno : 1;
		ep->cno = cmd.addr1.cno;
		return (0);
	}

	/* Reset "last" command. */
	if (flags & E_SETLAST)
		lastcmd = cp;

	cmd.cmd = cp;
#if DEBUG && 0
{
	int __cnt;

	TRACE(sp, "ex_cmd: %s", cmd.cmd->name);
	if (cmd.addrcnt > 0) {
		TRACE(sp, "\taddr1 %d", cmd.addr1.lno);
		if (cmd.addrcnt > 1)
			TRACE(sp, " addr2: %d", cmd.addr2.lno);
		TRACE(sp, "\n");
	}
	if (cmd.lineno)
		TRACE(sp, "\tlineno %d", cmd.lineno);
	if (cmd.flags)
		TRACE(sp, "\tflags %0x", cmd.flags);
	if (cmd.command)
		TRACE(sp, "\tcommand %s", cmd.command);
	if (cmd.plus)
		TRACE(sp, "\tplus %s", cmd.plus);
	if (cmd.buffer != OOBCB)
		TRACE(sp, "\tbuffer %c", cmd.buffer);
	TRACE(sp, "\n");
	if (cmd.argc) {
		for (__cnt = 0; __cnt < cmd.argc; ++__cnt)
			TRACE(sp, "\targ %d: {%s}", __cnt, cmd.argv[__cnt]);
		TRACE(sp, "\n");
	}
}
#endif
	/* Clear autoprint. */
	F_CLR(sp, S_AUTOPRINT);

	/*
	 * If file state, set rptlines.  If file state and not doing a global
	 * command, log the start of an action.
	 */
	sp->rptlines = 0;
	if (ep != NULL && !F_ISSET(sp, S_IN_GLOBAL))
		(void)log_cursor(sp, ep);

	/* Save the current mode. */
	saved_mode = F_ISSET(sp, S_MODE_EX | S_MODE_VI | S_FILE_CHANGED);

	/* Do the command. */
	if ((cp->fn)(sp, ep, &cmd))
		return (1);

	/* If the world changed, we're done. */
	if (saved_mode != F_ISSET(sp, S_MODE_EX | S_MODE_VI | S_FILE_CHANGED))
		return (0);

	/* If just starting up, or not in ex mode, we're done. */
	if (ep == NULL || !F_ISSET(sp, S_MODE_EX))
		return (0);

	/*
	 * The print commands have already handled the `print' flags.
	 * If so, clear them.  Don't return, autoprint may still have
	 * stuff to print out.
	 */
	 if (flags & E_F_PRCLEAR)
		 cmd.flags &= ~(E_F_HASH | E_F_LIST | E_F_PRINT);

	/*
	 * If the command was successful, and there was an explicit flag to
	 * display the new cursor line, or we're in ex mode, autoprint is set,
	 * and a change was made, display the line.
	 */
	if (flagoff) {
		if (flagoff < 0) {
			if ((recno_t)flagoff > ep->lno) {
				msgq(sp, M_ERROR,
				    "Flag offset before line 1.");
				return (1);
			}
		} else if (ep->lno + flagoff > file_lline(sp, ep)) {
			msgq(sp, M_ERROR, "Flag offset past end-of-file.");
			return (1);
		}
		ep->lno += flagoff;
	}

	if (F_ISSET(sp, S_AUTOPRINT) && ISSET(O_AUTOPRINT))
		flags = E_F_PRINT;
	else
		flags = cmd.flags & (E_F_HASH | E_F_LIST | E_F_PRINT);
	parg.addr1.lno = parg.addr2.lno = ep->lno;
	parg.addr1.cno = parg.addr2.cno = ep->cno;
	if (flags) {
		switch (flags) {
		case E_F_HASH:
			parg.cmd = &cmds[C_HASH];
			ex_number(sp, ep, &parg);
			break;
		case E_F_LIST:
			parg.cmd = &cmds[C_LIST];
			ex_list(sp, ep, &parg);
			break;
		case E_F_PRINT:
			parg.cmd = &cmds[C_PRINT];
			ex_pr(sp, ep, &parg);
			break;
		}
	}
	return (0);
}

/*
 * linespec --
 *	Parse a line specifier for ex commands.
 */
static u_char *
linespec(sp, ep, cmd, cp)
	SCR *sp;
	EXF *ep;
	u_char *cmd;
	EXCMDARG *cp;
{
	MARK cur, savecursor, sm, *mp;
	long num, total;
	int savecursor_set;
	u_char *endp;

	/* Percent character is all lines in the file. */
	if (*cmd == '%') {
		cp->addr1.lno = 1;
		cp->addr2.lno = file_lline(sp, ep);
		/* If an empty file, then the first line is 0, not 1. */
		if (cp->addr2.lno == 0)
			cp->addr1.lno = 0;
		cp->addr1.cno = cp->addr2.cno = 0;
		cp->addrcnt = 2;
		return (++cmd);
	}

	/* Parse comma or semi-colon delimited line specs. */
	savecursor_set = 0;
	for (cp->addrcnt = 0;;) {
		switch(*cmd) {
		case ';':		/* Semi-colon delimiter. */
			/*
			 * Comma delimiters delimit; semi-colon delimiters
			 * change the current address for the 2nd address
			 * to be the first address.  Trailing or multiple
			 * delimiters are discarded.
			 */
			if (cp->addrcnt == 0)
				goto done;
			if (!savecursor_set) {
				savecursor.lno = ep->lno;
				savecursor.cno = ep->cno;
				ep->lno = cp->addr1.lno;
				ep->cno = cp->addr1.cno;
			}
			savecursor_set = 1;
			/* FALLTHROUGH */
		case ',':		/* Comma delimiter. */
			++cmd;
			break;
		case '$':		/* Last line. */
			cur.lno = file_lline(sp, ep);
			cur.cno = 0;
			++cmd;
			break;
					/* Absolute line number. */
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			cur.lno = USTRTOL(cmd, &endp, 10);
			cur.cno = 0;
			cmd = endp;
			break;
		case '\'':		/* Set mark. */
			if (cmd[1] == '\0') {
				msgq(sp, M_ERROR,
				    "No mark name; use 'a' to 'z'.");
				return (NULL);
			}
			if ((mp = mark_get(sp, ep, cmd[1])) == NULL)
				return (NULL);
			cur = *mp;
			cmd += 2;
			break;
		case '/':		/* Search forward. */
			sm.lno = ep->lno;
			sm.cno = ep->cno;
			if ((mp = f_search(sp, ep, &sm, cmd,
			    &endp, SEARCH_MSG | SEARCH_PARSE)) == NULL)
				return (NULL);
			cur.lno = ep->lno = mp->lno;
			cur.cno = ep->cno = mp->cno;
			cmd = endp;
			break;
		case '?':		/* Search backward. */
			sm.lno = ep->lno;
			sm.cno = ep->cno;
			if ((mp = b_search(sp, ep, &sm, cmd,
			    &endp, SEARCH_MSG | SEARCH_PARSE)) == NULL)
				return (NULL);
			cur.lno = ep->lno = mp->lno;
			cur.cno = ep->cno = mp->cno;
			cmd = endp;
			break;
		case '.':		/* Current position. */
			++cmd;
			/* FALLTHROUGH */
		case '+':		/* Increment. */
		case '-':		/* Decrement. */
			/* If an empty file, then '.' is 0, not 1. */
			if (ep->lno == 1) {
				if ((cur.lno = file_lline(sp, ep)) != 0)
					cur.lno = 1;
			} else
				cur.lno = ep->lno;
			cur.cno = ep->cno;
			break;
		default:
			goto done;
		}

		/*
		 * Evaluate any offset.  Offsets are +/- any number, or,
		 * any number of +/- signs, or any combination thereof.
		 */
		for (total = 0; *cmd == '-' || *cmd == '+'; total += num) {
			num = *cmd == '-' ? -1 : 1;
			if (isdigit(*++cmd)) {
				num *= USTRTOL(cmd, &endp, 10);
				cmd = endp;
			}
		}
		if (total < 0 && -total > cur.lno) {
			msgq(sp, M_ERROR,
			    "Reference to a line number less than 0.");
			return (NULL);
		}
		cur.lno += total;

		/* Extra addresses are discarded, starting with the first. */
		switch(cp->addrcnt) {
		case 0:
			cp->addr1 = cur;
			cp->addrcnt = 1;
			break;
		case 1:
			cp->addr2 = cur;
			cp->addrcnt = 2;
			break;
		case 2:
			cp->addr1 = cp->addr2;
			cp->addr2 = cur;
			break;
		}
	}

	/*
	 * XXX
	 * This is probably not right for treatment of savecursor -- figure
	 * out what the historical ex did for ";,;,;5p" or similar stupidity.
	 */
done:	if (savecursor_set) {
		ep->lno = savecursor.lno;
		ep->cno = savecursor.cno;
	}

	return (cmd);
}

typedef struct {
	int len;		/* Buffer length. */
	u_char *bp;		/* Buffer. */
#define	A_ALLOCATED	0x01	/* If allocated space. */
	u_char flags;
} ARGS;

/*
 * buildargv --
 *	Build an argv from the rest of the command line.
 */
int
buildargv(sp, ep, exc, expand, cp)
	SCR *sp;
	EXF *ep;
	u_char *exc;
	int expand;
	EXCMDARG *cp;
{
	static ARGS *args;
	static int argscnt;
	static u_char **argv;
	static glob_t g;
	int ch, cnt, done, globoff, len, needslots, off;
	u_char *ap;

	/* Discard any previous information. */
	if (g.gl_pathc) {
		globfree(&g);
		g.gl_pathc = 0;
		g.gl_offs = 0;
		g.gl_pathv = NULL;
	}

	/* Break into argv vector. */
	for (done = globoff = off = 0;; ) {
		/* New argument; NULL terminate. */
		for (ap = exc; *exc && !isspace(*exc); ++exc);
		if (*exc)
			*exc = '\0';
		else
			done = 1;

		/*
		 * Expand and count up the necessary slots.  Add +1 to
		 * include the trailing NULL.
		 */
		len = exc - ap +1;

		if (expand) {
			if (fileexpand(sp, ep, &g, ap, len))
				return (1);
			needslots = g.gl_pathc - globoff + 1;
		} else
			needslots = 2;

		/*
		 * Allocate more pointer space if necessary; leave a space
		 * for a trailing NULL.
		 */
#define	INCREMENT	20
		if (off + needslots >= argscnt - 1) {
			argscnt += cnt = MAX(INCREMENT, needslots);
			if ((args =
			    realloc(args, argscnt * sizeof(ARGS))) == NULL) {
				free(argv);
				goto mem1;
			}
			if ((argv =
			    realloc(argv, argscnt * sizeof(char *))) == NULL) {
				free(args);
mem1:				argscnt = 0;
				args = NULL;
				argv = NULL;
				msgq(sp, M_ERROR,
				    "Error: %s.", strerror(errno));
				return (1);
			}
			memset(&args[off], 0, cnt * sizeof(ARGS));
		}

		/*
		 * Copy the argument(s) into place, allocating space if
		 * necessary.
		 */
		if (expand) {
			for (cnt = globoff; cnt < g.gl_pathc; ++cnt, ++off) {
				if (args[off].flags & A_ALLOCATED) {
					free(args[off].bp);
					args[off].flags &= ~A_ALLOCATED;
				}
				argv[off] = args[off].bp =
				    (u_char *)g.gl_pathv[cnt];
			}
			globoff = g.gl_pathc;
		} else {
			if (args[off].len < len && (args[off].bp =
			    realloc(args[off].bp, len)) == NULL) {
				args[off].bp = NULL;
				args[off].len = 0;
				msgq(sp, M_ERROR,
				    "Error: %s.", strerror(errno));
				return (1);
			}
			argv[off] = args[off].bp;
			memmove(args[off].bp, ap, len);
			args[off].flags |= A_ALLOCATED;
			++off;
		}

		if (done)
			break;

		/* Skip whitespace. */
		while (ch = *++exc) {
			if (ch == '\\' && exc[1])
				++exc;
			if (!isspace(ch))
				break;
		}
		if (!*exc)
			break;
	}
	argv[off] = NULL;
	cp->argv = argv;
	cp->argc = off;
	return (0);
}

static int
fileexpand(sp, ep, gp, word, wordlen)
	SCR *sp;
	EXF *ep;
	glob_t *gp;
	u_char *word;
	int wordlen;
{
	static int tpathlen;
	static u_char *tpath;
	EXF *prev_ep;
	int len;
	u_char ch, *p;

	/*
	 * Check for escaped %, # characters.
	 * XXX
	 */
	/* Figure out how much space we need for this argument. */
	prev_ep = NULL;
	len = wordlen;
	for (p = word; p = USTRPBRK(p, "%#\\"); ++p)
		switch (*p) {
		case '%':
			if (F_ISSET(ep, F_NONAME)) {
				msgq(sp, M_ERROR,
				    "No filename to substitute for %%.");
				return (1);
			}
			len += ep->nlen;
			break;
		case '#':
			if (prev_ep == NULL)
				prev_ep = file_prev(sp, ep, 0);
			if (prev_ep == NULL || F_ISSET(prev_ep, F_NONAME)) {
				msgq(sp, M_ERROR,
				    "No filename to substitute for #.");
				return (1);
			}
			len += prev_ep->nlen;
			break;
		case '\\':
			if (p[1] != '\0')
				++p;
			break;
		}

	if (len != wordlen) {
		/*
		 * Copy argument into temporary space, replacing file
		 * names.  Allocate temporary space if necessary.
		 */
		if (tpathlen < len) {
			len = MAX(len, 64);
			if ((tpath = realloc(tpath, len)) == NULL) {
				tpathlen = 0;
				tpath = NULL;
				msgq(sp, M_ERROR,
				    "Error: %s.", strerror(errno));
				return (1);
			}
			tpathlen = len;
		}

		for (p = tpath; ch = *word; ++word)
			switch(ch) {
			case '%':
				memmove(p, ep->name, ep->nlen);
				p += ep->nlen;
				break;
			case '#':
				memmove(p, prev_ep->name, prev_ep->nlen);
				p += prev_ep->nlen;
				break;
			case '\\':
				if (p[1] != '\0')
					++p;
				/* FALLTHROUGH */
			default:
				*p++ = ch;
			}
		p = tpath;
	} else
		p = word;

	glob((char *)p,
	    GLOB_APPEND|GLOB_NOSORT|GLOB_NOCHECK|GLOB_QUOTE|GLOB_TILDE,
	    NULL, gp);
	return (0);
}
