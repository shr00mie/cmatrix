/*
    cmatrix.c

    Copyright (C) 1999-2017 Chris Allegretta
    Copyright (C) 2017-Present Abishek V Ashok
    This version (fork) by shr00mie.

    This file is part of cmatrix.

    cmatrix is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cmatrix is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cmatrix. If not, see <http://www.gnu.org/licenses/>.

*/

/* clock_nanosleep, strcasecmp, etc. on glibc */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>

#include <notcurses/notcurses.h>

#ifndef EXCLUDE_CONFIG_H
#include "config.h"
#endif

#include "cmatrix.h"
#include "matrix_rain_glyphs.h"

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

/* Legacy ncurses color indices (0–7) for matrix cell sweegie_base_color and -T/-H/-O. */
#ifndef COLOR_BLACK
#define COLOR_BLACK     0
#define COLOR_RED       1
#define COLOR_GREEN     2
#define COLOR_YELLOW    3
#define COLOR_BLUE      4
#define COLOR_MAGENTA   5
#define COLOR_CYAN      6
#define COLOR_WHITE     7
#endif
/* Sweegie / CLI: RGB from hex or non-legacy name (not a small palette index). */
#define COLOR_HEAD_RGB     8
#define COLOR_CUSTOM_TAIL  9
#define COLOR_CUSTOM_MSG   10

typedef struct {
    const char *name;
    uint32_t rgb;
    int legacy; /* COLOR_BLACK..COLOR_WHITE or -1 */
} cmatrix_named_color_t;

/* Help legend order: hue wheel (red → yellow → green → cyan/blue → magenta/pink).
 * Name lookup is a linear scan; order does not affect -T/-H/-O parsing. */
static const cmatrix_named_color_t cmatrix_named_colors[] = {
    { "maroon",     0x800000u, -1 },
    { "red",        0xff0000u, COLOR_RED },
    { "coral",      0xff7f50u, -1 },
    { "orange",     0xffa500u, -1 },
    { "gold",       0xffd700u, -1 },
    { "yellow",     0xffff00u, COLOR_YELLOW },
    { "chartreuse", 0x7fff00u, -1 },
    /* Single canonical trail green (Matrix #00FF41 darkened 3 stops; matches default rain). */
    { "green",      0x008220u, COLOR_GREEN },
    { "mint",       0xc3ffbfu, -1 },
    { "olive",      0x808000u, -1 },
    { "wheat",      0xf5deb3u, -1 },
    { "turquoise",  0x40e0d0u, -1 },
    { "cyan",       0x00ffffu, COLOR_CYAN },
    { "teal",       0x008080u, -1 },
    { "steelblue",  0x4682b4u, -1 },
    { "blue",       0x0000ffu, COLOR_BLUE },
    { "indigo",     0x4b0082u, -1 },
    { "lavender",   0xe6e6fau, -1 },
    { "magenta",    0xff00ffu, COLOR_MAGENTA },
    { "pink",       0xda70d6u, -1 },
};

#define CMATRIX_NAMED_COLOR_COUNT \
    (sizeof(cmatrix_named_colors) / sizeof(cmatrix_named_colors[0]))

/* Truecolor: leading '#' required (#RRGGBB). */
static int parse_rgb_hex_optarg(const char *s, uint32_t *out_rgb) {
    const char *p = s;
    unsigned val;
    int i;
    if (!s || s[0] != '#')
        return 0;
    p = s + 1;
    if (strlen(p) != 6u)
        return 0;
    val = 0;
    for (i = 0; i < 6; i++) {
        char c = p[i];
        unsigned d;
        if (c >= '0' && c <= '9')
            d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = 10u + (unsigned)(c - 'a');
        else if (c >= 'A' && c <= 'F')
            d = 10u + (unsigned)(c - 'A');
        else
            return 0;
        val = (val << 4) | d;
    }
    *out_rgb = val;
    return 1;
}

/* Return 0 on success: *rgb_out and *legacy_out (-1 = use CUSTOM / HEAD_RGB). */
static int cmatrix_parse_color_optarg(const char *optarg, uint32_t *rgb_out,
    int *legacy_out) {
    size_t i;
    if (parse_rgb_hex_optarg(optarg, rgb_out)) {
        *legacy_out = -1;
        return 0;
    }
    for (i = 0; i < CMATRIX_NAMED_COLOR_COUNT; i++) {
        if (!strcasecmp(optarg, cmatrix_named_colors[i].name)) {
            *rgb_out = cmatrix_named_colors[i].rgb;
            *legacy_out = cmatrix_named_colors[i].legacy;
            return 0;
        }
    }
    return -1;
}

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <termios.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* macOS has no clock_nanosleep(3); Linux/glibc does (with _DEFAULT_SOURCE). */
static void cmatrix_sleep_until_monotonic(const struct timespec *target)
{
#if defined(__APPLE__)
    struct timespec now, sleepfor;

    for (;;) {
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
            return;
        if (now.tv_sec > target->tv_sec
            || (now.tv_sec == target->tv_sec && now.tv_nsec >= target->tv_nsec))
            return;
        sleepfor.tv_sec = target->tv_sec - now.tv_sec;
        sleepfor.tv_nsec = target->tv_nsec - now.tv_nsec;
        if (sleepfor.tv_nsec < 0) {
            sleepfor.tv_nsec += 1000000000L;
            sleepfor.tv_sec--;
        }
        (void)nanosleep(&sleepfor, NULL);
    }
#else
    (void)clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, target, NULL);
#endif
}

/* ---------------------------------------------------------------------------
 * Terminal font switch: set GNOME Terminal profile font to DejaVu Sans Mono
 * (merged PUA rain glyphs) at start and restore on exit. Uses gsettings only.
 * --------------------------------------------------------------------------- */
#define GSETTINGS_PROFILE_KEY "org.gnome.Terminal.Legacy.Profile:/org/gnome/terminal/legacy/profiles/:"
#define MATRIX_FONT_NAME "DejaVu Sans Mono 12"
#define SAVED_FONT_MAX 256
#define PROFILE_UUID_MAX 64
static char saved_profile_uuid[PROFILE_UUID_MAX];
static char saved_font[SAVED_FONT_MAX];
static char saved_use_sys_font[32];
static int did_switch_font;

/* Strip surrounding single quotes and newline from gsettings get output. */
static void strip_gsettings_value(char *buf, size_t size) {
    size_t len;

    if (size == 0u)
        return;
    len = strnlen(buf, size - 1u);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
    if (len >= 2 && buf[0] == '\'' && buf[len - 1] == '\'') {
        memmove(buf, buf + 1, len - 2);
        buf[len - 2] = '\0';
    }
}

static void try_font_switch_on(void) {
    FILE *fp;
    char key[320];
    char cmd[512];

    fp = popen("gsettings get org.gnome.Terminal.ProfilesList default 2>/dev/null", "r");
    if (!fp) return;
    if (!fgets(saved_profile_uuid, (int)sizeof(saved_profile_uuid), fp)) {
        pclose(fp);
        return;
    }
    pclose(fp);
    strip_gsettings_value(saved_profile_uuid, sizeof(saved_profile_uuid));
    if (strlen(saved_profile_uuid) < 10) return;

    (void) snprintf(key, sizeof(key), "%s%s/", GSETTINGS_PROFILE_KEY, saved_profile_uuid);

    (void) snprintf(cmd, sizeof(cmd), "gsettings get \"%s\" font 2>/dev/null", key);
    fp = popen(cmd, "r");
    if (!fp) return;
    if (!fgets(saved_font, (int)sizeof(saved_font), fp)) {
        pclose(fp);
        return;
    }
    pclose(fp);
    strip_gsettings_value(saved_font, sizeof(saved_font));

    (void) snprintf(cmd, sizeof(cmd), "gsettings get \"%s\" use-system-font 2>/dev/null", key);
    fp = popen(cmd, "r");
    if (!fp) return;
    if (!fgets(saved_use_sys_font, (int)sizeof(saved_use_sys_font), fp)) {
        pclose(fp);
        return;
    }
    pclose(fp);
    strip_gsettings_value(saved_use_sys_font, sizeof(saved_use_sys_font));

    (void) snprintf(cmd, sizeof(cmd), "gsettings set \"%s\" use-system-font false 2>/dev/null", key);
    (void) system(cmd);
    (void) snprintf(cmd, sizeof(cmd), "gsettings set \"%s\" font \"%s\" 2>/dev/null", key, MATRIX_FONT_NAME);
    (void) system(cmd);
    did_switch_font = 1;
}

void cmatrix_restore_terminal_font(void) {
    if (!did_switch_font) return;
    did_switch_font = 0;
    if (saved_profile_uuid[0]) {
        char key[320];
        char cmd[680];
        (void) snprintf(key, sizeof(key), "%s%s/", GSETTINGS_PROFILE_KEY, saved_profile_uuid);
        (void) snprintf(cmd, sizeof(cmd), "gsettings set \"%s\" font \"%s\" 2>/dev/null", key, saved_font);
        (void) system(cmd);
        (void) snprintf(cmd, sizeof(cmd), "gsettings set \"%s\" use-system-font %s 2>/dev/null", key, saved_use_sys_font);
        (void) system(cmd);
        saved_profile_uuid[0] = '\0';
    }
}

