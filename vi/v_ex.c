/*-
 * Copyright (c) 1992 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "$Id: v_ex.c,v 5.27 1992/12/25 16:22:44 bostic Exp $ (Berkeley) $Date: 1992/12/25 16:22:44 $";
#endif /* not lint */

#include <sys/param.h>

#include <ctype.h>
#include <curses.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "vi.h"
#include "excmd.h"
#include "options.h"
#include "screen.h"
#include "term.h"
#include "vcmd.h"

static size_t exlinecount, extotalcount;
static enum { NOTSET, NEXTLINE, THISLINE } continueline;

static int	moveup __P((EXF *, int, int, int *));
static void	v_leaveex __P((EXF *));
static void	v_startex __P((void));

/*
 * v_ex --
 *	Execute strings of ex commands.
 */
int
v_ex(vp, fm, tm, rp)
	VICMDARG *vp;
	MARK *fm, *tm, *rp;
{
	size_t len;
	int flags, key;
	u_char *p;

	for (flags = GB_BS;; flags |= GB_NLECHO) {
		v_startex();			/* Reset. */

		/*
		 * Get an ex command; echo the newline on any prompts after
		 * the first.  We may have to overwrite the command later;
		 * get the length for later.
		 */
		if (v_gb(ISSET(O_PROMPT) ? ':' : 0, &p, &len, flags) ||
		    p == NULL)
			break;

		(void)ex_cstring(p, len, 0);
		(void)fflush(curf->stdfp);
		if (extotalcount <= 1) {
			FF_SET(curf, F_NEEDMERASE);
			break;
		}

		/* The user may continue in ex mode by entering a ':'. */
		(void)moveup(curf, 1, 1, &key);
		if (key != ':')
                        break;
	}

	/*
	 * The file may have changed, if so, the main vi loop will take care of
	 * it.  Otherwise, the only cursor modifications will be real, however,
	 * the underlying line may have changed; don't trust anything.  This
	 * section of code has been a remarkably fertile place for bugs.  The
	 * cursor is set to the first non-blank character by the main vi loop.
	 * Don't trust ANYTHING.
	 */
	if (!FF_ISSET(curf, F_NEWSESSION)) {
		v_leaveex(curf);
		curf->olno = OOBLNO;
		rp->lno = curf->lno;
		if (file_gline(curf, curf->lno, &len) == NULL &&
		    file_lline(curf) != 0) {
			GETLINE_ERR(curf->lno);
			return (1);
		}
	}
	return (0);
}
		
/*
 * v_startex --
 *	Vi calls ex.
 */
static void
v_startex()
{
	exlinecount = extotalcount = 0;
	continueline = NOTSET;
}

/*
 * v_leaveex --
 *	Ex returns to vi.
 */
static void
v_leaveex(ep)
	EXF *ep;
{
	if (extotalcount == 0)
		return;
	if (extotalcount >= SCREENSIZE(ep)) { 
		FF_SET(ep, F_REDRAW);
		return;
	}
	do {
		--extotalcount;
		(void)scr_update(ep,
		    BOTLINE(ep, ep->otop) - extotalcount, NULL, 0, LINE_RESET);
	} while (extotalcount);
}

/*
 * v_exwrite --
 *	Write out the ex messages.
 */
int
v_exwrite(cookie, line, llen)
	void *cookie;
	const char *line;
	int llen;
{
	static size_t lcont;
	EXF *ep;
	int len, rlen, tlen;
	char *p;

	for (ep = cookie, rlen = llen; llen;) {
		/* Newline delimits. */
		if ((p = memchr(line, '\n', llen)) == NULL) {
			len = llen;
			lcont = len;
			continueline = NEXTLINE;
		} else
			len = p - line;

		/* Fold if past end-of-screen. */
		if (len > ep->cols) {
			continueline = NOTSET;
			len = ep->cols;
		}
		llen -= len + (p == NULL ? 0 : 1);

		if (continueline != THISLINE && extotalcount != 0)
			(void)moveup(ep, 0, 0, NULL);

		switch (continueline) {
		case NEXTLINE:
			continueline = THISLINE;
			/* FALLTHROUGH */
		case NOTSET:
			MOVE(SCREENSIZE(ep), 0);
			++extotalcount;
			++exlinecount;
			tlen = len;
			break;
		case THISLINE:
			MOVE(SCREENSIZE(ep), lcont);
			tlen = len + lcont;
			continueline = NOTSET;
			break;
		}
		addbytes(line, len);

		if (tlen < ep->cols)
			clrtoeol();

		line += len + (p == NULL ? 0 : 1);
	}
	return (rlen);
}

static int
moveup(ep, mustwait, colon_ok, chp)
	EXF *ep;
	int mustwait, colon_ok, *chp;
{
	int ch;

	/*
	 * Scroll the screen.  Instead of scrolling the entire screen, delete
	 * the line above the first line output so preserve the maximum amount
	 * of the screen.
	 */
	if (extotalcount >= SCREENSIZE(ep)) {
		MOVE(0, 0);
	} else
		MOVE(SCREENSIZE(ep) - extotalcount, 0);
	deleteln();

	/* If just displayed a full screen, wait. */
	if (mustwait || exlinecount == SCREENSIZE(ep)) {
		MOVE(SCREENSIZE(ep), 0);
		addbytes(CONTMSG, sizeof(CONTMSG) - 1);
		clrtoeol();
		refresh();
		while (special[ch = getkey(0)] != K_CR &&
		    !isspace(ch) && (!colon_ok || ch != ':'))
			bell();
		if (chp != NULL)
			*chp = ch;
		exlinecount = 0;
	}
}
