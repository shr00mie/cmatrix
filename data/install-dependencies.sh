#!/usr/bin/env bash
#
# Install dependencies required to compile and run `cmatrix` on supported distros.
#
# Build (matches CMakeLists.txt: C compiler, CMake, pkg-config, Notcurses.pc + dev headers, pthread via libc):
#   build-essential, cmake, pkg-config, libnotcurses-dev
#
# Runtime / install UX (fonts, fontconfig): fontconfig, fonts-dejavu-core
#
# Optional (font/grid Python tooling under data/fonts/, not needed for cmake .. && make):
#   python3
#
# Currently implemented for Ubuntu.

set -euo pipefail

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
  echo "Unsupported OS. This script currently only supports Ubuntu." >&2
  echo "Detected: ID='${OS_ID}' ID_LIKE='${OS_LIKE}'" >&2
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
  cmake
  pkg-config
  fontconfig
  python3
  fonts-dejavu-core
  libnotcurses-dev
)

echo "Installing dependencies:"
printf '  - %s\n' "${PACKAGES[@]}"

${SUDO} apt-get install -y "${PACKAGES[@]}"

echo "Dependency installation complete."

