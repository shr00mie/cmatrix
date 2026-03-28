# cMatrix
[![CMake on a single platform](https://github.com/shr00mie/cmatrix/actions/workflows/cmake-single-platform.yml/badge.svg)](https://github.com/shr00mie/cmatrix/actions/workflows/cmake-single-platform.yml)

For the time being, Ubuntu only.

## Digital Rain Demo
[Demo](https://github.com/user-attachments/assets/3178be08-7594-43bf-ab6d-fffcdf64601c)

## Custom head and tail colors via string or hex
<img width="832" height="438" alt="image" src="https://github.com/user-attachments/assets/25f57797-aa10-427a-b8ce-79cd7c850c78" />

## Clone

```bash
cd /path/to/your/src
git clone https://github.com/shr00mie/cmatrix.git
cd cmatrix
```

## Dependencies (Ubuntu)

Run the dependency installer script:

```bash
./data/install-dependencies.sh
```

This installs the toolchain and runtime/build deps for the Notcurses renderer, including:

- `build-essential`
- `cmake`
- `pkg-config`
- `libnotcurses-dev`
- `fontconfig`
- `fonts-dejavu-core`
- `python3`

## Compile via CMake

```bash
mkdir -p build
cd build
cmake ..
make
```

## Install

The install step may require `sudo` because it installs system fonts / refreshes font cache:

```bash
sudo make install
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

## Uninstall script location

After installation, the repo-provided uninstall script will be copied to:

`~/.local/share/cmatrix/uninstall.sh`

This script restores the original system `DejaVuSansMono.ttf` from `~/.local/share/cmatrix/backup/` and refreshes the system font cache.

## ToDo:
* [] tweak init sequence to make drop spawn more parabolic.
* [] migratory messages; appear in random locations. appear. stick around for like 10 seconds, get wiped, then get revealed somewhere else on screen.
* [] play around with total active columns and tail lengths a bit more. they're close, but not quite where i want them.
* [] mess with how drops spawn in columns with existing tails and how the squeegee fade works with various length sliding windows.
* [] update ubuntu installer to query gnome term profile for current font, and patch user's preffered font with matrix glyphs.
* [] figure out how to get this to work with putty.
* [] clone Emily Blunt.
