# Changelog

Changes in this fork relative to upstream [cmatrix](https://github.com/abishekvashok/cmatrix) (Chris Allegretta, Abishek V Ashok). This fork by shr00mie.

---

## [Unreleased]

### Added

- **Head color (`-H`)** — Set color of the drop head (default: white).
- **Tail color (`-T`)** — Set color of the matrix tail (default: green). Replaces upstream `-C` (same behavior, new letter).
- **Message color (`-O`)** — Set color of the `-M` message (default: red).
- **Frame rate (`-F`)** — Set frame rate in fps (e.g. `23.976`, `30`, `60`). Default 23.976 fps.
- **Source** — Head, tail, and message colors are variables used throughout; default frame delay 23.976 fps.
- **Version output** — `-V` credits original authors and "This version by shr00mie."

### Changed

- **Frame rate** — No `-u` or keyboard 0–9. Rate is fixed by default (23.976 fps) or set with `-F`.
- **Bold** — Only off or random bold; "all bold" mode (`-B`) removed.
- **Active columns** — Fraction of columns initialized for new drops increased from ~75% to ~85%.
- **Platform** — Linux (amd64) only; configure and CMake enforce this.
- **Console mode (`-l`)** — No bundled console fonts; use setfont/consolechars with a font you install separately.

### Removed

- **`-u`** — Update/delay option and 0–9 speed keys.
- **`-B`** — All-bold option and keyboard `B`.
- **`-x`** — X window mode; A_ALTCHARSET and character range now depend only on `-l`.
- **`-t`** — TTY option; ncurses always uses the default terminal.
- **Bundled fonts** — `mtx.pcf` (X11), `matrix.fnt` and `matrix.psf.gz` (console); no longer shipped or installed.
- **Non-Linux** — Windows/mingw support removed from configure and build.