/* ---------------------------------------------------------------------------
 * Global variables (declared in cmatrix.h). Mode flag (lock),
 * the 2D matrix grid, and per-column state: length/spaces/updates,
 * column_active (which columns are raining), column_clear_buffer (sliding
 * clear zone). signal_status is set by sighandler for the main loop.
 * --------------------------------------------------------------------------- */
int lock = 0;
cmatrix **matrix = (cmatrix **) NULL;
int *length = NULL;  /* Length of cols in each line */
int *spaces = NULL;  /* Spaces left to fill */
int *updates = NULL; /* Per-column update throttle: column j advances when count > updates[j] (or when asynch is off or clear buffer active). */
/* Which columns have rain: 1 = active, 0 = locked. Up to max_col_drops (~66% of
 * width) after the startup ramp; when rain hits the bottom row we lock that column
 * and unlock another. */
static unsigned char *column_active = NULL;
/* Per-column clear buffer size (3..length/2); slide this many rows ahead of the drop through old tail */
static int *column_clear_buffer = NULL;
/* Timer-gated sequential drop spawning state */
static int active_col_count = 0;
static int max_col_drops = 0;
static int spawn_timer_enabled = 0;
static double spawn_timer_remaining_sec = 3.00; /* two decimal points */
static struct timespec spawn_timer_start_ts;
static struct timespec spawn_timer_last_tick_ts;
static const double spawn_interval_initial_sec = 3.00; /* initial countdown */
static const double spawn_timer_disable_after_sec = 15.0; /* ~15 seconds total */
/* Per-column async speed (rows/sec) and accumulator (rows). */
static float *col_speed_rps = NULL;
static float *col_row_accum = NULL;
/* Screen dimensions (used instead of ncurses LINES/COLS for portability) */
static int screen_lines = 24;
static int screen_cols = 80;
volatile sig_atomic_t signal_status = 0; /* Indicates a caught signal */

struct notcurses *cmatrix_nc = NULL;
static struct ncplane *cmatrix_plane = NULL;
#if defined(__APPLE__)
static FILE *cmatrix_notcurses_outfp;
#endif

/* Snapshot of matrix at frame start: workers read neighbors from here, write live matrix. */
static cmatrix *matrix_snap = NULL;

#define CMATRIX_MAX_WORKERS 8
static int cmatrix_num_workers = 1;
static pthread_t cmatrix_worker_threads[CMATRIX_MAX_WORKERS];
static pthread_mutex_t cmatrix_mtx_start[CMATRIX_MAX_WORKERS];
static pthread_cond_t cmatrix_cv_start[CMATRIX_MAX_WORKERS];
static int cmatrix_start_pulse[CMATRIX_MAX_WORKERS];
static pthread_mutex_t cmatrix_mtx_done[CMATRIX_MAX_WORKERS];
static pthread_cond_t cmatrix_cv_done[CMATRIX_MAX_WORKERS];
static int cmatrix_done_pulse[CMATRIX_MAX_WORKERS];
static volatile int cmatrix_workers_running = 0;
static pthread_mutex_t cmatrix_spawn_mx = PTHREAD_MUTEX_INITIALIZER;

struct cmatrix_worker_arg {
    int thread_id;
    int num_workers;
    unsigned int rng;
};

static struct cmatrix_worker_arg cmatrix_worker_args[CMATRIX_MAX_WORKERS];

/* Per-character -M message state: visible[] and wasOnMessage[] (sized to message length).
 * Spaces in the message are not treated as message characters (no reveal tracking, rain passes through). */
#define MSG_STATE_MAX 512
static unsigned char msg_visible[MSG_STATE_MAX];
static unsigned char msg_was_on_message[MSG_STATE_MAX];
/* Dirty tracking: previous-frame buffer; only changed cells go to ncplane. */
typedef struct {
    int ch;           /* codepoint or ASCII */
    uint32_t fg_rgb;  /* 0x00RRGGBB foreground */
    unsigned char bold;
} rendered_cell;
static rendered_cell *prev_cell = NULL;
/* After var_init/resize, draw every cell once (keyframe). Then skip columns whose
 * simulation did not run and which do not carry -M message (inter-frame skip). */
static int full_redraw_pending = 1;
/* -M respawn bias: until the whole (non-space) message is revealed, if 2 new drops did not start in
 * unrevealed columns, the 3rd must start in one (if any). */
static int msg_spawns_since_unrevealed = 0;
/* Avoid spawning consecutive drops in the same column (aesthetics). */
static int last_spawn_col = -1;

static unsigned int cmatrix_main_rng = 1;

static void cmatrix_stop_workers(void);

void cmatrix_notcurses_stop(void) {
    cmatrix_stop_workers();
    if (cmatrix_nc) {
        notcurses_stop(cmatrix_nc);
        cmatrix_nc = NULL;
        cmatrix_plane = NULL;
    }
#if defined(__APPLE__)
    if (cmatrix_notcurses_outfp) {
        fclose(cmatrix_notcurses_outfp);
        cmatrix_notcurses_outfp = NULL;
    }
#endif
}

#if defined(__APPLE__) && defined(HAVE_SYS_IOCTL_H)
/* Terminal.app: notcurses sometimes leaves the standard plane at 1 row while
 * TIOCGWINSZ on stdout reports the real size; ncplane_resize() cannot resize the
 * stdplane. Build a separate full-screen pile from kernel geometry and draw there.
 * Set CMATRIX_NO_EXTRA_PILE to disable. */
static void cmatrix_try_macos_fullscreen_pile(void)
{
    struct winsize ws;
    ncplane_options popts;
    struct ncplane *p;

    if (getenv("CMATRIX_NO_EXTRA_PILE") != NULL)
        return;
    if (cmatrix_nc == NULL)
        return;
    memset(&ws, 0, sizeof(ws));
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0)
        return;
    if (ws.ws_row < 1 || ws.ws_col < 1)
        return;
    memset(&popts, 0, sizeof(popts));
    popts.y = 0;
    popts.x = 0;
    popts.rows = (unsigned)ws.ws_row;
    popts.cols = (unsigned)ws.ws_col;
    popts.name = "cmatrix";
    p = ncpile_create(cmatrix_nc, &popts);
    if (p != NULL)
        cmatrix_plane = p;
}

/* ncpile_render() always runs notcurses_resize_internal(), which reapplies
 * TIOCGWINSZ to THIS pile. If the library's tty fd reports 1 row but stdout's
 * ioctl matches the real terminal, the aux pile shrinks to 1 — undo from stdout. */
static int cmatrix_macos_fix_aux_pile_after_resize_internal(void)
{
    struct winsize ws;
    unsigned py, px;

    if (cmatrix_plane == NULL || cmatrix_nc == NULL)
        return 0;
    if (cmatrix_plane == notcurses_stdplane(cmatrix_nc))
        return 0;
    memset(&ws, 0, sizeof(ws));
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_row < 1 || ws.ws_col < 1)
        return 0;
    ncplane_dim_yx(cmatrix_plane, &py, &px);
    if ((unsigned)ws.ws_row == py && (unsigned)ws.ws_col == px)
        return 0;
    (void)ncplane_resize_simple(cmatrix_plane, (unsigned)ws.ws_row, (unsigned)ws.ws_col);
    return 1;
}
#endif

/* Render + rasterize the pile that owns cmatrix_plane (std pile or macOS aux pile). */
static void cmatrix_present_frame(void)
{
    if (cmatrix_plane == NULL || cmatrix_nc == NULL)
        return;
    if (ncpile_render(cmatrix_plane) != 0)
        return;
#if defined(__APPLE__) && defined(HAVE_SYS_IOCTL_H)
    if (cmatrix_macos_fix_aux_pile_after_resize_internal()) {
        if (ncpile_render(cmatrix_plane) != 0)
            return;
    }
#endif
    (void)ncpile_rasterize(cmatrix_plane);
}

static uint32_t rgb_lerp_to_black(uint32_t fg, unsigned char fade_amt);
static uint32_t cmatrix_default_tail_green_rgb(void);

/* Standard palette -> RGB (default tail green: classic Matrix #00FF41 darkened 3 stops). */
static uint32_t cmatrix_palette_rgb(int color_idx) {
    switch (color_idx) {
    case COLOR_BLACK:   return 0x000000u;
    case COLOR_RED:     return 0xff0000u;
    case COLOR_GREEN:   return cmatrix_default_tail_green_rgb();
    case COLOR_YELLOW:  return 0xffff00u;
    case COLOR_BLUE:    return 0x0000ffu;
    case COLOR_MAGENTA: return 0xff00ffu;
    case COLOR_CYAN:    return 0x00ffffu;
    case COLOR_WHITE:   return 0xffffffu;
    case COLOR_HEAD_RGB:
        return 0xffffffu; /* rgb_head set separately when this sentinel is used */
    case COLOR_CUSTOM_TAIL:
    case COLOR_CUSTOM_MSG:
        return 0x808080u; /* rgb_tail / rgb_msg hold real values */
    default:            return cmatrix_default_tail_green_rgb();
    }
}

