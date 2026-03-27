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

#define NCURSES_WIDECHAR 1

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <locale.h>
#include <math.h>

#ifndef EXCLUDE_CONFIG_H
#include "config.h"
#endif

#include "cmatrix.h"

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif

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
    size_t len = strlen(buf);
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
 * Global variables (declared in cmatrix.h). Mode flags (console,
 * lock), the 2D matrix grid, and per-column state: length/spaces/updates,
 * column_active (which columns are raining), column_clear_buffer (sliding
 * clear zone). signal_status is set by sighandler for the main loop.
 * --------------------------------------------------------------------------- */
int console = 0;
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

/* Per-character -M message state: visible[] and wasOnMessage[] (sized to message length).
 * Spaces in the message are not treated as message characters (no reveal tracking, rain passes through). */
#define MSG_STATE_MAX 512
static unsigned char msg_visible[MSG_STATE_MAX];
static unsigned char msg_was_on_message[MSG_STATE_MAX];
/* Pair ids used for deterministic green fade ramp (source -> black). */
#define SWEEGIE_FADE_STEPS 16
static int tail_fade_pairs[SWEEGIE_FADE_STEPS];
static int tail_fade_ready = 0;

/* Dirty tracking: previous-frame buffer and per-line dirty for wnoutrefresh/doupdate. */
typedef struct {
    int ch;     /* codepoint or ASCII (e.g. ' ', '#' or U+FF66..U+FF9D) */
    int color;  /* head_color, tail_color, or message_color */
    int attrs;  /* ncurses attrs (A_BOLD/A_DIM/...) */
} rendered_cell;
static rendered_cell *prev_cell = NULL;
static unsigned char *dirty_lines = NULL;
/* After var_init/resize, draw every cell once (keyframe). Then skip columns whose
 * simulation did not run and which do not carry -M message (inter-frame skip). */
static int full_redraw_pending = 1;
/* -M respawn bias: until the whole (non-space) message is revealed, if 2 new drops did not start in
 * unrevealed columns, the 3rd must start in one (if any). */
static int msg_spawns_since_unrevealed = 0;
/* Avoid spawning consecutive drops in the same column (aesthetics). */
static int last_spawn_col = -1;

#if defined(NCURSES_WIDECHAR) && defined(NCURSES_VERSION)
/* ---------------------------------------------------------------------------
 * Output one wide character (used for -c / classic Japanese mode). Uses
 * add_wch + cchar_t for stability; addwstr/addnwstr can segfault when rain
 * hits the bottom. Half-width katakana (U+FF66..U+FF9D) are one column each.
 * See docs/HALFWIDTH_KATAKANA_RESEARCH.md for the full spec and references.
 * --------------------------------------------------------------------------- */
static int add_wide_char(wchar_t wc) {
    cchar_t cchar;
    wchar_t wbuf[2] = { wc, L'\0' };
    memset(&cchar, 0, sizeof(cchar));  /* avoid garbage in opaque struct on some ncurses */
    if (setcchar(&cchar, wbuf, A_NORMAL, 0, NULL) != OK)
        return ERR;
    return add_wch(&cchar);
}

/* Encode BMP codepoint (0..0xFFFF) to UTF-8. Returns length; buf must have at least 4 bytes. */
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
#endif

/* ---------------------------------------------------------------------------
 * Non-message (rain): 104 merged Matrix glyphs in DejaVuSansMono_patched.ttf.
 * Same order as glyph grid nonempty index 1962–2065: PUA U+E000–U+E067.
 * Terminal uses font cmap only.
 * --------------------------------------------------------------------------- */
#define MATRIX_FIRST 0xE000
#define MATRIX_LAST  0xE067
#define MATRIX_CODE_GLYPHS 104

/* ---------------------------------------------------------------------------
 * Return a random character for matrix[row][col] that is not equal to any
 * value in the 8 adjacent cells (vertical, horizontal, diagonal). Retries
 * up to 50 times; then returns a random character anyway to avoid infinite loop.
 * --------------------------------------------------------------------------- */
