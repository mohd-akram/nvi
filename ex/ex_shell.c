/*-
 * Copyright (c) 1992 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "$Id: ex_shell.c,v 5.1 1992/04/02 11:21:11 bostic Exp $ (Berkeley) $Date: 1992/04/02 11:21:11 $";
#endif /* not lint */

#include <sys/types.h>

#include "vi.h"
#include "excmd.h"
#include "options.h"
#include "extern.h"

/*
 * ex_shell (:sh[ell])
 *	Invoke the program named in the SHELL environment variable with
 *	the argument -i.
 */
void
ex_shell(cmdp)
	CMDARG *cmdp;
{
	char buf[MAXPATHLEN];

	suspend_curses();

	(void)snprintf(buf, sizeof(buf), "%s -i", PVAL(O_SHELL));
	system(buf);

	resume_curses(mode == MODE_EX);
}
