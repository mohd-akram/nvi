#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>

#include "curses.h"

#define	BLKSIZE	2048
#define	REG	register
#define	UCHAR(c)	((u_char)(c))

/*------------------------------------------------------------------------*/
/* Miscellaneous constants.						  */

#define INFINITY	2000000001L	/* a very large integer */
#ifndef MAXRCLEN
# define MAXRCLEN	1000		/* longest possible .exrc file */
#endif

/*------------------------------------------------------------------------*/
/* These describe how temporary files are divided into blocks             */

#define MAXBLKS	(BLKSIZE / sizeof(unsigned short))
typedef union
{
	char		c[BLKSIZE];	/* for text blocks */
	unsigned short	n[MAXBLKS];	/* for the header block */
}
	BLK;

/*------------------------------------------------------------------------*/
/* These are used manipulate BLK buffers.                                 */

extern BLK	hdr;		/* buffer for the header block */
extern BLK	*blkget();	/* given index into hdr.c[], reads block */
extern BLK	*blkadd();	/* inserts a new block into hdr.c[] */

/*------------------------------------------------------------------------*/
/* These are used to keep track of various flags                          */
extern struct _viflags
{
	short	file;		/* file flags */
}
	viflags;

/* file flags */
#define NEWFILE		0x0001	/* the file was just created */
#define READONLY	0x0002	/* the file is read-only */
#define HADNUL		0x0004	/* the file contained NUL characters */
#define MODIFIED	0x0008	/* the file has been modified */
#define NOFILE		0x0010	/* no name is known for the current text */
#define ADDEDNL		0x0020	/* newlines were added to the file */
#define HADBS		0x0040	/* backspace chars were lost from the file */

/* macros used to set/clear/test flags */
#define setflag(x,y)	viflags.x |= y
#define clrflag(x,y)	viflags.x &= ~y
#define tstflag(x,y)	(viflags.x & y)
#define initflags()	viflags.file = 0;

#ifndef MAXPATHLEN			/* XXX */
#define	MAXPATHLEN	1024
#endif

/*------------------------------------------------------------------------*/
/* These help support the single-line multi-change "undo" -- shift-U      */

extern char	U_text[BLKSIZE];
extern long	U_line;

typedef long MARK;

#define markline(x)	(long)((x) / BLKSIZE)
#define markidx(x)	(int)((x) & (BLKSIZE - 1))
#define MARK_UNSET	((MARK)0)
#define MARK_FIRST	((MARK)BLKSIZE)
#define MARK_LAST	((MARK)(nlines * BLKSIZE))
#define MARK_AT_LINE(x)	((MARK)((x) * BLKSIZE))

#define NMARKS	29
extern MARK	mark[NMARKS];	/* marks a-z, plus mark ' and two temps */
extern MARK	cursor;		/* mark where line is */

/*------------------------------------------------------------------------*/
/* These are used to keep track of the current & previous files.	  */

extern long	origtime;	/* modification date&time of the current file */
extern char	origname[256];	/* name of the current file */
extern char	prevorig[256];	/* name of the preceding file */
extern long	prevline;	/* line number from preceding file */

/*------------------------------------------------------------------------*/
/* misc housekeeping variables & functions				  */

