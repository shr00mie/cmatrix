#!/usr/bin/env bash
#
# Install dependencies required to compile and run `cmatrix` on supported distros.
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

# ncurses dev package name varies across Ubuntu releases.
NCURSES_DEV_PKG="libncursesw5-dev"
if ! apt-cache show "${NCURSES_DEV_PKG}" >/dev/null 2>&1; then
  NCURSES_DEV_PKG="libncurses-dev"
fi

PACKAGES=(
  build-essential
  cmake
  pkg-config
  fontconfig
  python3
  fonts-dejavu-core
  libncursesw6
  "${NCURSES_DEV_PKG}"
)

echo "Installing dependencies:"
printf '  - %s\n' "${PACKAGES[@]}"

${SUDO} apt-get install -y "${PACKAGES[@]}"

echo "Dependency installation complete."

