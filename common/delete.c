/*-
 * Copyright (c) 1992 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "$Id: delete.c,v 5.17 1993/02/16 20:16:14 bostic Exp $ (Berkeley) $Date: 1993/02/16 20:16:14 $";
#endif /* not lint */

#include <sys/types.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vi.h"

/*
 * delete --
 *	Delete a range of text.
 */
int
delete(ep, fm, tm, lmode)
	EXF *ep;
	MARK *fm, *tm;
	int lmode;
{
	recno_t lno;
	size_t len, tlen;
	u_char *bp, *p;
	int eof;

#if DEBUG && 0
	TRACE("delete: from %lu/%d to %lu/%d%s\n",
	    fm->lno, fm->cno, tm->lno, tm->cno, lmode ? " (LINE MODE)" : "");
#endif
	bp = NULL;

	/* Case 1 -- delete in line mode. */
	if (lmode) {
		for (lno = tm->lno; lno >= fm->lno; --lno)
			if (file_dline(ep, lno))
				return (1);
		goto done;
	}

	/*
	 * Case 2 -- delete to EOF.  This is a special case because it's
	 * easier to pick it off than try and find it in the other cases.
 	 */
	lno = file_lline(ep);
	if (tm->lno >= lno) {
		if (tm->lno == lno) {
			if ((p = file_gline(ep, lno, &len)) == NULL) {
				GETLINE_ERR(ep, lno);
				return (1);
			}
			eof = tm->cno > len ? 1 : 0;
		} else
			eof = 1;
		if (eof) {
			for (lno = tm->lno; lno > fm->lno; --lno)
				if (file_dline(ep, lno))
					return (1);
			if (fm->cno == 0) {
				if (file_dline(ep, fm->lno))
					return (1);
				goto done;
			}
			if ((p = file_gline(ep, fm->lno, &len)) == NULL) {
				GETLINE_ERR(ep, fm->lno);
				return (1);
			}
			if (file_sline(ep, fm->lno, p, fm->cno))
				return (1);
			goto done;
		}
	}

	/* Case 3 -- delete within a single line. */
	if (tm->lno == fm->lno) {
		if ((p = file_gline(ep, fm->lno, &len)) == NULL) {
			GETLINE_ERR(ep, fm->lno);
			return (1);
		}
		if ((bp = malloc(len)) == NULL) {
			msg(ep, M_ERROR,
			    "Error: %s", strerror(errno));
			return (1);
		}
		memmove(bp, p, fm->cno);
		memmove(bp + fm->cno, p + tm->cno, len - tm->cno);
		if (file_sline(ep, fm->lno, bp, len - (tm->cno - fm->cno)))
			goto err;
		goto done;
	}

	/* Case 4 -- delete over multiple lines. */

	/* Delete all the intermediate lines. */
	for (lno = tm->lno - 1; lno > fm->lno; --lno)
		if (file_dline(ep, lno))
			return (1);

	/* Figure out how big a buffer we need. */
	if ((p = file_gline(ep, fm->lno, &len)) == NULL) {
		GETLINE_ERR(ep, fm->lno);
		return (1);
	}
	tlen = len;
	if ((p = file_gline(ep, tm->lno, &len)) == NULL) {
		GETLINE_ERR(ep, tm->lno);
		return (1);
	}
	tlen += len;		/* XXX Possible overflow! */
	if ((bp = malloc(tlen)) == NULL) {
		msg(ep, M_ERROR, "Error: %s", strerror(errno));
		return (1);
	}

	/* Copy the start partial line into place. */
	if ((p = file_gline(ep, fm->lno, &len)) == NULL) {
		GETLINE_ERR(ep, fm->lno);
		goto err;
	}
	memmove(bp, p, fm->cno);
	tlen = fm->cno;

	/* Copy the end partial line into place. */
	if ((p = file_gline(ep, tm->lno, &len)) == NULL) {
		GETLINE_ERR(ep, tm->lno);
		goto err;
	}
	memmove(bp + tlen, p + tm->cno, len - tm->cno);
	tlen += len - tm->cno;

	/* Set the current line. */
	if (file_sline(ep, fm->lno, bp, tlen))
		goto err;

	/* Delete the new number of the old last line. */
	if (file_dline(ep, fm->lno + 1))
		goto err;

	/* Update the marks. */
done:	mark_delete(ep, fm, tm, lmode);

	if (bp != NULL)
		free(bp);

	/*
	 * Reporting.
 	 * XXX
	 * Wrong, if deleting characters.
	 */
	curf->rptlines = tm->lno - fm->lno + 1;
	curf->rptlabel = "deleted";

	return (0);

err:	if (bp != NULL)
		free(bp);
	return (1);
}
