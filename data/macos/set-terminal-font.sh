#!/usr/bin/env bash
# Set Terminal.app default profile font to DejaVu Sans Mono (after patched TTF is in ~/Library/Fonts).
# Requires the font installed as ~/Library/Fonts/DejaVuSansMono.ttf (see make install on macOS).
# Uses AppleScript; validate with Font Book / Terminal if the profile name differs.
set -euo pipefail

if [ "$(uname -s)" != "Darwin" ]; then
  echo "This script is for macOS only." >&2
  exit 1
fi

FONT_FILE="${HOME}/Library/Fonts/DejaVuSansMono.ttf"
if [ ! -f "${FONT_FILE}" ]; then
  echo "Missing ${FONT_FILE}. Run 'make install' from cmatrix or copy DejaVuSansMono_patched.ttf there as DejaVuSansMono.ttf." >&2
  exit 1
fi

PROFILE_NAME="$(defaults read com.apple.Terminal "Default Window Settings" 2>/dev/null || true)"
if [ -z "${PROFILE_NAME}" ]; then
  PROFILE_NAME="Basic"
fi

FONT_NAME="DejaVu Sans Mono"
FONT_SIZE=12

osascript - "$PROFILE_NAME" "$FONT_NAME" "${FONT_SIZE}" <<'OSA'
on run argv
  set profileName to item 1 of argv
  set fontName to item 2 of argv
  set fontSz to (item 3 of argv) as integer
  tell application "Terminal"
    try
      set font name of settings set profileName to fontName
      set font size of settings set profileName to fontSz
    on error
      set font name of settings set "Basic" to fontName
      set font size of settings set "Basic" to fontSz
    end try
  end tell
end run
OSA

echo "Set Terminal profile '${PROFILE_NAME}' (fallback: Basic) font to ${FONT_NAME} ${FONT_SIZE}pt."
echo "Quit and reopen Terminal if the change does not appear."
