/*
    cmatrix.h

    Copyright (C) 1999-2017 Chris Allegretta
    Copyright (C) 2017-Present Abishek V Ashok
    This version (fork) by shr00mie.

    Shared types, globals, and declarations for cmatrix.
*/

#ifndef CMATRIX_H
#define CMATRIX_H

#include <stddef.h>
#include <signal.h>
#include <stdbool.h>

struct notcurses;

#ifndef EXCLUDE_CONFIG_H
#include "config.h"
#endif

/* ---------------------------------------------------------------------------
 * Matrix cell type: one screen cell in the rain grid.
 * val: -1 = empty (not drawn), ' ' = space in stream, or Unicode/codepoint.
 * is_head: true for the leading character of a column (drawn bright/white).
 * --------------------------------------------------------------------------- */
typedef struct cmatrix {
    int val;
    bool is_head;
    unsigned char sweegie_fade; /* 0..255 fade amount applied by sweegie */
    unsigned char sweegie_base_color; /* COLOR_* captured on first sweegie contact */
    unsigned char sweegie_base_bold;  /* 0/1 captured on first sweegie contact */
} cmatrix;

/* ---------------------------------------------------------------------------
 * Globals (defined in cmatrix.c). Used by main loop and util.c.
 * console/lock = mode flags; matrix/length/spaces/updates = per-column
 * state; signal_status = caught signal for main loop to handle.
 * --------------------------------------------------------------------------- */
extern int console;
extern int lock;
extern cmatrix **matrix;
extern int *length;
extern int *spaces;
extern int *updates;
extern volatile sig_atomic_t signal_status;

/* Notcurses context (set in main); util.c calls stop on exit. */
extern struct notcurses *cmatrix_nc;

/* ---------------------------------------------------------------------------
 * Utilities (implemented in util.c): printf-style system(), clean exit,
 * fatal error, usage/version, and non-NULL malloc wrapper.
 * --------------------------------------------------------------------------- */
int va_system(char *str, ...);
void finish(void);
void c_die(char *msg, ...);
void usage(void);
void version(void);
void *nmalloc(size_t howmuch);

/* ---------------------------------------------------------------------------
 * Matrix/screen state and signals (implemented in cmatrix.c).
 * var_init = (re)allocate matrix and per-column arrays; sighandler = set
 * signal_status; resize_screen = read terminal size and reinit.
 * --------------------------------------------------------------------------- */
void var_init(void);
void sighandler(int s);
void resize_screen(void);

/* Restore terminal font when exiting (called from finish/c_die). No-op if font was not switched. */
void cmatrix_restore_terminal_font(void);
void cmatrix_notcurses_stop(void);

/* Truecolor names + #RRGGBB (see usage / cmatrix_print_named_color_legend). */
void cmatrix_print_named_color_legend(void);

#endif /* CMATRIX_H */
