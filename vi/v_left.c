/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "$Id: v_left.c,v 8.5 1994/03/03 20:04:54 bostic Exp $ (Berkeley) $Date: 1994/03/03 20:04:54 $";
#endif /* not lint */

#include <sys/types.h>

#include "vi.h"
#include "vcmd.h"

/*
 * v_left -- [count]^H, [count]h
 *	Move left by columns.
 */
int
v_left(sp, ep, vp)
	SCR *sp;
	EXF *ep;
	VICMDARG *vp;
{
	recno_t cnt;

	/*
	 * !!!
	 * The ^H and h commands always failed in the first column.
	 */
	if (vp->m_start.cno == 0) {
		v_sol(sp);
		return (1);
	}

	/* Find the end of the range. */
	cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;
	if (vp->m_start.cno > cnt)
		vp->m_stop.cno = vp->m_start.cno - cnt;
	else
		vp->m_stop.cno = 0;

	/*
	 * VC_D and non-motion commands move to the end of the range,
	 * VC_Y stays at the start.  Ignore VC_C and VC_S.  Motion
	 * commands adjust the starting point to the character before
	 * the current one.
	 */
	vp->m_final = F_ISSET(vp, VC_Y) ? vp->m_start : vp->m_stop;
	if (ISMOTION(vp))
		--vp->m_start.cno;
	return (0);
}

/*
 * v_cfirst -- [count]_
 *	Move to the first non-blank character in a line.
 */
int
v_cfirst(sp, ep, vp)
	SCR *sp;
	EXF *ep;
	VICMDARG *vp;
{
	recno_t cnt;

	/*
	 * !!!
	 * If the _ is a motion component, it makes the command a line motion
	 * e.g. "d_" deletes the line.  It also means that the cursor doesn't
	 * move.
	 *
	 * The _ command never failed in the first column.
	 */
	if (ISMOTION(vp))
		F_SET(vp, VM_LMODE);
	/*
	 * !!!
	 * Historically a specified count makes _ move down count - 1
	 * rows, so, "3_" is the same as "2j_".
	 */
	cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;
	if (cnt != 1) {
		--vp->count;
		return (v_down(sp, ep, vp));
	}

	/*
	 * Move to the first non-blank.
	 *
	 * Can't just use RCM_SET_FNB, in case _ is used as the motion
	 * component of another command.
	 */
	vp->m_stop.cno = 0;
	if (nonblank(sp, ep, vp->m_stop.lno, &vp->m_stop.cno))
		return (1);

	/*
	 * VC_D and non-motion commands move to the end of the range,
	 * VC_Y stays at the start.  Ignore VC_C and VC_S.
	 */
	vp->m_final = F_ISSET(vp, VC_Y) ? vp->m_start : vp->m_stop;
	return (0);
}

/*
 * v_first -- ^
 *	Move to the first non-blank character in this line.
 */
int
v_first(sp, ep, vp)
	SCR *sp;
	EXF *ep;
	VICMDARG *vp;
{
	/*
	 * !!!
	 * Yielding to none in our quest for compatibility with every
	 * historical blemish of vi, no matter how strange it might be,
	 * we permit the user to enter a count and then ignore it.
	 */

	/*
	 * Move to the first non-blank.
	 *
	 * Can't just use RCM_SET_FNB, in case ^ is used as the motion
	 * component of another command.
	 */
	vp->m_stop.cno = 0;
	if (nonblank(sp, ep, vp->m_stop.lno, &vp->m_stop.cno))
		return (1);

	/*
	 * !!!
	 * The ^ command succeeded if used as a command without a whitespace
	 * character preceding the cursor in the line, but failed if used as
	 * a motion component in the same situation.
	 */
	if (ISMOTION(vp) && vp->m_start.cno <= vp->m_stop.cno) {
		v_sol(sp);
		return (1);
	}

	/*
	 * VC_D and non-motion commands move to the end of the range,
	 * VC_Y stays at the start.  Ignore VC_C and VC_S.  Motion
	 * commands adjust the starting point to the character before
	 * the current one.
	 */
	vp->m_final = F_ISSET(vp, VC_Y) ? vp->m_start : vp->m_stop;
	if (ISMOTION(vp))
		--vp->m_start.cno;
	return (0);
}

/*
 * v_ncol -- [count]|
 *	Move to column count or the first column on this line.  If the
 *	requested column is past EOL, move to EOL.  The nasty part is
 *	that we have to know character column widths to make this work.
 */
int
v_ncol(sp, ep, vp)
	SCR *sp;
	EXF *ep;
	VICMDARG *vp;
{
	if (F_ISSET(vp, VC_C1SET) && vp->count > 1) {
		--vp->count;
		vp->m_stop.cno = sp->s_chposition(sp,
		    ep, vp->m_start.lno, (size_t)vp->count);
		/*
		 * !!!
		 * The | command succeeded if used as a command and the cursor
		 * didn't move, but failed if used as a motion component in the
		 * same situation.
		 */
		if (ISMOTION(vp) && vp->m_stop.cno == vp->m_start.cno) {
			v_nomove(sp);
			return (1);
		}
	} else {
		/*
		 * !!!
		 * The | command succeeded if used as a command in column 0
		 * without a count, but failed if used as a motion component
		 * in the same situation.
		 */
		if (ISMOTION(vp) && vp->m_start.cno == 0) {
			v_sol(sp);
			return (1);
		}
		vp->m_stop.cno = 0;
	}

	/*
	 * If moving right, non-motion commands move to the end of the range.
	 * VC_D and VC_Y stay at the start.  If moving left, non-motion and
	 * VC_D commands move to the end of the range.  VC_Y remains at the
	 * start.  Ignore VC_C and VC_S.  Motion left commands adjust the
	 * starting point to the character before the current one.
	 */
	if (vp->m_start.cno > vp->m_stop.cno) {
		vp->m_final = vp->m_stop;
		if (ISMOTION(vp)) {
			if (F_ISSET(vp, VC_Y))
				vp->m_final = vp->m_start;
			--vp->m_start.cno;
		}
	} else
		vp->m_final = ISMOTION(vp) ? vp->m_start : vp->m_stop;
	return (0);
}

/*
 * v_zero -- 0
 *	Move to the first column on this line.
 */
int
v_zero(sp, ep, vp)
	SCR *sp;
	EXF *ep;
	VICMDARG *vp;
{
	/*
	 * !!!
	 * The 0 command succeeded if used as a command in the first column
	 * but failed if used as a motion component in the same situation.
	 */
	if (ISMOTION(vp) && vp->m_start.cno == 0) {
		v_sol(sp);
		return (1);
	}

	/*
	 * VC_D and non-motion commands move to the end of the range,
	 * VC_Y stays at the start.  Ignore VC_C and VC_S.  Motion
	 * commands adjust the starting point to the character before
	 * the current one.
	 */
	vp->m_stop.cno = 0;
	vp->m_final = F_ISSET(vp, VC_Y) ? vp->m_start : vp->m_stop;
	if (ISMOTION(vp))
		--vp->m_start.cno;
	return (0);
}
