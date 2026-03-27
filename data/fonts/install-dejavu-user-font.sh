#!/usr/bin/env bash
# Install CMatrix DejaVu Sans Mono patch (system-wide) with user-local backup.
#
# What this does (default / full mode):
# 1) Ensure ~/.local/share/cmatrix exists for the *real* invoking user.
# 2) Ensure the backup directory contains the original DejaVuSansMono.ttf.
# 3) Overwrite system DejaVuSansMono.ttf (usually
#    /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf on Debian/Ubuntu) with
#    the patched font from the repo (DejaVuSansMono_patched.ttf beside this script)
#    or from .../dejavu/DejaVuSansMono.ttf after cmake install (same name as the system file).
# 4) Refresh system font cache (fc-cache).
# 5) Copy the repo's uninstall script into ~/.local/share/cmatrix/uninstall.sh.
#
# CMake install uses --backup-only before installing DejaVuSansMono.ttf into the
# font directory, then --finalize for cache + uninstall helper. The _patched name
# exists only in the repository; on disk under .../truetype/dejavu/ the file is
# always DejaVuSansMono.ttf.
#
# Run this script with root privileges (via sudo) for the system font overwrite.
#
# Usage:
#   install-dejavu-user-font.sh           # full install (typical manual run from repo data/fonts/)
#   install-dejavu-user-font.sh --backup-only   # only backup system font (cmake: run before file install)
#   install-dejavu-user-font.sh --finalize      # fc-cache + uninstall copy (cmake: after file install)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DATA_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

MODE=full
case "${1:-}" in
  --backup-only) MODE=backup ;;
  --finalize) MODE=finalize ;;
  "") MODE=full ;;
  *)
    echo "Usage: $0 [--backup-only|--finalize]" >&2
    exit 1
    ;;
esac

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

if [ "$(id -u)" -ne 0 ]; then
  echo "Error: installation requires root privileges to modify system fonts." >&2
  echo "Run: sudo ${0} $*" >&2
  exit 1
fi

find_system_dejavu() {
  local t
  t="$(fc-match -f '%{file}' 'DejaVu Sans Mono' 2>/dev/null || true)"
  if [ -n "${t}" ] && [ -f "${t}" ]; then
    printf '%s' "${t}"
    return 0
  fi
  for candidate in \
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf" \
    "/usr/share/fonts/truetype/DejaVuSansMono.ttf" \
    "/usr/local/share/fonts/DejaVuSansMono.ttf"; do
    if [ -f "${candidate}" ]; then
      printf '%s' "${candidate}"
      return 0
    fi
  done
  return 1
}

backup_system_font_if_needed() {
  local SYSTEM_DEJAVU_TTF BACKUP_FONT SYSTEM_SHA BACKUP_SHA
  SYSTEM_DEJAVU_TTF="$(find_system_dejavu)" || {
    echo "Error: could not find system DejaVu Sans Mono font (DejaVuSansMono.ttf)." >&2
    return 1
  }

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
}

finalize_install() {
  local SYSTEM_FONT_DIR UNINSTALL_SRC UNINSTALL_DEST
  # Refresh font cache (system path was updated by cmake install or manual copy).
  if command -v fc-match >/dev/null 2>&1; then
    SYSTEM_DEJAVU_TTF="$(find_system_dejavu)" || SYSTEM_DEJAVU_TTF=""
    if [ -n "${SYSTEM_DEJAVU_TTF}" ] && [ -f "${SYSTEM_DEJAVU_TTF}" ]; then
      SYSTEM_FONT_DIR="$(dirname "${SYSTEM_DEJAVU_TTF}")"
      if command -v fc-cache >/dev/null 2>&1; then
        fc-cache -f -v "${SYSTEM_FONT_DIR}" >/dev/null 2>&1 || fc-cache -f -v >/dev/null 2>&1 || true
      fi
    fi
  fi

  UNINSTALL_SRC="${SCRIPT_DIR}/uninstall.sh"
  if [ ! -f "${UNINSTALL_SRC}" ]; then
    UNINSTALL_SRC="${REPO_DATA_DIR}/uninstall.sh"
  fi
  UNINSTALL_DEST="${STATE_DIR}/uninstall.sh"

  if [ -f "${UNINSTALL_SRC}" ]; then
    cp -f "${UNINSTALL_SRC}" "${UNINSTALL_DEST}"
    chmod +x "${UNINSTALL_DEST}"
  else
    echo "Warning: uninstall script not found (tried ${SCRIPT_DIR}/uninstall.sh, ${REPO_DATA_DIR}/uninstall.sh)" >&2
  fi

  echo "Installed CMatrix font patch."
  echo "Run your uninstaller at: ${UNINSTALL_DEST}"
}

if [ "${MODE}" = backup ]; then
  backup_system_font_if_needed
  echo "Backup step complete."
  exit 0
fi

if [ "${MODE}" = finalize ]; then
  finalize_install
  exit 0
fi

# --- full mode ---

backup_system_font_if_needed

# Repo only: DejaVuSansMono_patched.ttf. After cmake install, the patched bytes live as
# .../dejavu/DejaVuSansMono.ttf (no _patched suffix on disk).
PATCHED_FONT="${SCRIPT_DIR}/DejaVuSansMono_patched.ttf"
if [ ! -f "${PATCHED_FONT}" ]; then
  PATCHED_FONT="/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
fi
if [ ! -f "${PATCHED_FONT}" ]; then
  echo "Error: patched font not found (tried ${SCRIPT_DIR}/DejaVuSansMono_patched.ttf and /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf)" >&2
  exit 1
fi

SYSTEM_DEJAVU_TTF="$(find_system_dejavu)" || {
  echo "Error: could not find system DejaVu Sans Mono font (DejaVuSansMono.ttf)." >&2
  exit 1
}

# Same path (e.g. cmake already installed to system path): nothing to copy.
if [ "${PATCHED_FONT}" = "${SYSTEM_DEJAVU_TTF}" ]; then
  echo "Patched font already installed at: ${SYSTEM_DEJAVU_TTF}"
else
  echo "Overwriting system DejaVuSansMono.ttf with patched font:"
  echo "  System:  ${SYSTEM_DEJAVU_TTF}"
  echo "  Source:  ${PATCHED_FONT}"
  install -m 644 "${PATCHED_FONT}" "${SYSTEM_DEJAVU_TTF}"
fi

finalize_install
