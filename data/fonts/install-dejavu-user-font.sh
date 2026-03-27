#!/usr/bin/env bash
# Install CMatrix DejaVu Sans Mono patch (system-wide) with user-local backup.
#
# What this does:
# 1) Ensure ~/.local/share/cmatrix exists for the *real* invoking user.
# 2) Ensure the backup directory contains the original DejaVuSansMono.ttf.
# 3) Overwrite system DejaVuSansMono.ttf with data/fonts/DejaVuSansMono_patched.ttf.
# 4) Refresh system font cache (fc-cache).
# 5) Copy the repo's uninstall script into ~/.local/share/cmatrix/uninstall.sh.
#
# Run this script with root privileges (via sudo) for the system font overwrite.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DATA_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# If DESTDIR is set (packaging scenario), don't touch host system fonts.
if [ -n "${DESTDIR:-}" ]; then
  echo "Skipping system font modifications because DESTDIR is set: ${DESTDIR}"
  exit 0
fi

TARGET_HOME="${HOME:-}"
if [ -n "${SUDO_USER:-}" ] && [ -d "/home/${SUDO_USER}" ]; then
  TARGET_HOME="/home/${SUDO_USER}"
fi

STATE_DIR="${TARGET_HOME}/.local/share/cmatrix"
BACKUP_DIR="${STATE_DIR}/backup"

mkdir -p "${STATE_DIR}"
mkdir -p "${BACKUP_DIR}"

PATCHED_FONT="${SCRIPT_DIR}/DejaVuSansMono_patched.ttf"
if [ ! -f "${PATCHED_FONT}" ]; then
  echo "Error: patched font not found: ${PATCHED_FONT}" >&2
  exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
  echo "Error: installation requires root privileges to modify system fonts." >&2
  echo "Run: sudo ${0}" >&2
  exit 1
fi

SYSTEM_DEJAVU_TTF="$(fc-match -f '%{file}' 'DejaVu Sans Mono' 2>/dev/null || true)"
if [ -z "${SYSTEM_DEJAVU_TTF}" ] || [ ! -f "${SYSTEM_DEJAVU_TTF}" ]; then
  for candidate in \
    "/usr/share/fonts/truetype/DejaVuSansMono.ttf" \
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf" \
    "/usr/local/share/fonts/DejaVuSansMono.ttf"; do
    if [ -f "${candidate}" ]; then
      SYSTEM_DEJAVU_TTF="${candidate}"
      break
    fi
  done
fi

if [ -z "${SYSTEM_DEJAVU_TTF}" ] || [ ! -f "${SYSTEM_DEJAVU_TTF}" ]; then
  echo "Error: could not find system DejaVu Sans Mono font (DejaVuSansMono.ttf)." >&2
  exit 1
fi

BACKUP_FONT="${BACKUP_DIR}/DejaVuSansMono_original.ttf"
SYSTEM_SHA=""
BACKUP_SHA=""

if command -v sha256sum >/dev/null 2>&1; then
  SYSTEM_SHA="$(sha256sum "${SYSTEM_DEJAVU_TTF}" | awk '{print $1}')"
fi

if [ ! -f "${BACKUP_FONT}" ]; then
  echo "Backing up original DejaVuSansMono.ttf to: ${BACKUP_FONT}"
  cp -f "${SYSTEM_DEJAVU_TTF}" "${BACKUP_FONT}"
elif [ -n "${SYSTEM_SHA}" ] && command -v sha256sum >/dev/null 2>&1; then
  BACKUP_SHA="$(sha256sum "${BACKUP_FONT}" | awk '{print $1}')"
  if [ "${BACKUP_SHA}" != "${SYSTEM_SHA}" ]; then
    echo "Backup exists but does not match current system DejaVuSansMono.ttf; re-backing up."
    cp -f "${SYSTEM_DEJAVU_TTF}" "${BACKUP_FONT}"
  fi
fi

echo "Overwriting system DejaVuSansMono.ttf with patched font:"
echo "  System:  ${SYSTEM_DEJAVU_TTF}"
echo "  Patched: ${PATCHED_FONT}"

install -m 644 "${PATCHED_FONT}" "${SYSTEM_DEJAVU_TTF}"

if command -v fc-cache >/dev/null 2>&1; then
  SYSTEM_FONT_DIR="$(dirname "${SYSTEM_DEJAVU_TTF}")"
  fc-cache -f -v "${SYSTEM_FONT_DIR}" >/dev/null 2>&1 || fc-cache -f -v >/dev/null 2>&1 || true
fi

UNINSTALL_SRC="${REPO_DATA_DIR}/uninstall.sh"
UNINSTALL_DEST="${STATE_DIR}/uninstall.sh"

if [ -f "${UNINSTALL_SRC}" ]; then
  cp -f "${UNINSTALL_SRC}" "${UNINSTALL_DEST}"
  chmod +x "${UNINSTALL_DEST}"
else
  echo "Warning: uninstall script not found in repo: ${UNINSTALL_SRC}" >&2
fi

echo "Installed CMatrix font patch."
echo "Run your uninstaller at: ${UNINSTALL_DEST}"
