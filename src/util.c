/*
    util.c

    Copyright (C) 1999-2017 Chris Allegretta
    Copyright (C) 2017-Present Abishek V Ashok
    This version (fork) by shr00mie.

    Utility functions: exit path, usage, version, allocation.
*/

#include "cmatrix.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif

/* ---------------------------------------------------------------------------
 * Run a shell command from a printf-style format string. Used for
 * consolechars/setfont and for restoring the font on exit.
 * --------------------------------------------------------------------------- */
int va_system(char *str, ...) {
    va_list ap;
    char buf[133];

    va_start(ap, str);
    vsnprintf(buf, sizeof(buf), str, ap);
    va_end(ap);
    return system(buf);
}

/* ---------------------------------------------------------------------------
 * Normal exit: restore cursor, clear screen, end ncurses, optionally
 * restore console font (console mode), then exit(0).
 * --------------------------------------------------------------------------- */
void finish(void) {
    cmatrix_restore_terminal_font();
    curs_set(1);
    clear();
    refresh();
    resetty();
    endwin();
    if (console) {
#ifdef HAVE_CONSOLECHARS
        va_system("consolechars -d");
#elif defined(HAVE_SETFONT)
        va_system("setfont");
#endif
    }
    exit(0);
}

/* ---------------------------------------------------------------------------
 * Fatal error exit: same cleanup as finish(), then vfprintf to stderr
 * and exit(0). Used when malloc fails or option/init errors occur.
 * --------------------------------------------------------------------------- */
void c_die(char *msg, ...) {
    va_list ap;

    cmatrix_restore_terminal_font();
    curs_set(1);
    clear();
    refresh();
    resetty();
    endwin();

    if (console) {
#ifdef HAVE_CONSOLECHARS
        va_system("consolechars -d");
#elif defined(HAVE_SETFONT)
        va_system("setfont");
#endif
    }

    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
    exit(0);
}

/* ---------------------------------------------------------------------------
 * Print command-line usage (-h). Documents all options (scroll, bold,
 * colors, delay, lock/message, classic Japanese, etc.).
 * --------------------------------------------------------------------------- */
void usage(void) {
    printf(" Usage: cmatrix -[abcfhlsmVk] [-F fps] [-T color] [-H color] [-O color] [-M message]\n");
    printf(" -a: Asynchronous scroll\n");
    printf(" -b: Bold characters on\n");
    printf(" -c: Use Japanese characters as seen in the original matrix. Requires appropriate fonts\n");
    printf(" -f: Force the linux $TERM type to be on\n");
    printf(" -l: Linux mode (uses setfont/consolechars; install a console font separately for -l)\n");
    printf(" -L: Lock mode (can be closed from another terminal)\n");
    printf(" -h: Print usage and exit\n");
    printf(" -n: No bold characters (overrides -b, default)\n");
    printf(" -s: \"Screensaver\" mode, exits on first keystroke\n");
    printf(" -V: Print version information and exit\n");
    printf(" -M [message]: Prints your message in the center of the screen. Overrides -L's default message.\n");
    printf(" -F fps: Frame rate (default 23.976; e.g. 30, 60)\n");
    printf(" -T [color]: Use this color for matrix tail (default green)\n");
    printf(" -H [color]: Use this color for drop head (default white)\n");
    printf(" -O [color]: Use this color for -M message (default red)\n");
    printf(" -k: Characters change while scrolling\n");
}

/* ---------------------------------------------------------------------------
 * Print version and build info (-V): version string, compile time/date,
 * contact and project URL.
 * --------------------------------------------------------------------------- */
void version(void) {
    printf(" CMatrix version %s (compiled %s, %s)\n",
        VERSION, __TIME__, __DATE__);
    printf("Rain: Matrix PUA U+E000-U+E067 (104 glyphs; patched font grid idx 1962-2065). Set terminal font to DejaVu Sans Mono (install merged DejaVuSansMono_patched.ttf).\n");
    printf("Original author: Chris Allegretta. 2017-Present: Abishek V Ashok.\n");
    printf("This version by shr00mie.\n");
}

/* ---------------------------------------------------------------------------
 * Allocate memory or die: malloc(howmuch); on failure calls c_die().
 * Used for matrix and per-column arrays so the rest of the code can
 * assume non-NULL pointers.
 * --------------------------------------------------------------------------- */
void *nmalloc(size_t howmuch) {
    void *r;

    if (!(r = malloc(howmuch))) {
        c_die("CMatrix: malloc: out of memory!");
    }

    return r;
}
