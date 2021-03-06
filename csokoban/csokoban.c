/* csokoban.c: Top-level functions.
 *
 * Copyright (C) 2000 by Brian Raiter, under the GNU General Public
 * License. No warranty. See COPYING for details.
 */

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>
#include	<getopt.h>
#include	"gen.h"
#include	"csokoban.h"
#include	"movelist.h"
#include	"dirio.h"
#include	"fileread.h"
#include	"answers.h"
#include	"play.h"
#include	"userio.h"

/* The default directory for the puzzle files.
 */
#ifndef DATADIR
#define	DATADIR		"/usr/local/share/csokoban"
#endif

/* Structure used to pass data back from readcmdline().
 */
typedef	struct startupdata {
    char       *filename;	/* which puzzle file to use */
    int		level;		/* which puzzle to start at */
    int		silence;	/* FALSE if we are allowed to ring the bell */
    int		listseries;	/* TRUE if the files should be displayed */
    int		writeanswer;	/* TRUE if the solution should be displayed */
} startupdata;

/* Online help.
 */
static char const *yowzitch = 
	"Usage: csokoban [-hvqlwW] [-D DIR] [-S DIR] [NAME] [-LEVEL]\n"
	"   -h  Display this help\n"
	"   -v  Display version information\n"
	"   -l  Print out the list of available setup files\n"
	"   -w  Print out the solution for the specified level\n"
	"   -W  Same as -w, but using the least-pushes solution\n"
	"   -D  Read setup files from DIR instead of the default\n"
	"   -S  Save games in DIR instead of the default\n"
	"   -q  Be quiet; don't ring the bell\n"
	"NAME specifies which setup file to read.\n"
	"LEVEL specifies which level number to start with.\n"
	"(Press ? during the game for further help.)\n";

/* Version information.
 */
static char const *vourzhon =
	"csokoban, version 1.3. Copyright (C) 2000 by Brian Raiter\n"
	"under the terms of the GNU General Public License.\n";

/* The list of available puzzle files.
 */
static gameseries      *serieslist = NULL;

/* The size of the serieslist array.
 */
static int		seriescount = 0;

/* The index of the current series.
 */
static int		currentseries = 0;

/* The index of the current puzzle within the current series.
 */
static int		currentgame = 0;

/* How to initialize the redo list for the next puzzle.
 */
static int		usemoves = TRUE;

/*
 * Game-choosing functions
 */

/* Ensure that the current puzzle has actually been read into
 * memory. If currentgame is less than zero, or exceeds the total
 * number of puzzles in the current series, then change the current
 * series if possible. (currentgame and currentseries are
 * appropriately updated in this case.) FALSE is returned if the
 * current puzzle does not exist.
 */
int readlevel(void)
{
    int	level = currentgame;

    while (level < 0) {
	if (!currentseries)
	    return FALSE;
	--currentseries;
	level += serieslist[currentseries].count;
    }
    while (level >= serieslist[currentseries].count) {
	if (!readlevelinseries(serieslist + currentseries, level)) {
	    if (level < serieslist[currentseries].count)
		return FALSE;
	    if (currentseries + 1 >= seriescount)
		return FALSE;
	    level -= serieslist[currentseries].count;
	    ++currentseries;
	}
    }
    currentgame = level;
    return TRUE;
}

/* Print out a list of the available puzzle files.
 */
static void displayserieslist(void)
{
    int	namewidth;
    int	i, n;

    namewidth = 0;
    for (i = 0 ; i < seriescount ; ++i) {
	n = strlen(serieslist[i].filename);
	if (!readlevelinseries(serieslist + i, 0)) {
	    *serieslist[i].filename = '\0';
	    continue;
	}
	if (n > 4 && !strcmp(serieslist[i].filename + n - 4, ".txt")) {
	    n -= 4;
	    serieslist[i].filename[n] = '\0';
	}
	if (n > namewidth)
	    namewidth = n;
    }
    for (i = 0 ; i < seriescount ; ++i) {
	if (*serieslist[i].name)
	    printf("%-*.*s  %s\n", namewidth, namewidth,
				   serieslist[i].filename,
				   serieslist[i].name);
	else
	    puts(serieslist[i].filename);
    }
}

