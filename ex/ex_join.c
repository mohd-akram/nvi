/*-
 * Copyright (c) 1992 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "$Id: ex_join.c,v 5.1 1992/04/02 11:21:00 bostic Exp $ (Berkeley) $Date: 1992/04/02 11:21:00 $";
#endif /* not lint */

#include <sys/types.h>

#include "vi.h"
#include "excmd.h"
#include "extern.h"

void
ex_join(cmdp)
	CMDARG *cmdp;
{
	long	l;
	char	*scan;
	int	len;	/* length of the new line */
	MARK	frommark;
	MARK	tomark;

	frommark = cmdp->addr1;
	tomark = cmdp->addr2;

	/* if only one line is specified, assume the following one joins too */
	if (markline(frommark) == nlines)
	{
		msg("Nothing to join with this line");
		return;
	}
	if (markline(frommark) == markline(tomark))
	{
		tomark += BLKSIZE;
	}

	/* get the first line */
	l = markline(frommark);
	strcpy(tmpblk.c, fetchline(l));
	len = strlen(tmpblk.c);

	/* build the longer line */
	while (++l <= markline(tomark))
	{
		/* get the next line */
		scan = fetchline(l);

		/* remove any leading whitespace */
		while (*scan == '\t' || *scan == ' ')
		{
			scan++;
		}

		/* see if the line will fit */
		if (strlen(scan) + len + 3 > BLKSIZE)
		{
			msg("Can't join -- the resulting line would be too long");
			return;
		}

		/* catenate it, with a space (or two) in between */
		if (len >= 1 &&
			(tmpblk.c[len - 1] == '.'
			 || tmpblk.c[len - 1] == '?'
			 || tmpblk.c[len - 1] == '!'))
		{
			 tmpblk.c[len++] = ' ';
		}
		tmpblk.c[len++] = ' ';
		strcpy(tmpblk.c + len, scan);
		len += strlen(scan);
	}
	tmpblk.c[len++] = '\n';
	tmpblk.c[len] = '\0';

	/* make the change */
	ChangeText
	{
		frommark &= ~(BLKSIZE - 1);
		tomark &= ~(BLKSIZE - 1);
		tomark += BLKSIZE;
		change(frommark, tomark, tmpblk.c);
	}

	/* Reporting... */
	rptlines = markline(tomark) - markline(frommark) - 1L;
	rptlabel = "joined";
}
