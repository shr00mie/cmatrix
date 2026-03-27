#!/usr/bin/env bash
# Uninstall CMatrix DejaVu Sans Mono patch and remove the cmatrix binary.
#
# This script is installed during `make install` / `cmake --install` into:
#   ~/.local/share/cmatrix/uninstall.sh
#
# It restores DejaVuSansMono.ttf from the backup created by the installer.

set -euo pipefail

TARGET_HOME="${HOME:-}"
if [ -n "${SUDO_USER:-}" ] && [ -d "/home/${SUDO_USER}" ]; then
  TARGET_HOME="/home/${SUDO_USER}"
fi

STATE_DIR="${TARGET_HOME}/.local/share/cmatrix"
BACKUP_DIR="${STATE_DIR}/backup"
BACKUP_FONT="${BACKUP_DIR}/DejaVuSansMono_original.ttf"

if [ "$(id -u)" -ne 0 ]; then
  echo "Error: uninstall requires root privileges to modify system fonts." >&2
  echo "Run: sudo ${0}" >&2
  exit 1
fi

if [ ! -f "${BACKUP_FONT}" ]; then
  echo "Error: backup font not found at: ${BACKUP_FONT}" >&2
  echo "Did you run the installer, or is this the right user?" >&2
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

install -m 644 "${BACKUP_FONT}" "${SYSTEM_DEJAVU_TTF}"

# Remove binary from the most likely locations.
BIN_CANDIDATES="$(command -v cmatrix 2>/dev/null || true)"
for p in \
  "${BIN_CANDIDATES}" \
  "/usr/local/bin/cmatrix" \
  "/usr/bin/cmatrix"; do
  if [ -n "${p}" ] && [ -f "${p}" ]; then
    rm -f "${p}"
  fi
done

if command -v fc-cache >/dev/null 2>&1; then
  SYSTEM_FONT_DIR="$(dirname "${SYSTEM_DEJAVU_TTF}")"
  fc-cache -f -v "${SYSTEM_FONT_DIR}" >/dev/null 2>&1 || true
fi

echo "Uninstalled CMatrix: restored DejaVuSansMono.ttf and removed cmatrix binary."
