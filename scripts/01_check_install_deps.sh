#!/usr/bin/env bash
#
# Step 1 of 2: OS detection + system dependencies only (sudo on Ubuntu for apt when not root).
# Does NOT compile cmatrix or install the binary/fonts — use ./scripts/02_compile_install_bin.sh for that.
#
# Ubuntu: uses sudo with apt when not root (see install_ubuntu).
# macOS: Homebrew packages; Apple Clang from CLT/Xcode. brew install does not use sudo;
#   the Homebrew installer may prompt for your password.
#
# Step 2 (no sudo): from the repo root run:
#   ./scripts/02_compile_install_bin.sh
#
set -euo pipefail

UNAME="$(uname -s)"

# Load Homebrew into this shell when installed in the default locations (Apple Silicon / Intel).
brew_shellenv() {
  if command -v brew >/dev/null 2>&1; then
    eval "$(brew shellenv)"
    return 0
  fi
  local b
  for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do
    if [ -x "$b" ]; then
      eval "$("$b" shellenv)"
      return 0
    fi
  done
  return 1
}

install_macos() {
  echo "=== macOS build dependencies for cmatrix ==="
  echo ""
  echo "1) Apple Clang (compiler)"
  echo "   Install Xcode Command Line Tools (opens a GUI prompt; Apple ID may be required):"
  echo "     xcode-select --install"
  echo "   Or install full Xcode from the Mac App Store, then point the active developer dir:"
  echo "     sudo xcode-select -s /Applications/Xcode.app/Contents/Developer"
  echo "   (The sudo line is only if you use full Xcode; CLT-only installs rarely need it.)"
  echo ""

  if ! xcode-select -p >/dev/null 2>&1; then
    echo "Xcode Command Line Tools are not selected. Run xcode-select --install first." >&2
    exit 1
  fi
  if ! command -v xcrun >/dev/null 2>&1; then
    echo "xcrun not found. Install Xcode Command Line Tools: xcode-select --install" >&2
    exit 1
  fi
  if ! xcrun clang --version 2>/dev/null | head -1 | grep -q 'Apple clang'; then
    echo "WARN: xcrun clang does not report as Apple Clang; install CLT or Xcode." >&2
  fi
  echo "   OK: $(xcrun clang --version | head -1)"
  echo ""

  echo "2) Homebrew + Notcurses (headers/libs, pkg-config metadata)"
  echo "   Homebrew installs under your user or /opt/homebrew — brew install does NOT use sudo."
  echo "   If brew is missing, install from https://brew.sh (the installer may ask for your password)."
  echo ""

  if ! brew_shellenv; then
    echo "Homebrew not found. Install from https://brew.sh e.g.:" >&2
    echo '  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"' >&2
    exit 1
  fi
  echo "   OK: $(command -v brew) ($(brew --prefix 2>/dev/null || echo unknown))"
  echo ""

  echo "Installing packages: pkgconf notcurses"
  brew install pkgconf notcurses

  brew_shellenv
  if ! pkg-config --exists notcurses; then
    echo "pkg-config still cannot see notcurses. Add Homebrew to your shell profile, e.g.:" >&2
    echo '  eval "$(brew shellenv)"' >&2
    exit 1
  fi
  echo "   OK: pkg-config notcurses → $(pkg-config --modversion notcurses)"
  echo ""
  echo "Before running make, ensure brew is on PATH in this terminal (same line as above)."
  echo "Dependency installation complete (macOS)."
  echo "Next: ./scripts/02_compile_install_bin.sh"
}

install_ubuntu() {
  if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
  fi
  OS_ID="${ID:-}"
  OS_LIKE="${ID_LIKE:-}"
  is_ubuntu() {
    [ "${OS_ID}" = "ubuntu" ] || [[ "${OS_LIKE}" == *"ubuntu"* ]]
  }
  if ! is_ubuntu; then
    echo "Unsupported Linux distro for this script. Detected: ID='${OS_ID}' ID_LIKE='${OS_LIKE}'" >&2
    exit 1
  fi

  SUDO=""
  if [ "$(id -u)" -ne 0 ]; then
    SUDO="sudo"
  fi

  echo "Updating apt package lists..."
  ${SUDO} apt-get update

  PACKAGES=(
    build-essential
    clang
    pkg-config
    fontconfig
    python3
    fonts-dejavu-core
    libnotcurses-dev
  )

  echo "Installing dependencies (requires sudo for apt when not root):"
  printf '  - %s\n' "${PACKAGES[@]}"

  ${SUDO} apt-get install -y "${PACKAGES[@]}"

  echo "Dependency installation complete (Ubuntu)."
  echo "Next: ./scripts/02_compile_install_bin.sh"
}

case "${UNAME}" in
  Darwin) install_macos ;;
  Linux) install_ubuntu ;;
  *)
    echo "Unsupported OS: ${UNAME}" >&2
    exit 1
    ;;
esac