static int random_char_avoiding_neighbors(int row, int col) {
    int adj[8], n_adj = 0;
    int dr[] = { -1, -1, -1,  0, 0,  1, 1, 1 };
    int dc[] = { -1,  0,  1, -1, 1, -1, 0, 1 };
    int d;
    for (d = 0; d < 8; d++) {
        int r = row + dr[d], c = col + dc[d];
        if (r >= 0 && r <= screen_lines && c >= 0 && c < screen_cols && matrix != NULL)
            adj[n_adj++] = matrix[r][c].val;
    }
    for (d = 0; d < 50; d++) {
        int val = MATRIX_FIRST + (int) (rand() % MATRIX_CODE_GLYPHS);
        int ok = 1;
        int a;
        for (a = 0; a < n_adj; a++)
            if (val == adj[a]) { ok = 0; break; }
        if (ok)
            return val;
    }
    return MATRIX_FIRST + (int) (rand() % MATRIX_CODE_GLYPHS);
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

static int tail_fade_pair_for_amount(unsigned char fade_amt, int fallback_pair) {
    int idx;
    if (!tail_fade_ready)
        return fallback_pair;
    idx = (int)((fade_amt * (SWEEGIE_FADE_STEPS - 1)) / 255);
    if (idx < 0)
        idx = 0;
    if (idx >= SWEEGIE_FADE_STEPS)
        idx = SWEEGIE_FADE_STEPS - 1;
    return tail_fade_pairs[idx];
}

static int rand_inclusive(int lo, int hi) {
    if (hi < lo)
        return lo;
    return lo + (int)(rand() % (hi - lo + 1));
}

static int column_has_previous_tail(int col) {
    int r;
    if (matrix == NULL || col < 0 || col >= screen_cols)
        return 0;
    for (r = 0; r <= screen_lines; r++) {
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
    return rand_inclusive(lo, hi);
}

/* Spec (updated): sweegie in [3, 1/2 * available] when previous tail exists. */
static int spec_sweegie_length_for_tailed_column(void) {
    int avail = available_rows_for_new_drop();
    int hi = avail / 2;
    if (hi < 3)
        hi = 3;
    return rand_inclusive(3, hi);
}

static void spec_prepare_spawn_in_column(int col) {
    int had_tail = column_has_previous_tail(col);
    if (col < 0 || col >= screen_cols)
        return;
    spaces[col] = (int) rand() % screen_lines + 1;
    length[col] = spec_tail_length_for_clear_column();
    /* Per-column async speed (rows/sec), randomized on spawn. */
    if (col_speed_rps != NULL && col_row_accum != NULL) {
        int whole = rand_inclusive(10, 23);
        int frac = rand() % 100;
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
    matrix[1][col].val = random_char_avoiding_neighbors(1, col);
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
        r = rand() % eligible_unrevealed;
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
        r = rand() % eligible_locked;
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
    int bold, int head_color, int tail_color, int message_color, int use_ascii_fallback)
{
    rendered_cell r = { .ch = ' ', .color = tail_color, .attrs = 0 };
    int mk = -1;
    int is_msg_cell = 0;
    if (msg_len > 0 && i == msg_row && j >= msg_y && j < msg_y + msg_len) {
        mk = j - msg_y;
        if (mk >= 0 && mk < msg_len && mk < MSG_STATE_MAX && msg[mk] != ' ')
            is_msg_cell = 1;
    }
    if (!is_msg_cell && (matrix == NULL || matrix[0] == NULL || i < 0 || i > screen_lines || j < 0 || j >= screen_cols))
        return r;
    {
        int idx = i * screen_cols + j;
        int max_cell = (screen_lines + 1) * screen_cols - 1;
        if (!is_msg_cell && (idx < 0 || idx > max_cell))
            return r;
        cmatrix cell = matrix[0][idx];
        if (is_msg_cell && mk >= 0 && mk < MSG_STATE_MAX) {
            if (cell.is_head) {
                r.ch = (unsigned char)msg[mk];
                r.color = head_color;
                r.attrs = A_BOLD;
                return r;
            }
            if (msg_was_on_message[mk]) {
                r.ch = (unsigned char)msg[mk];
                r.color = message_color;
                r.attrs = A_BOLD;
                return r;
            }
            if (msg_visible[mk]) {
                r.ch = (unsigned char)msg[mk];
                r.color = message_color;
                r.attrs = A_BOLD;
                return r;
            }
            /* unrevealed: fall through to matrix cell */
        }
        /* Matrix cell (head or trail, or unrevealed message cell) */
        if (cell.val == -1) {
            r.ch = ' ';
            r.color = cell.is_head ? head_color : tail_color;
            r.attrs = cell.is_head ? (bold ? A_BOLD : 0) : 0;
            return r;
        }
        {
            int v = cell.val;
            if (v >= MATRIX_FIRST && v <= MATRIX_LAST) {
                r.ch = v;
            } else if (v > 127) {
                r.ch = '#';
            } else {
                r.ch = v;
            }
            if (!cell.is_head && cell.sweegie_fade > 0) {
                /* Deterministic incremental fade using explicit ncurses color ramp pairs. */
                int base_color = (cell.sweegie_base_color <= COLOR_WHITE) ? (int)cell.sweegie_base_color : tail_color;
                if (base_color == COLOR_GREEN)
                    r.color = tail_fade_pair_for_amount(cell.sweegie_fade, tail_color);
                else
                    r.color = (cell.sweegie_fade >= 200) ? COLOR_BLACK : base_color;
                r.attrs = 0;
            } else {
                r.color = cell.is_head ? head_color : tail_color;
                r.attrs = cell.is_head ? (bold ? A_BOLD : 0) : ((bold && (v % 2 == 0)) ? A_BOLD : 0);
            }
            return r;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Draw a single rendered_cell at (line, col). No state updates; caller
 * updates msg_was_on_message / msg_visible when drawing message cells.
 * --------------------------------------------------------------------------- */
static void draw_cell_at(int line, int col, const rendered_cell *r, int use_ascii_fallback)
{
    move(line, col);
    if (console)
        attron(A_ALTCHARSET);
    attron(COLOR_PAIR(r->color));
    if (r->attrs)
        attron(r->attrs);
    if (r->ch == ' ') {
        addch(' ');
    } else if (r->ch >= MATRIX_FIRST && r->ch <= MATRIX_LAST) {
#if defined(NCURSES_WIDECHAR) && defined(NCURSES_VERSION)
        /* Emit UTF-8 for PUA so the terminal gets the same bytes as printf "\U...".
         * add_wch can cause the terminal to use a different font (e.g. with bold/color). */
        if (!use_ascii_fallback) {
            unsigned char ubuf[4];
            int n = codepoint_to_utf8((unsigned int)r->ch, ubuf);
            if (n > 0) {
                ubuf[n] = '\0';
                addstr((const char *)ubuf);
            } else
                addch((chtype)'#');
        } else
            addch((chtype)'#');
#else
        addch((chtype)(unsigned char)r->ch);
#endif
    } else {
        addch((chtype)(r->ch > 127 ? '#' : (unsigned char)r->ch));
    }
    if (r->attrs)
        attroff(r->attrs);
    attroff(COLOR_PAIR(r->color));
    if (console)
        attroff(A_ALTCHARSET);
}

/* ---------------------------------------------------------------------------
 * Initialize (or re-initialize) all matrix state. Frees existing buffers,
 * allocates matrix[screen_lines+1][screen_cols], length/spaces/updates and
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

    matrix = nmalloc(sizeof(cmatrix *) * (screen_lines + 1));
    matrix[0] = nmalloc(sizeof(cmatrix) * (screen_lines + 1) * (size_t)screen_cols);
    for (i = 1; i <= screen_lines; i++) {
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
    for (i = 0; i <= screen_lines; i++) {
        for (j = 0; j < screen_cols; j++) {
            matrix[i][j].val = -1;
            matrix[i][j].is_head = false;
            matrix[i][j].sweegie_fade = 0;
            matrix[i][j].sweegie_base_color = (unsigned char)COLOR_GREEN;
            matrix[i][j].sweegie_base_bold = 0;
        }
    }

    /* Spawn the first drop during init, then start the spawn-delay timer. */
    {
        int first_col = rand() % screen_cols;
        column_active[first_col] = 1;
        active_col_count = 1;
        spec_prepare_spawn_in_column(first_col);
        last_spawn_col = first_col;

        spawn_timer_remaining_sec = spawn_interval_initial_sec; /* 3.00 */
        spawn_timer_enabled = 1;
        clock_gettime(CLOCK_MONOTONIC, &spawn_timer_start_ts);
        spawn_timer_last_tick_ts = spawn_timer_start_ts;
    }

    /* Dirty tracking: previous-frame buffer and dirty-line bitmap */
    if (prev_cell != NULL)
        free(prev_cell);
    prev_cell = nmalloc((size_t)(screen_lines * screen_cols) * sizeof(rendered_cell));
    for (i = 0; i < screen_lines * screen_cols; i++)
        prev_cell[i].ch = -1;  /* sentinel: force full draw on first frame / after resize */
    if (dirty_lines != NULL)
        free(dirty_lines);
    dirty_lines = nmalloc((size_t)screen_lines);

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
 * clamp to minimum 10x10, call ncurses resizeterm/wresize if available,
 * then var_init() to reallocate matrix and per-column arrays, and redraw.
 * --------------------------------------------------------------------------- */
void resize_screen(void) {
    char *tty;
    int fd = 0;
    int result = 0;
    struct winsize win;

    tty = ttyname(0);
    if (!tty)
        return;
    fd = open(tty, O_RDWR);
    if (fd == -1)
        return;
    result = ioctl(fd, TIOCGWINSZ, &win);
    if (result == -1)
        return;
    screen_cols = win.ws_col;
    screen_lines = win.ws_row;
    close(fd);

    if (screen_lines < 10) {
        screen_lines = 10;
    }
    if (screen_cols < 10) {
        screen_cols = 10;
    }

#ifdef HAVE_RESIZETERM
    resizeterm(screen_lines, screen_cols);
#ifdef HAVE_WRESIZE
    if (wresize(stdscr, screen_lines, screen_cols) == ERR) {
        c_die("Cannot resize window!");
    }
#endif /* HAVE_WRESIZE */
#endif /* HAVE_RESIZETERM */

    var_init();
    /* Do these because width may have changed... */
    clear();
    refresh();
}

/* ---------------------------------------------------------------------------
 * Entry point. Parses options, initializes locale/ncurses/colors, sets up
 * the matrix and main loop. Loop: handle signals, read keypress (screensaver/
 * toggle options), update active columns (spawn, advance, clear buffer,
 * lock/unlock columns), draw the grid, optionally draw lock message, sleep.
 * --------------------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    int i, y, z, optchr, keypress;
    int j = 0;
    int screensaver = 0;
    int asynch = 0;
    int bold = 0;
    int force = 0;
    int firstcoldone = 0;
    int delay_ms = (int)(1000.0 / 23.976 + 0.5);  /* frame delay in ms; default 23.976 fps, or -F fps */
    int head_color = COLOR_WHITE;
    int tail_color = COLOR_GREEN;
    int message_color = COLOR_RED;
    int pause = 0;
    int classic = 0;
    int changes = 0;
    char *msg = "";

    srand((unsigned) time(NULL));

    /* -c uses Unicode (ASCII range); ncurses wide chars need UTF-8 for terminal */
    (void) setlocale(LC_ALL, "");
    if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
        (void) setlocale(LC_CTYPE, "");

    /* --- Command-line option parsing (getopt) --- */
    opterr = 0;
    while ((optchr = getopt(argc, argv, "abcfhlLnskVM:T:H:O:F:")) != EOF) {
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
        case 'T':
            if (!strcasecmp(optarg, "green")) {
                tail_color = COLOR_GREEN;
            } else if (!strcasecmp(optarg, "red")) {
                tail_color = COLOR_RED;
            } else if (!strcasecmp(optarg, "blue")) {
                tail_color = COLOR_BLUE;
            } else if (!strcasecmp(optarg, "white")) {
                tail_color = COLOR_WHITE;
            } else if (!strcasecmp(optarg, "yellow")) {
                tail_color = COLOR_YELLOW;
            } else if (!strcasecmp(optarg, "cyan")) {
                tail_color = COLOR_CYAN;
            } else if (!strcasecmp(optarg, "magenta")) {
                tail_color = COLOR_MAGENTA;
            } else if (!strcasecmp(optarg, "black")) {
                tail_color = COLOR_BLACK;
            } else {
                c_die(" Invalid color selection\n Valid "
                       "colors are green, red, blue, "
                       "white, yellow, cyan, magenta " "and black.\n");
            }
            break;
        case 'H':
            if (!strcasecmp(optarg, "green")) {
                head_color = COLOR_GREEN;
            } else if (!strcasecmp(optarg, "red")) {
                head_color = COLOR_RED;
            } else if (!strcasecmp(optarg, "blue")) {
                head_color = COLOR_BLUE;
            } else if (!strcasecmp(optarg, "white")) {
                head_color = COLOR_WHITE;
            } else if (!strcasecmp(optarg, "yellow")) {
                head_color = COLOR_YELLOW;
            } else if (!strcasecmp(optarg, "cyan")) {
                head_color = COLOR_CYAN;
            } else if (!strcasecmp(optarg, "magenta")) {
                head_color = COLOR_MAGENTA;
            } else if (!strcasecmp(optarg, "black")) {
                head_color = COLOR_BLACK;
            } else {
                c_die(" Invalid head color\n Valid "
                       "colors are green, red, blue, "
                       "white, yellow, cyan, magenta and black.\n");
            }
            break;
        case 'O':
            if (!strcasecmp(optarg, "green")) {
                message_color = COLOR_GREEN;
            } else if (!strcasecmp(optarg, "red")) {
                message_color = COLOR_RED;
            } else if (!strcasecmp(optarg, "blue")) {
                message_color = COLOR_BLUE;
            } else if (!strcasecmp(optarg, "white")) {
                message_color = COLOR_WHITE;
            } else if (!strcasecmp(optarg, "yellow")) {
                message_color = COLOR_YELLOW;
            } else if (!strcasecmp(optarg, "cyan")) {
                message_color = COLOR_CYAN;
            } else if (!strcasecmp(optarg, "magenta")) {
                message_color = COLOR_MAGENTA;
            } else if (!strcasecmp(optarg, "black")) {
                message_color = COLOR_BLACK;
            } else {
                c_die(" Invalid message color\n Valid "
                       "colors are green, red, blue, "
                       "white, yellow, cyan, magenta and black.\n");
            }
            break;
        case 'c':
            classic = 1;
            break;
        case 'f':
            force = 1;
            break;
        case 'l':
            console = 1;
            break;
        case 'L':
            lock = 1;
            //if -M was used earlier, don't override it
            if (0 == strncmp(msg, "", 1)) {
                msg = "Computer locked.";
            }
            break;
        case 'M':
            msg = strdup(optarg);
            break;
        case 'n':
            bold = 0;
            break;
        case 'h':
        case '?':
            usage();
            exit(0);
        case 'F': {
            double fps = atof(optarg);
            if (fps >= 1.0 && fps <= 24.0) {
                delay_ms = (int)(1000.0 / fps + 0.5);
            } else {
                c_die(" -F fps must be between 1 and 24 (e.g. 23.976, 24).\n");
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

    /* --- Locale: rain uses Unicode (ASCII 0x20–0x7E via add_wch); need UTF-8 --- */
    if (setlocale(LC_ALL, "C.UTF-8") == NULL)
        (void) setlocale(LC_ALL, "");

    /* -c is unstable with TERM=dumb (e.g. IDE integrated terminals); disable to avoid crash */
    if (classic) {
        const char *term = getenv("TERM");
        if (!term || strcmp(term, "dumb") == 0) {
            classic = 0;
            fprintf(stderr, "cmatrix: -c disabled (TERM is dumb or unset). Run in a proper terminal for Japanese mode.\n");
        }
    }

    /* When -c is on: 0 = draw half-width Katakana with add_wch (no fallback); 1 = ASCII only (CMATRIX_ASCII_FALLBACK). */
    static int use_ascii_fallback = -1;
    if (use_ascii_fallback < 0)
        use_ascii_fallback = (getenv("CMATRIX_ASCII_FALLBACK") != NULL) ? 1 : 0;

    if (force && strcmp("linux", getenv("TERM")))
        setenv("TERM", "linux", 1);

    /* Switch terminal font to DejaVu Sans Mono (merged rain PUA); restore on exit. */
    {
        const char *term = getenv("TERM");
        if (term && strcmp(term, "dumb") != 0)
            try_font_switch_on();
    }

    /* --- ncurses init and terminal settings --- */
    initscr();
    savetty();
    nonl();
    cbreak();
    noecho();
    timeout(0);
    leaveok(stdscr, TRUE);
    curs_set(0);
    signal(SIGINT, sighandler);
    signal(SIGQUIT, sighandler);
    signal(SIGWINCH, sighandler);
    signal(SIGTSTP, sighandler);

if (console) {
#ifdef HAVE_CONSOLECHARS
        if (va_system("consolechars -f matrix") != 0) {
            c_die
                (" There was an error running consolechars. Please make sure the\n"
                 " consolechars program is in your $PATH.  Try running \"consolechars -f matrix\" by hand.\n");
        }
#elif defined(HAVE_SETFONT)
        if (va_system("setfont matrix") != 0) {
            c_die
                (" There was an error running setfont. Please make sure the\n"
                 " setfont program is in your $PATH.  Try running \"setfont matrix\" by hand.\n");
        }
#else
        c_die(" Neither consolechars nor setfont is available; cannot use -l (Linux console mode).\n");
#endif
}

    /* --- Color pairs for matrix (green head/trail) and optional custom green --- */
    if (has_colors()) {
        int using_default_bg = 0;
        int fade_bg = COLOR_BLACK;
        int fi;
        start_color();
        /* Matrix digital rain green #00FF41 (R=0, G=255, B=65); ncurses uses 0-1000 per component */
        if (can_change_color())
            init_color(COLOR_GREEN, 0, 1000, 255);  /* 65*1000/255 ≈ 255 */
        /* Add in colors, if available */
#ifdef HAVE_USE_DEFAULT_COLORS
        if (use_default_colors() != ERR) {
            using_default_bg = 1;
            fade_bg = -1;
            init_pair(COLOR_BLACK, -1, -1);
            init_pair(COLOR_GREEN, COLOR_GREEN, -1);
            init_pair(COLOR_WHITE, COLOR_WHITE, -1);
            init_pair(COLOR_RED, COLOR_RED, -1);
            init_pair(COLOR_CYAN, COLOR_CYAN, -1);
            init_pair(COLOR_MAGENTA, COLOR_MAGENTA, -1);
            init_pair(COLOR_BLUE, COLOR_BLUE, -1);
            init_pair(COLOR_YELLOW, COLOR_YELLOW, -1);
        } else {
#else
        { /* Hack to deal with the after effects of else when HAVE_USE_DEFAULT_COLORS is not defined */
#endif
            init_pair(COLOR_BLACK, COLOR_BLACK, COLOR_BLACK);
            init_pair(COLOR_GREEN, COLOR_GREEN, COLOR_BLACK);
            init_pair(COLOR_WHITE, COLOR_WHITE, COLOR_BLACK);
            init_pair(COLOR_RED, COLOR_RED, COLOR_BLACK);
            init_pair(COLOR_CYAN, COLOR_CYAN, COLOR_BLACK);
            init_pair(COLOR_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
            init_pair(COLOR_BLUE, COLOR_BLUE, COLOR_BLACK);
            init_pair(COLOR_YELLOW, COLOR_YELLOW, COLOR_BLACK);
        }
        /* ncurses A_DIM is terminal-dependent; build explicit ramp pairs for green fade. */
        tail_fade_ready = 0;
        for (fi = 0; fi < SWEEGIE_FADE_STEPS; fi++)
            tail_fade_pairs[fi] = COLOR_GREEN;
        if (can_change_color() &&
            COLORS >= (COLOR_WHITE + 1 + SWEEGIE_FADE_STEPS) &&
            COLOR_PAIRS >= (16 + SWEEGIE_FADE_STEPS)) {
            int base_cid = COLORS - SWEEGIE_FADE_STEPS;
            int base_pid = 16;
            for (fi = 0; fi < SWEEGIE_FADE_STEPS; fi++) {
                int cid = base_cid + fi;
                int pid = base_pid + fi;
                /* fi=0 => bright green, fi=last => near black-green */
                int g = 1000 - (fi * 960) / (SWEEGIE_FADE_STEPS - 1);
                int b = 255 - (fi * 245) / (SWEEGIE_FADE_STEPS - 1);
                if (g < 0) g = 0;
                if (b < 0) b = 0;
                init_color(cid, 0, g, b);
                init_pair(pid, cid, fade_bg);
                tail_fade_pairs[fi] = pid;
            }
            tail_fade_ready = 1;
        } else if (using_default_bg) {
            /* Fallback ramp when terminal cannot redefine colors. */
            for (fi = 0; fi < SWEEGIE_FADE_STEPS; fi++)
                tail_fade_pairs[fi] = (fi > (SWEEGIE_FADE_STEPS * 3) / 4) ? COLOR_BLACK : COLOR_GREEN;
        }
    }

    /* --- Non-message rain: PUA U+E000–U+E067 (DejaVuSansMono_patched Matrix block) --- */

    /* --- Screen size and one-time matrix init --- */
    getmaxyx(stdscr, screen_lines, screen_cols);
    if (screen_lines < 10)
        screen_lines = 10;
    if (screen_cols < 10)
        screen_cols = 10;

    var_init();

    /* Clear -M message state when message is set (invisible until revealed by drop). */
    if (msg[0] != '\0') {
        size_t ml = strlen(msg);
        if (ml > MSG_STATE_MAX)
            ml = MSG_STATE_MAX;
        memset(msg_visible, 0, ml);
        memset(msg_was_on_message, 0, ml);
        msg_spawns_since_unrevealed = 0;
    }

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

        /* Process keypress: screensaver (exit on any key) or runtime toggles (q, a, b, L, n, 0–9, colors, p). */
        if ((keypress = wgetch(stdscr)) != ERR) {
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
                    break;
                case '@':
                    tail_color = COLOR_GREEN;
                    break;
                case '#':
                    tail_color = COLOR_YELLOW;
                    break;
                case '$':
                    tail_color = COLOR_BLUE;
                    break;
                case '%':
                    tail_color = COLOR_MAGENTA;
                    break;
                case '^':
                    tail_color = COLOR_CYAN;
                    break;
                case '&':
                    tail_color = COLOR_WHITE;
                    break;
                case 'p':
                case 'P':
                    pause = (pause == 0)?1:0;
                    break;

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
                while (active_col_count < max_col_drops)
                    unlock_one_column_and_spawn(msg, msg_len, msg_y);
            } else {
                double dt =
                    (double)(now_ts.tv_sec - spawn_timer_last_tick_ts.tv_sec) +
                    (double)(now_ts.tv_nsec - spawn_timer_last_tick_ts.tv_nsec) / 1000000000.0;
                if (dt < 0)
                    dt = 0;
                spawn_timer_last_tick_ts = now_ts;

                spawn_timer_remaining_sec -= dt;
                while (spawn_timer_enabled && spawn_timer_remaining_sec <= 0.0) {
                    if (active_col_count < max_col_drops)
                        unlock_one_column_and_spawn(msg, msg_len, msg_y);

                    /* Recompute elapsed at the moment of timer expiry. */
                    clock_gettime(CLOCK_MONOTONIC, &now_ts);
                    elapsed =
                        (double)(now_ts.tv_sec - spawn_timer_start_ts.tv_sec) +
                        (double)(now_ts.tv_nsec - spawn_timer_start_ts.tv_nsec) / 1000000000.0;

                    if (elapsed >= spawn_timer_disable_after_sec) {
                        spawn_timer_enabled = 0;
                        spawn_timer_remaining_sec = 0.0;
                        while (active_col_count < max_col_drops)
                            unlock_one_column_and_spawn(msg, msg_len, msg_y);
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

        /* Clear dirty-line bitmap for this frame. */
        if (dirty_lines != NULL)
            memset(dirty_lines, 0, (size_t)screen_lines);

        /* Update active columns: spawn new drops, advance segments, slide clear buffer, lock column when drop hits bottom and unlock another. */
        for (j = 0; j < screen_cols; j++) {
            if (matrix == NULL || j < 0 || j >= screen_cols)
                continue;
            if (pause == 0 && column_active != NULL && column_active[j]) {
                /* Head and sweegie must advance on the same column tick (same cadence). */
                int steps = 0;
                if (asynch == 0) {
                    steps = 1;
                } else if (col_speed_rps != NULL && col_row_accum != NULL) {
                    double dt = (double)delay_ms / 1000.0;
                    if (dt < 0.0)
                        dt = 0.0;
                    col_row_accum[j] += col_speed_rps[j] * (float)dt;
                    if (col_row_accum[j] >= 1.0f) {
                        steps = (int)col_row_accum[j];
                        /* Avoid extreme catch-up on stalls. */
                        if (steps > 3)
                            steps = 3;
                        col_row_accum[j] -= (float)steps;
                    }
                }
                while (steps-- > 0 && column_active[j]) {

                /* No in-column respawn: new drops start only via unlock_one_column_and_spawn(). */
                    /* Walk column j top-to-bottom: skip leading spaces, then segment (run of non-space), then advance head and optionally slide clear buffer; when segment hits bottom, lock column and unlock another. */
                    i = 0;
                    y = 0;
                    firstcoldone = 0;
                    while (i <= screen_lines) {
                        /* Bounds check before any matrix read (avoids reorder/overflow) */
                        if (i < 0 || i > screen_lines)
                            break;
                        /* Skip over spaces */
                        while (i <= screen_lines) {
                            int v = (i >= 0 && i <= screen_lines) ? matrix[i][j].val : -1;
                            if (v != ' ' && v != -1)
                                break;
                            i++;
                        }

                        if (i > screen_lines) {
                            break;
                        }

                        /* Go to the head of this column */
                        z = i;
                        y = 0;
                        while (i <= screen_lines) {
                            if (i < 0 || i > screen_lines)
                                break;
                            {
                                int v = matrix[i][j].val;
                                if (v == ' ' || v == -1)
                                    break;
                            }
                            matrix[i][j].is_head = false;
                            if (changes) {
                                if (rand() % 8 == 0) {
                                    matrix[i][j].val = random_char_avoiding_neighbors(i, j);
                                    matrix[i][j].sweegie_fade = 0;
                                }
                            }
                            i++;
                            y++;
                        }

                        if (i > screen_lines) {
                            /* Head stepped past bottom: freeze residual tail in this column, lock it,
                             * optionally unlock a locked column and start a new drop there. */
                            if (!firstcoldone && column_active != NULL) {
                                column_active[j] = 0;
                                if (active_col_count > 0)
                                    active_col_count--;
                                /* Replacement spawn should always occur, independent of timer ramp-up. */
                                unlock_one_column_and_spawn(msg, msg_len, msg_y);
                                break;
                            }
                            continue;
                        }

                        if (i >= 0 && i <= screen_lines) {
                            matrix[i][j].val = random_char_avoiding_neighbors(i, j);
                            matrix[i][j].is_head = true;
                            matrix[i][j].sweegie_fade = 0;
                            matrix[i][j].sweegie_base_color = (unsigned char)head_color;
                            matrix[i][j].sweegie_base_bold = (unsigned char)(bold ? 1 : 0);
                            /* Progressive fade in sweegie window, then clear at trailing edge (dist=1). */
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

                        /* If we're at the top of the column and it's reached its
                           full length (about to start moving down), we do this
                           to get it moving.  This is also how we keep segments not
                           already growing from growing accidentally =>
                         */
                        if (y > length[j] || firstcoldone) {
                            /* Only clear top of first segment; stream already extended at bottom in block above */
                            if (z >= 0 && z <= screen_lines && !firstcoldone) {
                                matrix[z][j].val = ' ';
                                matrix[z][j].sweegie_fade = 0;
                                matrix[z][j].sweegie_base_color = (unsigned char)tail_color;
                                matrix[z][j].sweegie_base_bold = 0;
                                if (i > screen_lines) {
                                    matrix[0][j].val = -1; /* segment hit bottom, allow respawn */
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
            y = 1;
            z = screen_lines;
            if (z > screen_lines)
                z = screen_lines;
            for (i = y; i <= z; i++) {
                int line = i - 1;
                int idx = line * screen_cols + j;
                int mk = -1;
                int is_msg_cell = 0;
                if (msg_len > 0 && i == msg_row && j >= msg_y && j < msg_y + msg_len) {
                    mk = j - msg_y;
                    if (mk >= 0 && mk < msg_len && mk < MSG_STATE_MAX && msg[mk] != ' ')
                        is_msg_cell = 1;
                }
                cmatrix cell = (matrix != NULL && matrix[0] != NULL && idx >= 0 && idx <= (screen_lines + 1) * screen_cols - 1)
                    ? matrix[0][idx] : (cmatrix){ .val = -1, .is_head = false, .sweegie_fade = 0, .sweegie_base_color = COLOR_GREEN, .sweegie_base_bold = 0 };
                int message_cell_with_side_effect = (is_msg_cell && mk >= 0 && mk < MSG_STATE_MAX && (cell.is_head || msg_was_on_message[mk]));
                rendered_cell current = get_rendered_cell(i, j, msg, msg_len, msg_row, msg_y, bold, head_color, tail_color, message_color, use_ascii_fallback);
                if (prev_cell != NULL && prev_cell[idx].ch != -1 && memcmp(&current, &prev_cell[idx], sizeof(rendered_cell)) == 0 && !message_cell_with_side_effect)
                    continue;
                draw_cell_at(line, j, &current, use_ascii_fallback);
                if (prev_cell != NULL)
                    prev_cell[idx] = current;
                if (dirty_lines != NULL)
                    dirty_lines[line] = 1;
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

        /* Only refresh changed lines, then one burst to terminal. */
        if (dirty_lines != NULL) {
            int line;
            for (line = 0; line < screen_lines; line++) {
                if (dirty_lines[line])
                    touchline(stdscr, line, 1);
            }
        }
        wnoutrefresh(stdscr);
        doupdate();

        if (full_redraw_pending)
            full_redraw_pending = 0;

        /* Frame delay in ms (default 23.976 fps, or -F fps). */
        napms(delay_ms);
    }
    finish();
}
