/*-
 * Copyright (c) 1992 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "$Id: v_zexit.c,v 5.18 1993/05/17 16:28:47 bostic Exp $ (Berkeley) $Date: 1993/05/17 16:28:47 $";
#endif /* not lint */

#include <sys/types.h>

#include <string.h>

#include "vi.h"
#include "excmd.h"
#include "vcmd.h"

/*
 * v_exit -- ZZ
 *	Save the file and exit.
 */
int
v_exit(sp, ep, vp, fm, tm, rp)
	SCR *sp;
	EXF *ep;
	VICMDARG *vp;
	MARK *fm, *tm, *rp;
{
	if (F_ISSET(ep, F_MODIFIED) &&
	    file_write(sp, ep, NULL, NULL, NULL, FS_ALL))
		return (1);
	if (ep->refcnt <= 1 && file_next(sp, ep, 0)) {
		msgq(sp, M_ERR,
		    "More files to edit; use \":n\" to go to the next file");
		return (1);
	}

	F_SET(sp, S_EXIT);
	return (0);
}
