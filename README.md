<div style="background:#030f03;color:#00ff4f;padding:16px 18px;border:1px solid #00ff4f1a;border-radius:10px;">
  <h1 style="margin:0 0 8px 0;font-size:28px;letter-spacing:0.5px;">CMatrix (Matrix rain)</h1>
  <div style="font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;color:#9bffb0;">
    Green tail + bright head. Terminal animation inspired by the classic “Matrix” rain.
  </div>
</div>

## For the time being, Ubuntu only.

## Clone

```bash
cd /path/to/your/src
git clone <repository-url> cmatrix
cd cmatrix
```

## Dependencies (Ubuntu)

Run the dependency installer script:

```bash
bash data/install-dependencies.sh
```

## Compile via CMake

```bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
```

## Install

The install step may require `sudo` because it installs system fonts / refreshes font cache:

```bash
sudo cmake --install .
```

After install, update your terminal *profile font* to the patched `DejaVu Sans Mono` (your “DejaVuSansMono”).
If your terminal profile is set to a different font, `cmatrix` will run but the patched glyphs may not render correctly.

## Execution flags overview

Run `cmatrix` with options like:

```bash
cmatrix -a -b -T green -H white -F 23.976
```

Common flags (from `cmatrix -h` / `cmatrix.1`):

- `-a` : asynchronous scroll
- `-b` : bold characters on
- `-B` : all bold characters (overrides `-b`)
- `-c` : “Japanese mode” (draws Matrix Code glyphs; needs a proper UTF-8 terminal + installed Matrix Code font)
- `-f` : force Linux `$TERM` type to be on
- `-l` : Linux mode (console: uses `setfont/consolechars`; you must install a console font for `-l`)
- `-o` : old-style scrolling
- `-L` : locks cmatrix (cannot quit)
- `-k` : every character change
- `-n` : no bold characters (overrides `-b`)
- `-s` : screensaver mode (exits on first keystroke)
- `-V` : print version and exit
- `-F fps` : frame rate (allowed range: 1 to 24; default ~23.976)
- `-T color` : tail color (default green). Valid: `green, red, blue, white, yellow, cyan, magenta, black`
- `-H color` : drop head color (default white). Valid: `green, red, blue, white, yellow, cyan, magenta, black`
- `-O color` : message color (default red). Valid: `green, red, blue, white, yellow, cyan, magenta, black`
- `-M message` : add a centered message

Environment variable:

- `CMATRIX_ASCII_FALLBACK=1` : forces `-c` to use ASCII placeholders (`#0-9A-F`) instead of the Matrix Code glyphs.

## Uninstall script location

After installation, the repo-provided uninstall script will be copied to:

`~/.local/share/cmatrix/uninstall.sh`

This script restores the original system `DejaVuSansMono.ttf` from `~/.local/share/cmatrix/backup/` and refreshes the system font cache.