extern int	tmpfd;		/* fd used to access the tmp file */
extern int	tmpnum;		/* counter used to generate unique filenames */
extern long	lnum[MAXBLKS];	/* last line# of each block */
extern long	nlines;		/* number of lines in the file */
extern char	args[BLKSIZE];	/* file names given on the command line */
extern int	argno;		/* the current element of args[] */
extern int	nargs;		/* number of filenames in args */
extern long	changes;	/* counts changes, to prohibit short-cuts */
extern int	significant;	/* boolean: was a *REAL* change made? */
extern BLK	tmpblk;		/* a block used to accumulate changes */
extern long	topline;	/* file line number of top line */
extern int	leftcol;	/* column number of left col */
#define		botline	 (topline + LINES - 2)
#define		rightcol (leftcol + COLS - (LVAL(O_NUMBER) ? 9 : 1))
extern int	physcol;	/* physical column number that cursor is on */
extern int	physrow;	/* physical row number that cursor is on */
extern int	exwrote;	/* used to detect verbose ex commands */
extern int	doingdot;	/* boolean: are we doing the "." command? */
extern int	doingglobal;	/* boolean: are doing a ":g" command? */
#ifndef CRUNCH
extern int	force_lnmd;	/* boolean: force a command to work in line mode? */
#endif
extern long	rptlines;	/* number of lines affected by a command */
extern char	*rptlabel;	/* description of how lines were affected */
extern char	*fetchline();	/* read a given line from tmp file */
extern char	*parseptrn();	/* isolate a regexp in a line */
extern MARK	paste();	/* paste from cut buffer to a given point */
extern char	*wildcard();	/* expand wildcards in filenames */
extern MARK	input();	/* inserts characters from keyboard */
extern char	*linespec();	/* finds the end of a /regexp/ string */
#define		ctrl(ch) ((ch)&037)
#ifndef NO_RECYCLE
extern long	allocate();	/* allocate a free block of the tmp file */
#endif
extern void	blkdirty();	/* marks a block as being "dirty" */
extern void	blkflush();	/* writes a single dirty block to the disk */
extern void	blksync();	/* forces all "dirty" blocks to disk */
extern void	blkinit();	/* resets the block cache to "empty" state */
extern void	beep();		/* rings the terminal's bell */
extern void	exrefresh();	/* writes text to the screen */
extern void	endmsgs();	/* if "manymsgs" is set, then scroll up 1 line */
extern void	garbage();	/* reclaims any garbage blocks */
extern void	redraw();	/* updates the screen after a change */
extern void	resume_curses();/* puts the terminal in "cbreak" mode */
extern void	beforedo();	/* saves current revision before a new change */
extern void	afterdo();	/* marks end of a beforedo() change */
extern void	abortdo();	/* like "afterdo()" followed by "undo()" */
extern int	undo();		/* restores file to previous undo() */
extern void	dumpkey();	/* lists key mappings to the screen */
extern void	mapkey();	/* defines a new key mapping */
extern void	savekeys();	/* lists key mappings to a file */
extern void	redrawrange();	/* records clues from modify.c */
extern void	cut();		/* saves text in a cut buffer */
extern void	delete();	/* deletes text */
extern void	add();		/* adds text */
extern void	change();	/* deletes text, and then adds other text */
extern void	cutswitch();	/* updates cut buffers when we switch files */
extern void	do_abbr();	/* defines or lists abbreviations */
extern void	do_digraph();	/* defines or lists digraphs */
extern void	exstring();	/* execute a string as EX commands */
extern void	dumpopts();
extern void	setopts();
extern void	saveopts();
extern void	savedigs();
extern void	saveabbr();
extern void	savecolor();
extern void	cutname();
extern void	cutname();
extern void	initopts();
extern void	cutend();

/*------------------------------------------------------------------------*/
/* macros that are used as control structures                             */

#define BeforeAfter(before, after) for((before),bavar=1;bavar;(after),bavar=0)
#define ChangeText	BeforeAfter(beforedo(FALSE),afterdo())

extern int	bavar;		/* used only in BeforeAfter macros */

/*------------------------------------------------------------------------*/
/* These are the movement commands.  Each accepts a mark for the starting */
/* location & number and returns a mark for the destination.		  */

extern MARK	m_updnto();		/* k j G */
extern MARK	m_right();		/* h */
extern MARK	m_left();		/* l */
extern MARK	m_tocol();		/* | */
extern MARK	m_front();		/* ^ */
extern MARK	m_rear();		/* $ */
extern MARK	m_fword();		/* w */
extern MARK	m_bword();		/* b */
extern MARK	m_eword();		/* e */
extern MARK	m_paragraph();		/* { } [[ ]] */
extern MARK	m_match();		/* % */
#ifndef NO_SENTENCE
 extern MARK	m_fsentence();		/* ) */
 extern MARK	m_bsentence();		/* ( */
#endif
extern MARK	m_tomark();		/* 'm */
#ifndef NO_EXTENSIONS
extern MARK	m_wsrch();		/* ^A */
#endif
extern MARK	m_nsrch();		/* n */
extern MARK	m_Nsrch();		/* N */
extern MARK	m_fsrch();		/* /regexp */
extern MARK	m_bsrch();		/* ?regexp */
#ifndef NO_CHARSEARCH
 extern MARK	m__ch();		/* ; , */
 extern MARK	m_fch();		/* f */
 extern MARK	m_tch();		/* t */
 extern MARK	m_Fch();		/* F */
 extern MARK	m_Tch();		/* T */
#endif
extern MARK	m_row();		/* H L M */
extern MARK	m_z();			/* z */
extern MARK	m_scroll();		/* ^B ^F ^E ^Y ^U ^D */

/* Some stuff that is used by movement functions... */

extern MARK	adjmove();		/* a helper fn, used by move fns */

/* This macro is used to set the default value of cnt for an operation */
#define SETDEFCNT(val) { \
	if (cnt < 1) \
		cnt = (val); \
}

/* These are used to minimize calls to fetchline() */
extern int	plen;	/* length of the line */
extern long	pline;	/* line number that len refers to */
extern long	pchgs;	/* "changes" level that len refers to */
extern char	*ptext;	/* text of previous line, if valid */
extern void	pfetch();
extern char	digraph();

/* This is used to build a MARK that corresponds to a specific point in the
 * line that was most recently pfetch'ed.
 */
#define buildmark(text)	(MARK)(BLKSIZE * pline + (int)((text) - ptext))


/*------------------------------------------------------------------------*/

extern void	ex();
extern void	vi();

/*----------------------------------------------------------------------*/
/* These are used to handle VI commands 				*/

