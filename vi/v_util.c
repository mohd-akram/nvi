/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "$Id: v_util.c,v 8.4 1993/10/28 11:22:26 bostic Exp $ (Berkeley) $Date: 1993/10/28 11:22:26 $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vi.h"
#include "vcmd.h"

/*
 * v_eof --
 *	Vi end-of-file error.
 */
void
v_eof(sp, ep, mp)
	SCR *sp;
	EXF *ep;
	MARK *mp;
{
	u_long lno;

	if (mp == NULL)
		msgq(sp, M_BERR, "Already at end-of-file.");
	else {
		if (file_lline(sp, ep, &lno))
			return;
		if (mp->lno >= lno)
			msgq(sp, M_BERR, "Already at end-of-file.");
		else
			msgq(sp, M_BERR,
			    "Movement past the end-of-file.");
	}
}

/*
 * v_eol --
 *	Vi end-of-line error.
 */
void
v_eol(sp, ep, mp)
	SCR *sp;
	EXF *ep;
	MARK *mp;	
{
	size_t len;

	if (mp == NULL)
		msgq(sp, M_BERR, "Already at end-of-line.");
	else {
		if (file_gline(sp, ep, mp->lno, &len) == NULL) {
			GETLINE_ERR(sp, mp->lno);
			return;
		}
		if (mp->cno == len - 1)
			msgq(sp, M_BERR, "Already at end-of-line.");
		else
			msgq(sp, M_BERR, "Movement past the end-of-line.");
	}
}

/*
 * v_sof --
 *	Vi start-of-file error.
 */
void
v_sof(sp, mp)
	SCR *sp;
	MARK *mp;
{
	if (mp == NULL || mp->lno == 1)
		msgq(sp, M_BERR, "Already at the beginning of the file.");
	else
		msgq(sp, M_BERR, "Movement past the beginning of the file.");
}

/*
 * v_isempty --
 *	Return if the line contains nothing but white-space characters.
 */
int 
v_isempty(p, len)
	char *p;
	size_t len;
{
	for (; len--; ++p)
		if (!isblank(*p))
			return (0);
	return (1);
}
