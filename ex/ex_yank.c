/*-
 * Copyright (c) 1992 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "$Id: ex_yank.c,v 5.7 1993/03/25 15:00:16 bostic Exp $ (Berkeley) $Date: 1993/03/25 15:00:16 $";
#endif /* not lint */

#include <sys/types.h>

#include <limits.h>
#include <stdio.h>

#include "vi.h"
#include "excmd.h"

/*
 * ex_yank -- :[line [,line]] ya[nk] [buffer] [count]
 *	Yank the lines into a buffer.
 */
int
ex_yank(sp, ep, cmdp)
	SCR *sp;
	EXF *ep;
	EXCMDARG *cmdp;
{
	/* Yank the lines. */
	return (cut(sp, ep, cmdp->buffer != OOBCB ? cmdp->buffer : DEFCB,
	    &cmdp->addr1, &cmdp->addr2, 1));
}