extern MARK	v_1ex();	/* : */
extern MARK	v_mark();	/* m */
extern MARK	v_quit();	/* Q */
extern MARK	v_redraw();	/* ^L ^R */
extern MARK	v_undo();	/* u */
extern MARK	v_xchar();	/* x X */
extern MARK	v_replace();	/* r */
extern MARK	v_overtype();	/* R */
extern MARK	v_selcut();	/* " */
extern MARK	v_paste();	/* p P */
extern MARK	v_yank();	/* y Y */
extern MARK	v_delete();	/* d D */
extern MARK	v_join();	/* J */
extern MARK	v_insert();	/* a A i I o O */
extern MARK	v_change();	/* c C */
extern MARK	v_subst();	/* s */
extern MARK	v_lshift();	/* < */
extern MARK	v_rshift();	/* > */
extern MARK	v_reformat();	/* = */
extern MARK	v_filter();	/* ! */
extern MARK	v_status();	/* ^G */
extern MARK	v_switch();	/* ^^ */
extern MARK	v_tag();	/* ^] */
extern MARK	v_xit();	/* ZZ */
extern MARK	v_undoline();	/* U */
extern MARK	v_again();	/* & */
#ifndef NO_EXTENSIONS
 extern MARK	v_keyword();	/* ^K */
 extern MARK	v_increment();	/* * */
#endif
#ifndef NO_ERRLIST
 extern MARK	v_errlist();	/* * */
#endif
#ifndef NO_AT
 extern MARK	v_at();		/* @ */
#endif
#ifndef NO_POPUP
 extern MARK	v_popup();	/* \ */
#endif

/*----------------------------------------------------------------------*/
/* These describe what mode we're in */

#define MODE_EX		1	/* executing ex commands */
#define	MODE_VI		2	/* executing vi commands */
#define	MODE_COLON	3	/* executing an ex command from vi mode */
#define	MODE_QUIT	4
extern int	mode;

#define WHEN_VICMD	1	/* getkey: we're reading a VI command */
#define WHEN_VIINP	2	/* getkey: we're in VI's INPUT mode */
#define WHEN_VIREP	4	/* getkey: we're in VI's REPLACE mode */
#define WHEN_EX		8	/* getkey: we're in EX mode */
#define WHEN_MSG	16	/* getkey: we're at a "more" prompt */
#define WHEN_POPUP	32	/* getkey: we're in the pop-up menu */
#define WHEN_REP1	64	/* getkey: we're getting a single char for 'r' */
#define WHEN_CUT	128	/* getkey: we're getting a cut buffer name */
#define WHEN_MARK	256	/* getkey: we're getting a mark name */
#define WHEN_CHAR	512	/* getkey: we're getting a destination for f/F/t/T */
#define WHEN_INMV	4096	/* in input mode, interpret the key in VICMD mode */
#define WHEN_FREE	8192	/* free the keymap after doing it once */
#define WHENMASK	(WHEN_VICMD|WHEN_VIINP|WHEN_VIREP|WHEN_REP1|WHEN_CUT|WHEN_MARK|WHEN_CHAR)

#ifndef NO_VISIBLE
extern MARK	V_from;
extern int	V_linemd;
extern MARK	v_start();
#endif

/*----------------------------------------------------------------------*/
/* These are used to pass info about ^V quoting */
#ifdef NO_QUOTE
# define QSTART(base)
# define QEND()
# define QSET(addr)
# define QCLR(addr)
# define QCHK(addr)	FALSE
#else
  extern char	Qflags[132];
  extern char	*Qbase;
  extern int	Qlen;
# define QSTART(base)	(Qbase = base, bzero(Qflags, sizeof(Qflags)))
# define QEND()		(Qlen = strlen(Qbase))
# define QSET(addr)	(Qflags[(int)((addr) - Qbase)] = TRUE)
# define QCLR(addr)	(Qflags[(int)((addr) - Qbase)] = FALSE)
# define QCHK(addr)	(Qflags[(int)((addr) - Qbase)])
#endif
#ifdef DEBUG
# define QTST(addr)	(((int)((addr) - Qbase) < Qlen && (int)((addr) - Qbase) > 0) ? QCHK(addr) : (abort(), 0))
#else
# define QTST(addr)	QCHK(addr)
#endif

#ifdef notdef
/* Structure passed around to functions implementing ex commands. */
typedef struct _cmdarg {
	int addrcnt;		/* Address count. */
	CMDLIST *cmd;		/* Command structure. */
	MARK addr1;		/* 1st address. */
	MARK addr2;		/* 2nd address. */
	int force;		/* If command is forced. */
	int argc;		/* Argument count. */
	char **argv;		/* Arguments. */
} CMDARG;

extern char *defcmdarg[2];

/* Macro to set up the structure. */
#define	SETCMDARG(s, _addrcnt, _addr1, _addr2, _force, _arg) { \
	s.addrcnt = (_addrcnt); \
	s.addr1 = (_addr1); \
	s.addr2 = (_addr2); \
	s.force = (_force); \
	s.argc = _arg ? 1 : 0; \
	s.argv = defcmdarg; \
	defcmdarg[0] = _arg; \
}
#endif