/* Initialize currentseries to point to the chosen puzzle file, and
 * currentgame to the chosen puzzle. If the user didn't request a
 * specific file, start at the first one and use all of them. If the
 * user didn't request a specific puzzle, select the first one for
 * which the user has not found a solution, or the first one if all
 * available puzzles have been solved.
 */
static void pickstartinggame(char const *startfile, int startlevel)
{
    int	i, n;

    if (*startfile) {
	n = strlen(startfile);
	for (i = 0 ; i < seriescount ; ++i)
	    if (!strncmp(serieslist[i].filename, startfile, n))
		break;
	if (i == seriescount)
	    die("Couldn't find any data file named \"%s\".", startfile);
	serieslist += i;
	seriescount = 1;
    }
    currentseries = 0;

    if (startlevel) {
	currentgame = startlevel - 1;
	if (!readlevel()) {
	    if (*startfile)
		die("Couldn't find a level %d in %s.", startlevel, startfile);
	    else
		die("Couldn't find a level %d to start with.", startlevel);
	}
    } else {
	currentgame = 0;
	if (!readlevel()) {
	    if (*startfile)
		die("Couldn't find any levels in %s.", startfile);
	    else
		die("Couldn't find any levels in any files in %s.", datadir);
	}
	while (serieslist[currentseries].games[currentgame].movebestcount) {
	    ++currentgame;
	    if (!readlevel()) {
		currentseries = 0;
		currentgame = 0;
		break;
	    }
	}
    }
}

/*
 * User interface functions
 */

/* Save an incomplete solution.
 */
static int partialsave(void)
{
    return replaceanswers(TRUE) && saveanswers(serieslist + currentseries);
}

/* Display information on the various key commands during a game.
 */
static void drawhelpscreen(void)
{
    static char const  *keys[] = { "h j k l\0move",
				   "arrows\0    \"",
				   "x\0undo move",
				   "X\0undo eight moves",
				   "z\0redo undone move",
				   "Z\0redo eight undone moves",
				   "R ^R\0return to starting position",
				   "s\0save current position",
				   "r\0restore saved position",
				   "m\0toggle macro recording",
				   "p\0play current macro",
				   "S\0save current position to disk",
				   "P\0previous level",
				   "N\0next level",
				   "Ctrl-L\0redraw screen",
				   "q\0quit"
    };

    displayhelp(keys, sizeof keys / sizeof *keys);
    input();
}

/* Get a keystroke from the user (or from a macro) and perform the
 * indicated command. A non-zero return value indicates a request to
 * change the current puzzle, the actual value being a delta.
 */
static int doturn(void)
{
    if (isplaying()) {
	macromove();
	return 0;
    }

    switch (input()) {
      case 'k':		newmove(-XSIZE);			break;
      case 'l':		newmove(+1);				break;
      case 'j':		newmove(+XSIZE);			break;
      case 'h':		newmove(-1);				break;
      case 'K':		while (newmove(-XSIZE)) ;		break;
      case 'L':		while (newmove(+1)) ;			break;
      case 'J':		while (newmove(+XSIZE)) ;		break;
      case 'H':		while (newmove(-1)) ;			break;
      case 'x':		if (!undomove())		ding();	break;
      case 'z':		if (!redomove())		ding();	break;
      case 'X':		if (!undomoves(8))		ding();	break;
      case 'Z':		if (!redomoves(8))		ding();	break;
      case 'R':		initgamestate(TRUE);			break;
      case '\022':	initgamestate(FALSE);			break;
      case 's':		savestate();				break;
      case 'r':		if (!restorestate())		ding();	break;
      case 'S':		if (!partialsave())		ding();	break;
      case 'm':		setmacro();				break;
      case 'p':		if (!startmacro())		ding();	break;
      case '?':		drawhelpscreen();			break;
      case '\f':						break;
      case 'P':		return -1;
      case 'N':		return +1;
      case 'q':		exit(0);
      case 'Q':		exit(0);
    }

    return 0;
}

/* Get a keystroke from the user at the completion of the current
 * puzzle. The default behavior of advancing to the next puzzle is
 * overridden by a non-zero return value.
 */