static uint32_t rgb_lerp_to_black(uint32_t fg, unsigned char fade_amt) {
    unsigned fr = (unsigned)((fg >> 16) & 0xffu);
    unsigned fg_g = (unsigned)((fg >> 8) & 0xffu);
    unsigned fb = (unsigned)(fg & 0xffu);
    unsigned t = 255u - (unsigned)fade_amt;
    fr = (fr * t) / 255u;
    fg_g = (fg_g * t) / 255u;
    fb = (fb * t) / 255u;
    return ((uint32_t)fr << 16) | ((uint32_t)fg_g << 8) | (uint32_t)fb;
}

/* Classic Matrix #00FF41, darkened by three −1-stop lerps (same fade step as tail glyph ramp). */
static uint32_t cmatrix_default_tail_green_rgb(void) {
    uint32_t g = 0x00ff41u;
    g = rgb_lerp_to_black(g, 51);
    g = rgb_lerp_to_black(g, 51);
    g = rgb_lerp_to_black(g, 51);
    return g;
}

/* Lerp fg toward white; amt 0 = fg, 255 = white. */
static uint32_t rgb_lerp_to_white(uint32_t fg, unsigned char amt) {
    unsigned fr = (unsigned)((fg >> 16) & 0xffu);
    unsigned fg_g = (unsigned)((fg >> 8) & 0xffu);
    unsigned fb = (unsigned)(fg & 0xffu);
    unsigned t = (unsigned)amt;
    fr = fr + (((255u - fr) * t) / 255u);
    fg_g = fg_g + (((255u - fg_g) * t) / 255u);
    fb = fb + (((255u - fb) * t) / 255u);
    return ((uint32_t)fr << 16) | ((uint32_t)fg_g << 8) | (uint32_t)fb;
}

/* Tail glyph color: five stops relative to active tail RGB (−4..+4, even). */
static uint32_t rgb_tail_at_stop(uint32_t base, int stop) {
    switch (stop) {
    case -4:
        return rgb_lerp_to_black(base, 102);
    case -2:
        return rgb_lerp_to_black(base, 51);
    case 0:
        return base;
    case 2:
        return rgb_lerp_to_white(base, 51);
    case 4:
        return rgb_lerp_to_white(base, 102);
    default:
        return base;
    }
}

static const int tail_glyph_stop_table[5] = { -4, -2, 0, 2, 4 };

