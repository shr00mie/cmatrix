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

/* ---------------------------------------------------------------------------
 * Normal exit: restore cursor, clear screen, end notcurses, then exit(0).
 * --------------------------------------------------------------------------- */
void finish(void) {
    cmatrix_restore_terminal_font();
    cmatrix_notcurses_stop();
    exit(0);
}

/* ---------------------------------------------------------------------------
 * Fatal error exit: same cleanup as finish(), then vfprintf to stderr
 * and exit(0). Used when malloc fails or option/init errors occur.
 * --------------------------------------------------------------------------- */
void c_die(char *msg, ...) {
    va_list ap;

    cmatrix_restore_terminal_font();
    cmatrix_notcurses_stop();

    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
    exit(0);
}

/* ---------------------------------------------------------------------------
 * Print command-line usage (-h). Documents all options (scroll, bold,
 * colors, delay, lock/message, matrix glyphs (-c), etc.).
 * --------------------------------------------------------------------------- */
void usage(void) {
    printf(" Usage: cmatrix -[abchskV] [-F fps] [-T color] [-H color] [-O color] [-M message]\n");
    printf(" -a: Asynchronous scroll\n");
    printf(" -b: Bold characters on\n");
    printf(" -c: Matrix glyph mode (movie-style column stream). Requires a UTF-8 font with the right code points\n");
    printf(" -h: Print usage and exit\n");
    printf(" -s: \"Screensaver\" mode, exits on first keystroke\n");
    printf(" -V: Print version information and exit\n");
    printf(" -M [message]: Prints your message in the center of the screen.\n");
    printf(" -F fps: Frame rate 12–60 (default 24)\n");
    printf(" -T color: Matrix tail (default green)\n");
    printf(" -H color: Drop head (default mint)\n");
    printf(" -O color: -M message color (default red)\n");
    printf(" -k: Characters change while scrolling\n");
    printf(
        " Colors for -T, -H, and -O: use a name (string) or hex (#RRGGBB, leading #).\n");
}

/* ---------------------------------------------------------------------------
 * Print version and build info (-V): version string, compile time/date,
 * contact and project URL.
 * --------------------------------------------------------------------------- */
void version(void) {
    printf(" CMatrix version %s (compiled %s, %s)\n",
        VERSION, __TIME__, __DATE__);
    printf("Rain: Matrix PUA U+E000-U+E067 (104 glyphs; grid idx 1962-2065). Install the patched DejaVu via `make install` (Linux: ~/.local/share/fonts/DejaVuSansMono.ttf; macOS: ~/Library/Fonts/DejaVuSansMono.ttf), then set the terminal profile to DejaVu Sans Mono.\n");
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