static int endinput(int endofsession)
{
    displayendmessage(endofsession);
    for (;;) {
	switch (input()) {
	  case '\n':	usemoves = TRUE;	return 0;
	  case 'N':	usemoves = TRUE;	return 0;
	  case 'R':	usemoves = TRUE;	return -1;
	  case 'P':	usemoves = TRUE;	return -2;
	  case '\016':	usemoves = FALSE;	return 0;
	  case '\022':	usemoves = FALSE;	return -1;
	  case '\020':	usemoves = FALSE;	return -2;
	  case 'q':				exit(0);
	  case 'Q':				exit(0);
	}
    }
}

/* Play the current puzzle. Return when the user solves the puzzle or
 * requests a different puzzle.
 */
static void playgame(void)
{
    int index, n;

    index = 0;
    for (n = 0 ; n < currentseries ; ++n)
	index += serieslist[n].count;
    index += currentgame;

    if (drawscreen(index)) {
	do {
	    if ((n = doturn())) {
		currentgame += n;
		if (readlevel())
		    break;
		currentgame -= n;
		ding();
	    }
	    drawscreen(index);
	} while (!checkfinished());
	freesavedstates();
	if (checkfinished() && replaceanswers(FALSE))
	    saveanswers(serieslist + currentseries);
    }

    if (!n) {
	++currentgame;
	n = endinput(!readlevel());
	currentgame += n;
    }
}

/*
 * Main functions
 */

/* Initialize the user-controlled options to their default values, and
 * then parse the command-line options and arguments.
 */
static void initwithcmdline(int argc, char *argv[], startupdata *start)
{
    char const *dir;
    int		ch;

    programname = argv[0];

    datadir = getpathbuffer();
    copypath(datadir, DATADIR);

    savedir = getpathbuffer();
    if ((dir = getenv("SOKSAVEDIR"))) {
	strncpy(savedir, dir, sizeof savedir - 1);
	savedir[sizeof savedir - 1] = '\0';
    } else if ((dir = getenv("HOME")))
	sprintf(savedir, "%.*s/.csokoban", (int)(sizeof savedir - 11), dir);

    start->filename = getpathbuffer();
    *start->filename = '\0';
    start->level = 0;
    start->silence = FALSE;
    start->listseries = FALSE;
    start->writeanswer = FALSE;

    while ((ch = getopt(argc, argv, "0123456789D:S:hlqvWw")) != EOF) {
	switch (ch) {
	  case '0': case '1': case '2': case '3': case '4':
	  case '5': case '6': case '7': case '8': case '9':
	    start->level = start->level * 10 + ch - '0';
	    break;
	  case 'D':	strncpy(datadir, optarg, sizeof datadir - 1);	break;
	  case 'S':	strncpy(savedir, optarg, sizeof savedir - 1);	break;
	  case 'q':	start->silence = TRUE;				break;
	  case 'l':	start->listseries = TRUE;			break;
	  case 'W':	start->writeanswer = -1;			break;
	  case 'w':	start->writeanswer = +1;			break;
	  case 'h':	fputs(yowzitch, stdout); 	exit(EXIT_SUCCESS);
	  case 'v':	fputs(vourzhon, stdout); 	exit(EXIT_SUCCESS);
	  default:	fputs(yowzitch, stderr); 	exit(EXIT_FAILURE);
	}
    }

    if (optind < argc)
	start->filename = argv[optind++];
    if (optind < argc) {
	fputs(yowzitch, stderr);
	exit(EXIT_FAILURE);
    }
}

/* main().
 */
int main(int argc, char *argv[])
{
    startupdata	start;

    initwithcmdline(argc, argv, &start);

    if (!getseriesfiles(start.filename, &serieslist, &seriescount))
	return EXIT_FAILURE;

    if (start.listseries) {
	displayserieslist();
	return EXIT_SUCCESS;
    }

    pickstartinggame(start.filename, start.level);

    if (start.writeanswer) {
	selectgame(serieslist[currentseries].games + currentgame, currentgame);
	initgamestate(start.writeanswer > 0);
	if (!displaygamesolution())
	    die("No solution exists for \"%s\", level %d.",
		serieslist[currentseries].name, currentgame);
	return EXIT_SUCCESS;
    }

    if (!ioinitialize(start.silence))
	die("Failed to initialize terminal.");

    for (;;) {
	selectgame(serieslist[currentseries].games + currentgame, currentgame);
	initgamestate(usemoves);
	playgame();
	if (!readlevel())
	    break;
    }

    return EXIT_SUCCESS;
}
