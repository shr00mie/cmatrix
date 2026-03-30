# cMatrix

[![ubuntu-22.04](https://github.com/shr00mie/cmatrix/actions/workflows/ubuntu-22.04.yml/badge.svg?branch=master)](https://github.com/shr00mie/cmatrix/actions/workflows/ubuntu-22.04.yml)
[![ubuntu-24.04](https://github.com/shr00mie/cmatrix/actions/workflows/ubuntu-24.04.yml/badge.svg?branch=master)](https://github.com/shr00mie/cmatrix/actions/workflows/ubuntu-24.04.yml)
[![macos-14](https://github.com/shr00mie/cmatrix/actions/workflows/macos-14.yml/badge.svg?branch=master)](https://github.com/shr00mie/cmatrix/actions/workflows/macos-14.yml)
[![macos-15](https://github.com/shr00mie/cmatrix/actions/workflows/macos-15.yml/badge.svg?branch=master)](https://github.com/shr00mie/cmatrix/actions/workflows/macos-15.yml)

Supported on **Ubuntu** (Linux) and **macOS** (Intel and Apple silicon).

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

## Setup (two steps)

| Script | Role |
|--------|------|
| **`./scripts/01_check_install_deps.sh`** | System packages (compilers, Notcurses, etc.). |
| **`./scripts/02_compile_install_bin.sh`** | **`make`**, then install under **`PREFIX`** (default **`~/.local`**): binary, man page, data, fonts. |

**`sudo`** is only for **step 1** on Ubuntu when **`apt`** runs as a non-root user. **Step 2** / **`make install`** does not need **`sudo`** for a normal **`~/.local`** install. Details, package lists, and edge cases are in the scripts.

```bash
./scripts/01_check_install_deps.sh
./scripts/02_compile_install_bin.sh
```

## Default paths

Default **`PREFIX`** is **`~/.local`**.

| Artifact | Linux | macOS |
|----------|-------|--------|
| Binary | **`~/.local/bin/cmatrix`** | same |
| Man page | **`~/.local/share/man/man1/cmatrix.1`** | same |
| Data (uninstall helper, optional scripts) | **`~/.local/share/cmatrix/`** | same |
| Patched DejaVu (rain PUA glyphs) | **`~/.local/share/fonts/DejaVuSansMono.ttf`** (from **`data/fonts/DejaVuSansMono_patched.ttf`**) + **`fc-cache -fv ~/.local/share/fonts/`** | **`~/Library/Fonts/DejaVuSansMono.ttf`** |

```bash
export PATH="$HOME/.local/bin:$PATH"
```

**macOS font helper:** **`$HOME/.local/share/cmatrix/set-terminal-font.sh`** (see also **`data/macos/`**). Use **DejaVu Sans Mono** in the terminal profile for correct glyphs.

## Execution flags overview

Run `cmatrix` with options like:

```bash
cmatrix -a -b -T green -H white -F 24
```

Common flags (from `cmatrix -h` / `cmatrix.1`):

- `-a` : asynchronous scroll
- `-b` : bold characters on
- `-B` : all bold characters (overrides `-b`)
- `-c` : matrix glyphs mode (movie-style halfwidth column stream + Latin; needs UTF-8 and a terminal font that includes those code points)
- `-o` : old-style scrolling
- `-k` : every character change
- `-s` : screensaver mode (exits on first keystroke)
- `-V` : print version and exit
- `-F fps` : frame rate (allowed range: 12–60; default 24)
- `-T`, `-H`, `-O` : each takes a color as a **name** (string) or **hex** (`#RRGGBB`). Defaults: tail green, head mint, message red.
- `-M message` : add a centered message

## ToDo:
- [ ] tweak init sequence to make drop spawn more parabolic.
- [ ] migratory messages; appear in random locations. appear. stick around for like 10 seconds, get wiped, then get revealed somewhere else on screen.
- [ ] play around with total active columns and tail lengths a bit more. they're close, but not quite where i want them.
- [ ] mess with how drops spawn in columns with existing tails and how the squeegee fade works with various length sliding windows.
- [ ] update Ubuntu helper to query GNOME Terminal profile font and patch preferred font with rain PUA glyphs.
- [X] modify patched font to pass PuTTY filter.
- [X] verify that cmatrix running on remote box via PuTTY displays properly on windows.
<img width="661" height="394" alt="image" src="https://github.com/user-attachments/assets/28387ada-a197-42bd-99de-db937a8b352f" />

- [ ] automate finding, enumerating, and editing PuTTY font and Terminal-type string config via powershell script.
- [ ] clone Emily Blunt.

## Uninstall

```bash
make uninstall
```

Uses **`02`** in uninstall mode with your **`PREFIX`** / **`DESTDIR`** (see script).

## Uninstall helper and optional system-wide font

After **`make install`**, a copy of the uninstall helper lives at:

**`~/.local/share/cmatrix/uninstall.sh`**

You can run it directly; **`make uninstall`** also invokes the logic from the repo via the install script.

**`data/fonts/install-dejavu-user-font.sh`** is **not** part of the default two-step flow. Use it only if you want to **replace the distro’s system DejaVu** under **`/usr/share/fonts/...`** (requires **root**). Normal installs use **user fonts** only (**`~/.local/share/fonts/`** on Linux).

## Attribution

This work builds on [cmatrix](https://github.com/abishekvashok/cmatrix), maintained by [Abishek V Ashok](https://github.com/abishekvashok) and contributors.
