#!/usr/bin/env bash
# Uninstall CMatrix user-installed font and remove the cmatrix binary.
#
# Installed by `make install` into PREFIX/share/cmatrix/uninstall.sh.
#
# Linux: removes ~/.local/share/fonts/DejaVuSansMono.ttf, refreshes user font cache, removes binaries.
# macOS: removes ~/Library/Fonts/DejaVuSansMono.ttf and binary paths; no root.

set -euo pipefail

if [ "$(uname -s)" = "Darwin" ]; then
  user_font="${HOME}/Library/Fonts/DejaVuSansMono.ttf"
  if [ -f "${user_font}" ]; then
    rm -f "${user_font}"
    echo "Removed ${user_font}"
  fi
  for p in \
    "$(command -v cmatrix 2>/dev/null || true)" \
    "${HOME}/.local/bin/cmatrix" \
    "/usr/local/bin/cmatrix" \
    "/opt/homebrew/bin/cmatrix"; do
    if [ -n "${p}" ] && [ -f "${p}" ]; then
      rm -f "${p}"
      echo "Removed ${p}"
    fi
  done
  echo "macOS uninstall done. Terminal.app font was not reverted automatically."
  exit 0
fi

if [ "$(uname -s)" = "Linux" ]; then
  user_font="${HOME}/.local/share/fonts/DejaVuSansMono.ttf"
  if [ -f "${user_font}" ]; then
    rm -f "${user_font}"
    echo "Removed ${user_font}"
  fi
  if command -v fc-cache >/dev/null 2>&1; then
    fc-cache -fv "${HOME}/.local/share/fonts/" >/dev/null 2>&1 || true
  fi
  for p in \
    "$(command -v cmatrix 2>/dev/null || true)" \
    "${HOME}/.local/bin/cmatrix" \
    "/usr/local/bin/cmatrix" \
    "/usr/bin/cmatrix"; do
    if [ -n "${p}" ] && [ -f "${p}" ]; then
      rm -f "${p}"
      echo "Removed ${p}"
    fi
  done
  echo "Linux uninstall done."
  exit 0
fi

echo "Error: this uninstall helper supports Darwin and Linux only." >&2
exit 1