/* Encode BMP codepoint (0..0xFFFF) to UTF-8 for ncplane_putegc (UTF-8 locale). */
static int codepoint_to_utf8(unsigned int u, unsigned char *buf) {
    if (u <= 0x7F) {
        buf[0] = (unsigned char)u;
        return 1;
    }
    if (u <= 0x7FF) {
        buf[0] = (unsigned char)(0xC0 | (u >> 6));
        buf[1] = (unsigned char)(0x80 | (u & 0x3F));
        return 2;
    }
    if (u <= 0xFFFF) {
        buf[0] = (unsigned char)(0xE0 | (u >> 12));
        buf[1] = (unsigned char)(0x80 | ((u >> 6) & 0x3F));
        buf[2] = (unsigned char)(0x80 | (u & 0x3F));
        return 3;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Non-message (rain): Matrix glyph locations (PUA) from patched font.
 * --------------------------------------------------------------------------- */
#define MATRIX_FIRST ((int)matrix_rain_codepoints[0])
#define MATRIX_LAST  ((int)matrix_rain_codepoints[MATRIX_RAIN_GLYPH_COUNT - 1])

/* ---------------------------------------------------------------------------
 * Return a random character for matrix[row][col] that is not equal to any
 * value in the 8 adjacent cells (vertical, horizontal, diagonal). Retries
 * up to 50 times; then returns a random character anyway to avoid infinite loop.
 * --------------------------------------------------------------------------- */
static int random_char_avoiding_neighbors(int row, int col, unsigned int *rng, cmatrix *src) {
    int adj[8], n_adj = 0;
    int dr[] = { -1, -1, -1,  0, 0,  1, 1, 1 };
    int dc[] = { -1,  0,  1, -1, 1, -1, 0, 1 };
    int d;
    if (src == NULL)
        src = matrix[0];
    for (d = 0; d < 8; d++) {
        int r = row + dr[d], c = col + dc[d];
        if (r >= 0 && r <= screen_lines + 1 && c >= 0 && c < screen_cols && src != NULL)
            adj[n_adj++] = src[r * screen_cols + c].val;
    }
    for (d = 0; d < 50; d++) {
        int val = (int)matrix_rain_codepoints[(unsigned)rand_r(rng) % (unsigned)MATRIX_RAIN_GLYPH_COUNT];
        int ok = 1;
        int a;
        for (a = 0; a < n_adj; a++) {
            int adjv = adj[a];
            if (adjv == val) {
                ok = 0;
                break;
            }
        }
        if (ok)
            return val;
    }
    return (int)matrix_rain_codepoints[(unsigned)rand_r(rng) % (unsigned)MATRIX_RAIN_GLYPH_COUNT];
}

static unsigned char sweegie_fade_for_distance(int window_len, int dist_from_head) {
    /* dist_from_head: 2..window_len (dist 1 is deleted, not faded).
     * Fade spans window_len-1 rows: leading edge gets smallest fade step,
     * row nearest head gets strongest fade step. */
    int fade_rows;
    int progress; /* 1..fade_rows */
    int fade;
    if (window_len <= 2)
        return 0;
    fade_rows = window_len - 1;
    progress = window_len - dist_from_head + 1;
    if (progress < 1)
        progress = 1;
    if (progress > fade_rows)
        progress = fade_rows;
    fade = (progress * 255) / fade_rows;
    if (fade < 0)
        fade = 0;
    if (fade > 255)
        fade = 255;
    return (unsigned char) fade;
}

static int rand_inclusive(int lo, int hi, unsigned int *rng) {
    if (hi < lo)
        return lo;
    return lo + (int)(rand_r(rng) % (unsigned)(hi - lo + 1));
}

static int column_has_previous_tail(int col) {
    int r;
    if (matrix == NULL || col < 0 || col >= screen_cols)
        return 0;
    for (r = 0; r <= screen_lines + 1; r++) {
        int v = matrix[r][col].val;
        if (v != -1 && v != ' ')
            return 1;
    }
    return 0;
}

/* Available rows for a newly initialized drop (row 1 .. bottom, inclusive). */
static int available_rows_for_new_drop(void) {
    if (screen_lines < 1)
        return 1;
    return screen_lines;
}

/* Spec: tail length in [1/4 * available, 3/4 * available]. */
static int spec_tail_length_for_clear_column(void) {
    int avail = available_rows_for_new_drop();
    int lo = avail / 4;
    int hi = (avail * 3) / 4;
    if (lo < 3)
        lo = 3;
    if (hi < lo)
        hi = lo;
    return rand_inclusive(lo, hi, &cmatrix_main_rng);
}

/* Spec (updated): sweegie in [3, 1/2 * available] when previous tail exists. */
static int spec_sweegie_length_for_tailed_column(void) {
    int avail = available_rows_for_new_drop();
    int hi = avail / 2;
    if (hi < 3)
        hi = 3;
    return rand_inclusive(3, hi, &cmatrix_main_rng);
}

static void spec_prepare_spawn_in_column(int col) {
    int had_tail = column_has_previous_tail(col);
    if (col < 0 || col >= screen_cols)
        return;
    spaces[col] = (int)rand_r(&cmatrix_main_rng) % screen_lines + 1;
    length[col] = spec_tail_length_for_clear_column();
    /* Per-column async speed (rows/sec), randomized on spawn. */
    if (col_speed_rps != NULL && col_row_accum != NULL) {
        int whole = rand_inclusive(10, 23, &cmatrix_main_rng);
        int frac = rand_r(&cmatrix_main_rng) % 100;
        col_speed_rps[col] = (float)whole + (float)frac / 100.0f;
        col_row_accum[col] = 0.0f;
    }

    if (column_clear_buffer != NULL) {
        if (had_tail) {
            column_clear_buffer[col] = spec_sweegie_length_for_tailed_column();
        } else {
            column_clear_buffer[col] = 0;
        }
    }

    /* Spec: new drop starts in newly unlocked column immediately. */
    matrix[1][col].val = random_char_avoiding_neighbors(1, col, &cmatrix_main_rng, matrix[0]);
    matrix[1][col].is_head = true;
    matrix[1][col].sweegie_fade = 0;
    matrix[1][col].sweegie_base_color = (unsigned char)COLOR_GREEN;
    matrix[1][col].sweegie_base_bold = 0;
}

static void unlock_one_column_and_spawn(const char *msg, int msg_len, int msg_y) {
    int nlocked = 0, nunrevealed = 0, eligible_locked = 0, eligible_unrevealed = 0, k, r;
    if (column_active == NULL)
        return;
    if (max_col_drops > 0 && active_col_count >= max_col_drops)
        return;
    for (k = 0; k < screen_cols; k++) {
        if (!column_active[k]) {
            nlocked++;
            if (msg_len > 0 && k >= msg_y && k < msg_y + msg_len) {
                int mk = k - msg_y;
                if (mk < MSG_STATE_MAX && (unsigned char)msg[mk] != ' ' && !msg_visible[mk])
                    nunrevealed++;
            }
        }
    }
    if (nlocked <= 0)
        return;

    /* If possible, avoid spawning in the same column as last time. */
    eligible_locked = nlocked;
    if (last_spawn_col >= 0 && last_spawn_col < screen_cols &&
        !column_active[last_spawn_col] && eligible_locked > 1) {
        eligible_locked--;
    }
    if (eligible_locked <= 0)
        eligible_locked = nlocked;

    if (msg_len > 0 && nunrevealed > 0) {
        eligible_unrevealed = nunrevealed;
        if (last_spawn_col >= msg_y && last_spawn_col < msg_y + msg_len &&
            last_spawn_col >= 0 && last_spawn_col < screen_cols &&
            !column_active[last_spawn_col]) {
            int mk = last_spawn_col - msg_y;
            if (mk >= 0 && mk < MSG_STATE_MAX &&
                (unsigned char)msg[mk] != ' ' && !msg_visible[mk] && eligible_unrevealed > 1) {
                eligible_unrevealed--;
            }
        }
        if (eligible_unrevealed <= 0)
            eligible_unrevealed = nunrevealed;
    }

    /* Preserve existing -M bias behavior; runtime-option parsing untouched. */
    if (msg_len > 0 && nunrevealed > 0 && msg_spawns_since_unrevealed >= 2) {
        r = (int)rand_r(&cmatrix_main_rng) % eligible_unrevealed;
        for (k = 0; k < screen_cols; k++) {
            if (!column_active[k] && k >= msg_y && k < msg_y + msg_len) {
                int mk = k - msg_y;
                if (mk < MSG_STATE_MAX && (unsigned char)msg[mk] != ' ' && !msg_visible[mk]) {
                    if (eligible_unrevealed < nunrevealed && k == last_spawn_col)
                        continue;
                    if (r == 0)
                        break;
                    r--;
                }
            }
        }
        msg_spawns_since_unrevealed = 0;
    } else {
        r = (int)rand_r(&cmatrix_main_rng) % eligible_locked;
        for (k = 0; k < screen_cols; k++) {
            if (!column_active[k]) {
                if (eligible_locked < nlocked && k == last_spawn_col)
                    continue;
                if (r == 0)
                    break;
                r--;
            }
        }
        if (msg_len > 0 && k >= msg_y && k < msg_y + msg_len) {
            int mk = k - msg_y;
            if (mk < MSG_STATE_MAX && (unsigned char)msg[mk] != ' ' && !msg_visible[mk])
                msg_spawns_since_unrevealed = 0;
            else
                msg_spawns_since_unrevealed++;
        } else {
            msg_spawns_since_unrevealed++;
        }
    }
    if (k >= 0 && k < screen_cols) {
        column_active[k] = 1;
        spec_prepare_spawn_in_column(k);
        active_col_count++;
        last_spawn_col = k;
    }
}

/* ---------------------------------------------------------------------------
 * Compute what would be drawn at (i, j) without side effects. Used for
 * dirty tracking: compare return value to prev_cell to skip unchanged cells.
 * --------------------------------------------------------------------------- */
static rendered_cell get_rendered_cell(int i, int j, const char *msg, int msg_len, int msg_row, int msg_y,
    int bold, int head_color, int tail_color, int message_color, int use_ascii_fallback,
    uint32_t rgb_head, uint32_t rgb_tail, uint32_t rgb_msg)
{
    rendered_cell r = { .ch = ' ', .fg_rgb = rgb_tail, .bold = 0 };
    int mk = -1;
    int is_msg_cell = 0;
    (void)head_color;
    (void)tail_color;
    (void)message_color;
    (void)use_ascii_fallback;
    if (msg_len > 0 && i == msg_row && j >= msg_y && j < msg_y + msg_len) {
        mk = j - msg_y;
        if (mk >= 0 && mk < msg_len && mk < MSG_STATE_MAX && msg[mk] != ' ')
            is_msg_cell = 1;
    }
    if (!is_msg_cell && (matrix == NULL || matrix[0] == NULL || i < 0 || i > screen_lines || j < 0 || j >= screen_cols))
        return r;
    {
        int idx = i * screen_cols + j;
        int max_cell = (screen_lines + 2) * screen_cols - 1;
        if (!is_msg_cell && (idx < 0 || idx > max_cell))
            return r;
        cmatrix cell = matrix[0][idx];
        if (is_msg_cell && mk >= 0 && mk < MSG_STATE_MAX) {
            if (cell.is_head) {
                r.ch = (unsigned char)msg[mk];
                r.fg_rgb = rgb_head;
                r.bold = 1;
                return r;
            }
            if (msg_was_on_message[mk]) {
                r.ch = (unsigned char)msg[mk];
                r.fg_rgb = rgb_msg;
                r.bold = 1;
                return r;
            }
            if (msg_visible[mk]) {
                r.ch = (unsigned char)msg[mk];
                r.fg_rgb = rgb_msg;
                r.bold = 1;
                return r;
            }
            /* unrevealed: fall through to matrix cell */
        }
        /* Matrix cell (head or trail, or unrevealed message cell) */
        if (cell.val == -1) {
            r.ch = ' ';
            r.fg_rgb = cell.is_head ? rgb_head : rgb_tail;
            r.bold = cell.is_head ? (bold ? 1 : 0) : 0;
            return r;
        }
        {
            int v = cell.val;
            if (v >= MATRIX_FIRST && v <= MATRIX_LAST)
                r.ch = v;
            else if (v > 127)
                r.ch = '#';
            else
                r.ch = v;
            if (!cell.is_head && cell.sweegie_fade > 0) {
                int base_color;
                if ((int)cell.sweegie_base_color == COLOR_HEAD_RGB)
                    base_color = COLOR_HEAD_RGB;
                else if (cell.sweegie_base_color <= COLOR_WHITE)
                    base_color = (int)cell.sweegie_base_color;
                else
                    base_color = tail_color;
                if (base_color == COLOR_GREEN || base_color == COLOR_CUSTOM_TAIL)
                    r.fg_rgb = rgb_lerp_to_black(rgb_tail, cell.sweegie_fade);
                else if (base_color == COLOR_HEAD_RGB)
                    r.fg_rgb = rgb_lerp_to_black(rgb_head, cell.sweegie_fade);
                else if (cell.sweegie_fade >= 200)
                    r.fg_rgb = 0x000000u;
                else
                    r.fg_rgb = rgb_lerp_to_black(cmatrix_palette_rgb(base_color), cell.sweegie_fade);
                r.bold = 0;
            } else {
                if (cell.is_head) {
                    r.fg_rgb = rgb_head;
                    r.bold = bold ? 1 : 0;
                } else {
                    int tstop = tail_glyph_stop_table[(unsigned)v % 5u];
                    r.fg_rgb = rgb_tail_at_stop(rgb_tail, tstop);
                    r.bold = (bold && (v % 2 == 0)) ? 1 : 0;
                }
            }
            return r;
        }
    }
}

/* Draw a single cell on the standard plane (truecolor fg + optional bold). */
static void draw_cell_at(struct ncplane *n, int line, int col, const rendered_cell *r, int use_ascii_fallback)
{
    unsigned tr = (unsigned)((r->fg_rgb >> 16) & 0xffu);
    unsigned tg = (unsigned)((r->fg_rgb >> 8) & 0xffu);
    unsigned tb = (unsigned)(r->fg_rgb & 0xffu);

    (void)use_ascii_fallback;
    (void)ncplane_set_fg_rgb8(n, tr, tg, tb);
    if (r->bold)
        ncplane_set_styles(n, NCSTYLE_BOLD);
    else
        ncplane_set_styles(n, NCSTYLE_NONE);
    if (r->ch == ' ') {
        (void)ncplane_putegc_yx(n, line, col, " ", NULL);
    } else if (r->ch >= MATRIX_FIRST && r->ch <= MATRIX_LAST) {
        /* No fallback: always render Matrix glyph locations directly. */
        unsigned char ubuf[8];
        int nbytes = codepoint_to_utf8((unsigned int)r->ch, ubuf);
        if (nbytes > 0) {
            ubuf[nbytes] = '\0';
            (void)ncplane_putegc_yx(n, line, col, (const char *)ubuf, NULL);
        } else {
            (void)ncplane_putegc_yx(n, line, col, "?", NULL);
        }
    } else {
        char one[8];
        one[0] = (char)(r->ch > 127 ? '#' : (unsigned char)r->ch);
        one[1] = '\0';
        (void)ncplane_putegc_yx(n, line, col, one, NULL);
    }
}

/* Per-frame simulation globals (main sets before waking worker threads). */
static int gsim_pause;
static int gsim_asynch;
static int gsim_delay_ms;
static int gsim_changes;
static int gsim_head_color;
static int gsim_tail_color;
static int gsim_bold;
static const char *gsim_msg;
static int gsim_msg_len;
static int gsim_msg_row;
static int gsim_msg_y;

static void run_column_simulation(int j,
    int head_color, int tail_color, int bold,
    unsigned int *rng)
{
    int i, y, z, firstcoldone;
    if (matrix == NULL || j < 0 || j >= screen_cols)
        return;
    if (gsim_pause == 0 && column_active != NULL && column_active[j]) {
        int steps = 0;
        if (gsim_asynch == 0) {
            steps = 1;
        } else if (col_speed_rps != NULL && col_row_accum != NULL) {
            double dt = (double)gsim_delay_ms / 1000.0;
            if (dt < 0.0)
                dt = 0.0;
            col_row_accum[j] += col_speed_rps[j] * (float)dt;
            if (col_row_accum[j] >= 1.0f) {
                steps = (int)col_row_accum[j];
                if (steps > 3)
                    steps = 3;
                col_row_accum[j] -= (float)steps;
            }
        }
        while (steps-- > 0 && column_active[j]) {
            i = 0;
            y = 0;
            firstcoldone = 0;
            while (i <= screen_lines + 1) {
                if (i < 0 || i > screen_lines + 1)
                    break;
                while (i <= screen_lines + 1) {
                    int v = (i >= 0 && i <= screen_lines + 1) ? matrix[i][j].val : -1;
                    if (v != ' ' && v != -1)
                        break;
                    i++;
                }

                if (i > screen_lines + 1) {
                    break;
                }

                z = i;
                y = 0;
                while (i <= screen_lines + 1) {
                    if (i < 0 || i > screen_lines + 1)
                        break;
                    {
                        int v = matrix[i][j].val;
                        if (v == ' ' || v == -1)
                            break;
                    }
                    matrix[i][j].is_head = false;
                    if (gsim_changes) {
                        int regen;
                        if (bold)
                            regen = (rand_r(rng) % 100) < 10;
                        else
                            regen = (rand_r(rng) % 8 == 0);
                        if (regen) {
                            matrix[i][j].val = random_char_avoiding_neighbors(i, j, rng, matrix_snap);
                            matrix[i][j].sweegie_fade = 0;
                        }
                    }
                    i++;
                    y++;
                }

                if (i > screen_lines + 1) {
                    if (!firstcoldone && column_active != NULL) {
                        column_active[j] = 0;
                        if (active_col_count > 0)
                            active_col_count--;
                        pthread_mutex_lock(&cmatrix_spawn_mx);
                        unlock_one_column_and_spawn(gsim_msg, gsim_msg_len, gsim_msg_y);
                        pthread_mutex_unlock(&cmatrix_spawn_mx);
                        break;
                    }
                    continue;
                }

                if (i == screen_lines) {
                    matrix[screen_lines][j].val = random_char_avoiding_neighbors(screen_lines, j, rng, matrix_snap);
                    matrix[screen_lines][j].is_head = false;
                    matrix[screen_lines][j].sweegie_fade = 0;
                    matrix[screen_lines][j].sweegie_base_color = (unsigned char)tail_color;
                    matrix[screen_lines][j].sweegie_base_bold = 0;
                    i = screen_lines + 1;
                }

                if (i >= 0 && i <= screen_lines + 1) {
                    matrix[i][j].val = random_char_avoiding_neighbors(i, j, rng, matrix_snap);
                    matrix[i][j].is_head = true;
                    matrix[i][j].sweegie_fade = 0;
                    matrix[i][j].sweegie_base_color = (unsigned char)head_color;
                    matrix[i][j].sweegie_base_bold = (unsigned char)(bold ? 1 : 0);
                    if (column_clear_buffer != NULL && column_clear_buffer[j] > 0 && !firstcoldone) {
                        int r, cb = column_clear_buffer[j];
                        int effective_cb = (screen_lines - i) < cb ? (screen_lines - i) : cb;
                        if (effective_cb < 0)
                            effective_cb = 0;
                        for (r = 1; r <= effective_cb && i + r <= screen_lines; r++) {
                            int rr = i + r;
                            if (matrix[rr][j].val == -1 || matrix[rr][j].is_head)
                                continue;
                            if (r == 1) {
                                matrix[rr][j].val = -1;
                                matrix[rr][j].is_head = false;
                                matrix[rr][j].sweegie_fade = 0;
                                matrix[rr][j].sweegie_base_color = (unsigned char)tail_color;
                                matrix[rr][j].sweegie_base_bold = 0;
                            } else {
                                unsigned char target_fade = sweegie_fade_for_distance(effective_cb, r);
                                if (matrix[rr][j].sweegie_fade == 0) {
                                    matrix[rr][j].sweegie_base_color = (unsigned char)tail_color;
                                    matrix[rr][j].sweegie_base_bold = (unsigned char)((bold && (matrix[rr][j].val % 2 == 0)) ? 1 : 0);
                                }
                                if (matrix[rr][j].sweegie_fade < target_fade)
                                    matrix[rr][j].sweegie_fade = target_fade;
                            }
                        }
                    }
                }

                if (y > length[j] || firstcoldone) {
                    if (z >= 0 && z <= screen_lines && !firstcoldone) {
                        matrix[z][j].val = ' ';
                        matrix[z][j].sweegie_fade = 0;
                        matrix[z][j].sweegie_base_color = (unsigned char)tail_color;
                        matrix[z][j].sweegie_base_bold = 0;
                        if (i > screen_lines + 1) {
                            matrix[0][j].val = -1;
                            matrix[0][j].sweegie_fade = 0;
                            matrix[0][j].sweegie_base_color = (unsigned char)tail_color;
                            matrix[0][j].sweegie_base_bold = 0;
                        }
                    } else if (firstcoldone) {
                        matrix[0][j].val = -1;
                        matrix[0][j].sweegie_fade = 0;
                        matrix[0][j].sweegie_base_color = (unsigned char)tail_color;
                        matrix[0][j].sweegie_base_bold = 0;
                    }
                }
                firstcoldone = 1;
                i++;
            }
        }
    }
}

static void *cmatrix_worker_main(void *arg) {
    struct cmatrix_worker_arg *wa = (struct cmatrix_worker_arg *)arg;
    int j;
    int wid = wa->thread_id;

    while (1) {
        pthread_mutex_lock(&cmatrix_mtx_start[wid]);
        while (!cmatrix_start_pulse[wid] && cmatrix_workers_running)
            pthread_cond_wait(&cmatrix_cv_start[wid], &cmatrix_mtx_start[wid]);
        cmatrix_start_pulse[wid] = 0;
        pthread_mutex_unlock(&cmatrix_mtx_start[wid]);

        if (!cmatrix_workers_running) {
            pthread_mutex_lock(&cmatrix_mtx_done[wid]);
            cmatrix_done_pulse[wid] = 1;
            pthread_cond_signal(&cmatrix_cv_done[wid]);
            pthread_mutex_unlock(&cmatrix_mtx_done[wid]);
            break;
        }
        for (j = wa->thread_id; j < screen_cols; j += wa->num_workers) {
            run_column_simulation(j, gsim_head_color, gsim_tail_color, gsim_bold, &wa->rng);
        }
        pthread_mutex_lock(&cmatrix_mtx_done[wid]);
        cmatrix_done_pulse[wid] = 1;
        pthread_cond_signal(&cmatrix_cv_done[wid]);
        pthread_mutex_unlock(&cmatrix_mtx_done[wid]);
    }
    return NULL;
}

static void cmatrix_stop_workers(void) {
    int w;
    int n;

    if (cmatrix_num_workers <= 0)
        return;
    n = cmatrix_num_workers;
    cmatrix_workers_running = 0;
    for (w = 0; w < n; w++) {
        pthread_mutex_lock(&cmatrix_mtx_start[w]);
        cmatrix_start_pulse[w] = 1;
        pthread_cond_signal(&cmatrix_cv_start[w]);
        pthread_mutex_unlock(&cmatrix_mtx_start[w]);
    }
    for (w = 0; w < n; w++)
        (void)pthread_join(cmatrix_worker_threads[w], NULL);
    for (w = 0; w < n; w++) {
        pthread_mutex_destroy(&cmatrix_mtx_start[w]);
        pthread_cond_destroy(&cmatrix_cv_start[w]);
        pthread_mutex_destroy(&cmatrix_mtx_done[w]);
        pthread_cond_destroy(&cmatrix_cv_done[w]);
    }
    cmatrix_num_workers = 0;
}

static void cmatrix_start_workers(void) {
    int w;
    cmatrix_num_workers = CMATRIX_MAX_WORKERS;
    if (cmatrix_num_workers > screen_cols)
        cmatrix_num_workers = screen_cols;
    if (cmatrix_num_workers < 1)
        cmatrix_num_workers = 1;
    cmatrix_workers_running = 1;
    for (w = 0; w < cmatrix_num_workers; w++) {
        cmatrix_start_pulse[w] = 0;
        cmatrix_done_pulse[w] = 0;
        (void)pthread_mutex_init(&cmatrix_mtx_start[w], NULL);
        (void)pthread_cond_init(&cmatrix_cv_start[w], NULL);
        (void)pthread_mutex_init(&cmatrix_mtx_done[w], NULL);
        (void)pthread_cond_init(&cmatrix_cv_done[w], NULL);
        cmatrix_worker_args[w].thread_id = w;
        cmatrix_worker_args[w].num_workers = cmatrix_num_workers;
        cmatrix_worker_args[w].rng = (unsigned int)(time(NULL) ^ (unsigned int)(w * 0x9e3779b9u));
        (void)pthread_create(&cmatrix_worker_threads[w], NULL, cmatrix_worker_main, &cmatrix_worker_args[w]);
    }
}

/* ---------------------------------------------------------------------------
 * Initialize (or re-initialize) all matrix state. Frees existing buffers,
 * allocates matrix[0..screen_lines+1] (extra row holds off-screen head so the
 * bottom visible row stays trail), length/spaces/updates and
 * column_active/column_clear_buffer per column. One column starts active; the
 * spawn-delay timer in main() adds more until active_col_count reaches
 * max_col_drops (ceil(66% of screen_cols)). Fills the grid with -1 (empty).
 * Called at startup and on SIGWINCH (resize).
 * --------------------------------------------------------------------------- */
void var_init() {
    int i, j;


    if (matrix != NULL) {
        free(matrix[0]);
        free(matrix);
    }
    if (matrix_snap != NULL) {
        free(matrix_snap);
        matrix_snap = NULL;
    }

    matrix = nmalloc(sizeof(cmatrix *) * (screen_lines + 2));
    matrix[0] = nmalloc(sizeof(cmatrix) * (screen_lines + 2) * (size_t)screen_cols);
    for (i = 1; i <= screen_lines + 1; i++) {
        matrix[i] = matrix[i - 1] + screen_cols;
    }

    if (length != NULL) {
        free(length);
    }
    length = nmalloc(screen_cols * sizeof(int));

    if (spaces != NULL) {
        free(spaces);
    }
    spaces = nmalloc(screen_cols* sizeof(int));

    if (updates != NULL) {
        free(updates);
    }
    updates = nmalloc(screen_cols * sizeof(int));

    if (col_speed_rps != NULL)
        free(col_speed_rps);
    col_speed_rps = nmalloc((size_t)screen_cols * sizeof(float));
    if (col_row_accum != NULL)
        free(col_row_accum);
    col_row_accum = nmalloc((size_t)screen_cols * sizeof(float));
    for (j = 0; j < screen_cols; j++) {
        col_speed_rps[j] = 0.0f;
        col_row_accum[j] = 0.0f;
    }

    if (column_active != NULL) {
        free(column_active);
    }
    column_active = nmalloc((size_t)screen_cols);
    memset(column_active, 0, (size_t)screen_cols);
    if (column_clear_buffer != NULL) {
        free(column_clear_buffer);
    }
    column_clear_buffer = nmalloc((size_t)screen_cols * sizeof(int));
    for (j = 0; j < screen_cols; j++)
        column_clear_buffer[j] = 0;
    /* Sequential-drop mode: one column starts active; the spawn-delay timer ramps
     * up until active_col_count reaches max_col_drops (66% of columns, rounded up). */
    max_col_drops = (screen_cols * 66 + 99) / 100;
    if (max_col_drops < 1)
        max_col_drops = 1;
    if (max_col_drops > screen_cols)
        max_col_drops = screen_cols;
    active_col_count = 0;

    /* Make the matrix (all columns so no cell is uninitialized) */
    for (i = 0; i <= screen_lines + 1; i++) {
        for (j = 0; j < screen_cols; j++) {
            matrix[i][j].val = -1;
            matrix[i][j].is_head = false;
            matrix[i][j].sweegie_fade = 0;
            matrix[i][j].sweegie_base_color = (unsigned char)COLOR_GREEN;
            matrix[i][j].sweegie_base_bold = 0;
        }
    }

    matrix_snap = nmalloc(sizeof(cmatrix) * (screen_lines + 2) * (size_t)screen_cols);

    /* Spawn the first drop during init, then start the spawn-delay timer. */
    {
        int first_col = (int)rand_r(&cmatrix_main_rng) % screen_cols;
        column_active[first_col] = 1;
        active_col_count = 1;
        spec_prepare_spawn_in_column(first_col);
        last_spawn_col = first_col;

        spawn_timer_remaining_sec = spawn_interval_initial_sec; /* 3.00 */
        spawn_timer_enabled = 1;
        clock_gettime(CLOCK_MONOTONIC, &spawn_timer_start_ts);
        spawn_timer_last_tick_ts = spawn_timer_start_ts;
    }

    /* Dirty tracking: previous-frame buffer */
    if (prev_cell != NULL)
        free(prev_cell);
    prev_cell = nmalloc((size_t)(screen_lines * screen_cols) * sizeof(rendered_cell));
    for (i = 0; i < screen_lines * screen_cols; i++)
        prev_cell[i].ch = -1;  /* sentinel: force full draw on first frame / after resize */

    full_redraw_pending = 1;

    /* No per-column initial delay setup here:
     * the first drop is spawned explicitly above, and subsequent drops are
     * activated in main() by the spawn-delay timer. */
}

/* ---------------------------------------------------------------------------
 * Signal handler: only sets signal_status so the main loop can handle
 * SIGINT/SIGQUIT (exit), SIGWINCH (resize), SIGTSTP (exit) safely from
 * a single thread (no async-signal-unsafe calls here).
 * --------------------------------------------------------------------------- */
void sighandler(int s) {
    signal_status = s;
}

/* ---------------------------------------------------------------------------
 * Handle terminal resize (SIGWINCH): read new size via TIOCGWINSZ,
 * refresh (notcurses_resize + clear), set screen_lines/cols from stddim,
 * then var_init() and restart worker threads.
 * --------------------------------------------------------------------------- */
void resize_screen(void) {
    char *tty;
    int fd = 0;
    int result = 0;
    struct winsize win;
    unsigned r = 0, c = 0;

    tty = ttyname(0);
    if (!tty)
        return;
    fd = open(tty, O_RDWR);
    if (fd == -1)
        return;
    result = ioctl(fd, TIOCGWINSZ, &win);
    if (result == -1) {
        close(fd);
        return;
    }
    close(fd);

    cmatrix_stop_workers();
#if defined(__APPLE__) && defined(HAVE_SYS_IOCTL_H)
    if (cmatrix_nc != NULL && cmatrix_plane != NULL &&
        cmatrix_plane != notcurses_stdplane(cmatrix_nc) &&
        win.ws_row > 0 && win.ws_col > 0) {
        (void)ncplane_resize_simple(cmatrix_plane, (unsigned)win.ws_row, (unsigned)win.ws_col);
        {
            unsigned dimy = 24, dimx = 80;

            ncplane_dim_yx(cmatrix_plane, &dimy, &dimx);
            screen_lines = (int)dimy;
            screen_cols = (int)dimx;
        }
        if (screen_lines < 1)
            screen_lines = 1;
        if (screen_cols < 1)
            screen_cols = 1;
        var_init();
        cmatrix_start_workers();
        return;
    }
#endif
    if (cmatrix_nc != NULL) {
        (void)notcurses_refresh(cmatrix_nc, &r, &c);
        {
            unsigned dimy = 24, dimx = 80;

            notcurses_stddim_yx(cmatrix_nc, &dimy, &dimx);
            screen_lines = (int)dimy;
            screen_cols = (int)dimx;
        }
#ifdef HAVE_SYS_IOCTL_H
        if (screen_lines < 1)
            screen_lines = 1;
        if (screen_cols < 1)
            screen_cols = 1;
#else
        if (screen_lines < 10)
            screen_lines = 10;
        if (screen_cols < 10)
            screen_cols = 10;
#endif
    } else {
        screen_cols = win.ws_col;
        screen_lines = win.ws_row;
        if (screen_lines < 10)
            screen_lines = 10;
        if (screen_cols < 10)
            screen_cols = 10;
    }

    var_init();
    cmatrix_start_workers();
}

/* ---------------------------------------------------------------------------
 * Entry point. Parses options, initializes locale/ncurses/colors, sets up
 * the matrix and main loop. Loop: handle signals, read keypress (screensaver/
 * toggle options), update active columns (spawn, advance, clear buffer,
 * lock/unlock columns), draw the grid, optionally draw lock message, sleep.
 * --------------------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    int i, optchr;
    int j = 0;
    int screensaver = 0;
    int asynch = 0;
    int bold = 0;
    /* Default 24 fps. -F selects 12–60 fps. */
    int delay_ms = (int)(1000.0 / 24.0 + 0.5);
    int head_color = COLOR_HEAD_RGB;
    int tail_color = COLOR_GREEN;
    int message_color = COLOR_RED;
    uint32_t rgb_head = 0xc3ffbfu;
    uint32_t rgb_tail = cmatrix_default_tail_green_rgb();
    uint32_t rgb_msg = 0xff0000u;
    int pause = 0;
    int classic = 0;
    int changes = 0;
    char *msg = "";

    cmatrix_main_rng = (unsigned int)time(NULL);

    /* -c matrix glyph mode; UTF-8 locale required for correct display */
    (void) setlocale(LC_ALL, "");
    if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
        (void) setlocale(LC_CTYPE, "");

    /* --- Command-line option parsing (getopt) --- */
    opterr = 0;
    while ((optchr = getopt(argc, argv, "abchskVM:T:H:O:F:")) != EOF) {
        switch (optchr) {
        case 's':
            screensaver = 1;
            break;
        case 'a':
            asynch = 1;
            break;
        case 'b':
            bold = 1;
            break;
        case 'T': {
            uint32_t rgb;
            int leg;
            if (cmatrix_parse_color_optarg(optarg, &rgb, &leg) != 0)
                c_die(" Invalid tail color (-T). Use a color name or hex "
                       "(#RRGGBB).\n");
            rgb_tail = rgb;
            if (leg >= 0)
                tail_color = leg;
            else
                tail_color = COLOR_CUSTOM_TAIL;
            break;
        }
        case 'H': {
            uint32_t rgb;
            int leg;
            if (cmatrix_parse_color_optarg(optarg, &rgb, &leg) != 0)
                c_die(" Invalid head color (-H). Use a color name or hex "
                       "(#RRGGBB).\n");
            rgb_head = rgb;
            if (leg >= 0)
                head_color = leg;
            else
                head_color = COLOR_HEAD_RGB;
            break;
        }
        case 'O': {
            uint32_t rgb;
            int leg;
            if (cmatrix_parse_color_optarg(optarg, &rgb, &leg) != 0)
                c_die(" Invalid message color (-O). Use a color name or hex "
                       "(#RRGGBB).\n");
            rgb_msg = rgb;
            if (leg >= 0)
                message_color = leg;
            else
                message_color = COLOR_CUSTOM_MSG;
            break;
        }
        case 'c':
            classic = 1;
            break;
        case 'M':
            msg = strdup(optarg);
            break;
        case 'h':
        case '?':
            usage();
            exit(0);
        case 'F': {
            double fps = atof(optarg);
            if (fps >= 12.0 && fps <= 60.0) {
                delay_ms = (int)(1000.0 / fps + 0.5);
                if (delay_ms < 1)
                    delay_ms = 1;
            } else {
                c_die(" -F fps must be between 12 and 60 (e.g. 24, 60).\n");
            }
            break;
        }
        case 'V':
            version();
            exit(0);
        case 'k':
            changes = 1;
            break;
        }
    }

    /* --- Locale: UTF-8; all cells go through ncplane_putegc_yx. Rain (PUA): UTF-8 via
     * codepoint_to_utf8 in draw_cell_at. -M message: one byte per character from msg[];
     * draw_cell_at maps ch > 127 to '#'. --- */
    if (setlocale(LC_ALL, "C.UTF-8") == NULL)
        (void) setlocale(LC_ALL, "");

    /* -c is unstable with TERM=dumb (e.g. IDE integrated terminals); disable to avoid crash */
    if (classic) {
        const char *term = getenv("TERM");
        if (!term || strcmp(term, "dumb") == 0) {
            classic = 0;
            fprintf(stderr, "cmatrix: -c disabled (TERM is dumb or unset). Run in a proper terminal for matrix glyph mode.\n");
        }
    }

    /* When -c is on: CMATRIX_ASCII_FALLBACK forces ASCII-only drawing where applicable. */
    static int use_ascii_fallback = -1;
    if (use_ascii_fallback < 0)
        use_ascii_fallback = (getenv("CMATRIX_ASCII_FALLBACK") != NULL) ? 1 : 0;

    /* Switch terminal font to DejaVu Sans Mono (merged rain PUA); restore on exit. */
    {
        const char *term = getenv("TERM");
        if (term && strcmp(term, "dumb") != 0)
            try_font_switch_on();
    }

    /* --- Notcurses (truecolor); we handle SIGWINCH ourselves --- */
    {
        notcurses_options ncopts;

        memset(&ncopts, 0, sizeof(ncopts));
        ncopts.flags = NCOPTION_SUPPRESS_BANNERS | NCOPTION_NO_WINCH_SIGHANDLER;
        /* Workaround for broken alternate screen (try if the display is garbled). */
        if (getenv("CMATRIX_NO_ALTERNATE_SCREEN"))
            ncopts.flags |= NCOPTION_NO_ALTERNATE_SCREEN;

        {
            FILE *ncout = stdout;

#if defined(__APPLE__)
            /* Upstream USAGE.md: notcurses_init() "You'll usually want stdout".
             * notcurses_core_init() maps NULL -> stdout (it does not open /dev/tty).
             * A write-only fopen("/dev/tty","w") was an experiment for winsize;
             * keep it opt-in only: CMATRIX_USE_DEVTTY=1. */
            cmatrix_notcurses_outfp = NULL;
            if (isatty(STDOUT_FILENO) && getenv("CMATRIX_USE_DEVTTY") != NULL) {
                FILE *tf = fopen("/dev/tty", "w");

                if (tf != NULL) {
                    cmatrix_notcurses_outfp = tf;
                    ncout = cmatrix_notcurses_outfp;
                }
            }
#endif
            cmatrix_nc = notcurses_init(&ncopts, ncout);
            if (cmatrix_nc == NULL) {
#if defined(__APPLE__)
                if (cmatrix_notcurses_outfp) {
                    fclose(cmatrix_notcurses_outfp);
                    cmatrix_notcurses_outfp = NULL;
                }
#endif
                c_die("cmatrix: notcurses_init failed (requires a real terminal and UTF-8 locale).\n");
            }
        }
        cmatrix_plane = notcurses_stdplane(cmatrix_nc);

#if defined(__APPLE__)
        /* notcurses#2812: Terminal.app can leave palette-query bytes in the input
         * queue; drain nonblocking reads so the main loop does not treat them as keys. */
        if (getenv("TERM_PROGRAM") && strcmp(getenv("TERM_PROGRAM"), "Apple_Terminal") == 0) {
            int d;

            for (d = 0; d < 4096; d++) {
                uint32_t id = notcurses_get_nblock(cmatrix_nc, NULL);

                if (id == 0 || id == (uint32_t)-1)
                    break;
            }
        }
#endif
        signal(SIGINT, sighandler);
        signal(SIGQUIT, sighandler);
        signal(SIGWINCH, sighandler);
        signal(SIGTSTP, sighandler);
    }

    /* --- Screen size and one-time matrix init --- */
    {
        unsigned dimy = 24, dimx = 80;

#if defined(__APPLE__) && defined(HAVE_SYS_IOCTL_H)
        cmatrix_try_macos_fullscreen_pile();
#endif
        /* ncpile_render() runs notcurses_resize_internal() (TIOCGWINSZ + pile sync). */
        if (ncpile_render(cmatrix_plane) != 0)
            c_die("cmatrix: ncpile_render failed (internal error).\n");
#if defined(__APPLE__) && defined(HAVE_SYS_IOCTL_H)
        if (cmatrix_macos_fix_aux_pile_after_resize_internal()) {
            if (ncpile_render(cmatrix_plane) != 0)
                c_die("cmatrix: ncpile_render failed (internal error).\n");
        }
#endif
        ncplane_dim_yx(cmatrix_plane, &dimy, &dimx);
        screen_lines = (int)dimy;
        screen_cols = (int)dimx;
    }
#ifdef HAVE_SYS_IOCTL_H
    if (screen_lines < 1)
        screen_lines = 1;
    if (screen_cols < 1)
        screen_cols = 1;
#else
    if (screen_lines < 10)
        screen_lines = 10;
    if (screen_cols < 10)
        screen_cols = 10;
#endif

    var_init();
    cmatrix_start_workers();

    /* Clear -M message state when message is set (invisible until revealed by drop). */
    if (msg[0] != '\0') {
        size_t ml = strlen(msg);
        if (ml > MSG_STATE_MAX)
            ml = MSG_STATE_MAX;
        memset(msg_visible, 0, ml);
        memset(msg_was_on_message, 0, ml);
        msg_spawns_since_unrevealed = 0;
    }

    /* Monotonic absolute wake times: steadier pacing than napms; catches up if a frame runs long. */
    struct timespec frame_next;
    clock_gettime(CLOCK_MONOTONIC, &frame_next);

    /* --- Main loop: signals, input, column updates, draw, lock message, sleep --- */
    while (1) {
        /* Handle signals from sighandler (exit or resize). */
        if (signal_status == SIGINT || signal_status == SIGQUIT) {
            if (lock != 1)
                finish();
            /* exits */
        }
        if (signal_status == SIGWINCH) {
            resize_screen();
            signal_status = 0;
            if (msg[0] != '\0') {
                size_t ml = strlen(msg);
                if (ml > MSG_STATE_MAX)
                    ml = MSG_STATE_MAX;
                memset(msg_visible, 0, ml);
                memset(msg_was_on_message, 0, ml);
                msg_spawns_since_unrevealed = 0;
            }
        }

        if (signal_status == SIGTSTP) {
            if (lock != 1)
                    finish();
        }

        /* Process keypress: screensaver (exit on any key) or runtime toggles. */
        {
            ncinput ni;
            uint32_t ich;
            memset(&ni, 0, sizeof(ni));
            ich = notcurses_get_nblock(cmatrix_nc, &ni);
            if (ich != 0u && ich != (uint32_t)-1 && ich < 256u) {
                char keypress = (char)ich;
                if (screensaver == 1) {
                    finish();
                } else {
                    switch (keypress) {
                    case 'q':
                        if (lock != 1)
                            finish();
                        break;
                    case 'a':
                        asynch = 1 - asynch;
                        break;
                    case 'b':
                        bold = 1;
                        break;
                    case 'L':
                        lock = 1;
                        break;
                    case 'n':
                        bold = 0;
                        break;
                    case '!':
                        tail_color = COLOR_RED;
                        rgb_tail = cmatrix_palette_rgb(tail_color);
                        break;
                    case '@':
                        tail_color = COLOR_GREEN;
                        rgb_tail = cmatrix_palette_rgb(tail_color);
                        break;
                    case '#':
                        tail_color = COLOR_YELLOW;
                        rgb_tail = cmatrix_palette_rgb(tail_color);
                        break;
                    case '$':
                        tail_color = COLOR_BLUE;
                        rgb_tail = cmatrix_palette_rgb(tail_color);
                        break;
                    case '%':
                        tail_color = COLOR_MAGENTA;
                        rgb_tail = cmatrix_palette_rgb(tail_color);
                        break;
                    case '^':
                        tail_color = COLOR_CYAN;
                        rgb_tail = cmatrix_palette_rgb(tail_color);
                        break;
                    case '&':
                        tail_color = COLOR_WHITE;
                        rgb_tail = cmatrix_palette_rgb(tail_color);
                        break;
                    case 'p':
                    case 'P':
                        pause = (pause == 0) ? 1 : 0;
                        break;
                    default:
                        break;
                    }
                }
            }
        }

        /* -M message position (center); used in draw loop for message-cell logic. */
        int msg_row = screen_lines/2 + 1;
        int msg_len = (msg[0] != '\0') ? (int)strlen(msg) : 0;
        int msg_y = (msg_len > 0) ? (screen_cols/2 - msg_len/2) : 0;

        /* Timer-gated sequential drop spawning:
         * - first drop spawns during var_init(), then timer starts
         * - while enabled, new drops are activated only when timer hits zero
         * - timer interval exponentially decays from 2.00s to ~0 over ~10s
         * - after ~10s, disable timer so subsequent drops ignore it */
        if (spawn_timer_enabled) {
            struct timespec now_ts;
            clock_gettime(CLOCK_MONOTONIC, &now_ts);
            double elapsed =
                (double)(now_ts.tv_sec - spawn_timer_start_ts.tv_sec) +
                (double)(now_ts.tv_nsec - spawn_timer_start_ts.tv_nsec) / 1000000000.0;

            if (elapsed >= spawn_timer_disable_after_sec) {
                spawn_timer_enabled = 0;
                spawn_timer_remaining_sec = 0.0;
                while (active_col_count < max_col_drops) {
                    pthread_mutex_lock(&cmatrix_spawn_mx);
                    unlock_one_column_and_spawn(msg, msg_len, msg_y);
                    pthread_mutex_unlock(&cmatrix_spawn_mx);
                }
            } else {
                double dt =
                    (double)(now_ts.tv_sec - spawn_timer_last_tick_ts.tv_sec) +
                    (double)(now_ts.tv_nsec - spawn_timer_last_tick_ts.tv_nsec) / 1000000000.0;
                if (dt < 0)
                    dt = 0;
                spawn_timer_last_tick_ts = now_ts;

                spawn_timer_remaining_sec -= dt;
                while (spawn_timer_enabled && spawn_timer_remaining_sec <= 0.0) {
                    if (active_col_count < max_col_drops) {
                        pthread_mutex_lock(&cmatrix_spawn_mx);
                        unlock_one_column_and_spawn(msg, msg_len, msg_y);
                        pthread_mutex_unlock(&cmatrix_spawn_mx);
                    }

                    /* Recompute elapsed at the moment of timer expiry. */
                    clock_gettime(CLOCK_MONOTONIC, &now_ts);
                    elapsed =
                        (double)(now_ts.tv_sec - spawn_timer_start_ts.tv_sec) +
                        (double)(now_ts.tv_nsec - spawn_timer_start_ts.tv_nsec) / 1000000000.0;

                    if (elapsed >= spawn_timer_disable_after_sec) {
                        spawn_timer_enabled = 0;
                        spawn_timer_remaining_sec = 0.0;
                        while (active_col_count < max_col_drops) {
                            pthread_mutex_lock(&cmatrix_spawn_mx);
                            unlock_one_column_and_spawn(msg, msg_len, msg_y);
                            pthread_mutex_unlock(&cmatrix_spawn_mx);
                        }
                        break;
                    }

                    /* Parabolic decay from 3.00s to 0.00s over ~15s. */
                    double t = elapsed / spawn_timer_disable_after_sec;
                    if (t < 0.0)
                        t = 0.0;
                    if (t > 1.0)
                        t = 1.0;
                    double next_interval =
                        spawn_interval_initial_sec * (1.0 - t) * (1.0 - t);
                    /* Quantize to 2 decimals as requested. */
                    next_interval = floor(next_interval * 100.0 + 0.5) / 100.0;
                    if (next_interval < 0.0)
                        next_interval = 0.0;
                    spawn_timer_remaining_sec = next_interval;

                    /* Spawn-delay timer gates only one activation per expiry. */
                    break;
                }
            }
        }

        /* Snapshot for neighbor reads; worker threads update live matrix. */
        if (matrix_snap != NULL && matrix != NULL && matrix[0] != NULL) {
            size_t sz = sizeof(cmatrix) * (size_t)(screen_lines + 2) * (size_t)screen_cols;
            memcpy(matrix_snap, matrix[0], sz);
        }
        gsim_pause = pause;
        gsim_asynch = asynch;
        gsim_delay_ms = delay_ms;
        gsim_changes = changes;
        gsim_head_color = head_color;
        gsim_tail_color = tail_color;
        gsim_bold = bold;
        gsim_msg = msg;
        gsim_msg_len = msg_len;
        gsim_msg_row = msg_row;
        gsim_msg_y = msg_y;
        {
            int w;
            for (w = 0; w < cmatrix_num_workers; w++) {
                pthread_mutex_lock(&cmatrix_mtx_start[w]);
                cmatrix_start_pulse[w] = 1;
                pthread_cond_signal(&cmatrix_cv_start[w]);
                pthread_mutex_unlock(&cmatrix_mtx_start[w]);
            }
            for (w = 0; w < cmatrix_num_workers; w++) {
                pthread_mutex_lock(&cmatrix_mtx_done[w]);
                while (!cmatrix_done_pulse[w])
                    pthread_cond_wait(&cmatrix_cv_done[w], &cmatrix_mtx_done[w]);
                cmatrix_done_pulse[w] = 0;
                pthread_mutex_unlock(&cmatrix_mtx_done[w]);
            }
        }

        for (j = 0; j < screen_cols; j++) {
            /* Skip rendering columns that cannot have changed this frame (no rain sim,
             * no -M band). Same idea as video coding: only send what changed; full
             * redraw_pending is a keyframe after resize/init. */
            if (pause && !full_redraw_pending)
                continue;
            if (!full_redraw_pending && column_active != NULL &&
                !column_active[j] &&
                !(msg_len > 0 && j >= msg_y && j < msg_y + msg_len))
                continue;
            /* Draw this column: only cells that changed (dirty tracking). */
            {
            int y = 1;
            int z = screen_lines;
            if (z > screen_lines)
                z = screen_lines;
            for (i = y; i <= z; i++) {
                int line = i - 1;
                /* Terminal line (line) keys prev_cell; matrix row i matches get_rendered_cell(i,). */
                int line_idx = line * screen_cols + j;
                int matrix_idx = i * screen_cols + j;
                int mk = -1;
                int is_msg_cell = 0;
                if (msg_len > 0 && i == msg_row && j >= msg_y && j < msg_y + msg_len) {
                    mk = j - msg_y;
                    if (mk >= 0 && mk < msg_len && mk < MSG_STATE_MAX && msg[mk] != ' ')
                        is_msg_cell = 1;
                }
                cmatrix cell = (matrix != NULL && matrix[0] != NULL && matrix_idx >= 0 && matrix_idx <= (screen_lines + 2) * screen_cols - 1)
                    ? matrix[0][matrix_idx] : (cmatrix){ .val = -1, .is_head = false, .sweegie_fade = 0, .sweegie_base_color = COLOR_GREEN, .sweegie_base_bold = 0 };
                int message_cell_with_side_effect = (is_msg_cell && mk >= 0 && mk < MSG_STATE_MAX && (cell.is_head || msg_was_on_message[mk]));
                rendered_cell current = get_rendered_cell(i, j, msg, msg_len, msg_row, msg_y, bold, head_color, tail_color, message_color, use_ascii_fallback,
                    rgb_head, rgb_tail, rgb_msg);
                if (prev_cell != NULL && prev_cell[line_idx].ch != -1 && memcmp(&current, &prev_cell[line_idx], sizeof(rendered_cell)) == 0 && !message_cell_with_side_effect)
                    continue;
                draw_cell_at(cmatrix_plane, line, j, &current, use_ascii_fallback);
                if (prev_cell != NULL)
                    prev_cell[line_idx] = current;
                if (is_msg_cell && mk >= 0 && mk < MSG_STATE_MAX) {
                    if (cell.is_head) {
                        msg_was_on_message[mk] = 1;
                        if (!msg_visible[mk])
                            msg_visible[mk] = 1;
                    } else if (msg_was_on_message[mk])
                        msg_was_on_message[mk] = 0;
                }
            }
            }
        }

        /* Paused + static screen: skip render (no cell changes). */
        if (!(pause && !full_redraw_pending)) {
            cmatrix_present_frame();
        }

        if (full_redraw_pending)
            full_redraw_pending = 0;

        /* Frame pacing: -F sets delay_ms; when paused with no resize keyframe, sleep longer (less CPU). */
        {
            long long add_ns;
            if (pause && !full_redraw_pending)
                add_ns = 100LL * 1000000LL;
            else
                add_ns = (long long)delay_ms * 1000000LL;
            frame_next.tv_nsec += add_ns;
            while (frame_next.tv_nsec >= 1000000000L) {
                frame_next.tv_sec++;
                frame_next.tv_nsec -= 1000000000L;
            }
            cmatrix_sleep_until_monotonic(&frame_next);
        }
    }
    finish();
}
