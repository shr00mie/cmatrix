#!/usr/bin/env bash
# Step 2 of 2: OS-specific build, install binary/man/data, and user fonts (see case on UNAME).
#
# - Prerequisite: ./scripts/01_check_install_deps.sh (Ubuntu: apt via sudo when not root).
# - Invoked by the Makefile as: ./scripts/02_compile_install_bin.sh install|uninstall
#   with PREFIX and DESTDIR set.
# - Run directly from repo root: ./scripts/02_compile_install_bin.sh [install|uninstall]
#   Default mode is install (runs `make`, then installs).
#
# Honors PREFIX, DESTDIR. Linux and macOS default PREFIX is ~/.local (binary under PREFIX/bin).
# Fonts: Linux → ~/.local/share/fonts/DejaVuSansMono.ttf + fc-cache; macOS → ~/Library/Fonts.
# Re-runs with sudo when PREFIX/bin is not user-writable. Packaging: DESTDIR skips auto-sudo.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT_SELF="${REPO_ROOT}/scripts/02_compile_install_bin.sh"
cd "$REPO_ROOT"

MODE="${1:-install}"
DESTDIR="${DESTDIR:-}"
UNAME="$(uname -s)"
case "${UNAME}" in
  Darwin|Linux) PREFIX="${PREFIX:-${HOME}/.local}" ;;
  *) PREFIX="${PREFIX:-/usr/local}" ;;
esac

log_ts() {
  date '+%Y-%m-%dT%H:%M:%S%z' 2>/dev/null || printf '%s' '--'
}

log_info() {
  printf '[cmatrix-install] %s INFO  %s\n' "$(log_ts)" "$*" >&2
}

log_warn() {
  printf '[cmatrix-install] %s WARN  %s\n' "$(log_ts)" "$*" >&2
}

log_error() {
  printf '[cmatrix-install] %s ERROR %s\n' "$(log_ts)" "$*" >&2
}

trap 'ec=$?; log_error "Command failed (exit ${ec}) at line ${LINENO}: ${BASH_COMMAND}"' ERR

path_tree_writable() {
  local target="$1"
  local p="$target"
  while [ -n "$p" ] && [ "$p" != "/" ]; do
    if [ -d "$p" ]; then
      [ -w "$p" ] && return 0
      return 1
    fi
    p="$(dirname "$p")"
  done
  return 1
}

BIN_DIR="${DESTDIR}${PREFIX}/bin"
MAN_DIR="${DESTDIR}${PREFIX}/share/man/man1"
DATA_DIR="${DESTDIR}${PREFIX}/share/cmatrix"
LINUX_USER_FONTS="${HOME}/.local/share/fonts"
PATCHED_FONT="${REPO_ROOT}/data/fonts/DejaVuSansMono_patched.ttf"
INSTALL_FONT_HELPER="${REPO_ROOT}/data/fonts/install-dejavu-user-font.sh"
UNINSTALL_SRC="${REPO_ROOT}/data/uninstall.sh"

install_needs_elevation() {
  [ -n "${DESTDIR}" ] && return 1
  [ "$(id -u)" -eq 0 ] && return 1

  if ! path_tree_writable "${BIN_DIR}"; then
    return 0
  fi
  return 1
}

elevate_install() {
  log_warn "Administrator privileges are required to install under ${PREFIX}"
  case "${UNAME}" in
    Darwin|Linux)
      log_warn "Tip: use a user-writable PREFIX (default: ~/.local) to avoid sudo"
      ;;
  esac
  log_info "Re-running this script with sudo -E …"
  exec sudo -E env \
    "PREFIX=${PREFIX}" \
    "DESTDIR=${DESTDIR}" \
    "HOME=${HOME:-}" \
    "PATH=${PATH}" \
    bash "${SCRIPT_SELF}" install
}

uninstall_needs_elevation() {
  [ -n "${DESTDIR}" ] && return 1
  [ "$(id -u)" -eq 0 ] && return 1
  case "${UNAME}" in
    Darwin|Linux)
      local f="${DESTDIR}${PREFIX}/bin/cmatrix"
      if [ -e "$f" ] && [ ! -w "$f" ]; then
        return 0
      fi
      if [ -d "${DESTDIR}${PREFIX}/bin" ] && [ ! -w "${DESTDIR}${PREFIX}/bin" ]; then
        return 0
      fi
      return 1
      ;;
    *)
      path_tree_writable "${BIN_DIR}" || return 0
      return 1
      ;;
  esac
}

elevate_uninstall() {
  log_warn "Administrator privileges required to remove files under ${PREFIX}/bin"
  log_info "Re-running this script with sudo -E …"
  exec sudo -E env \
    "PREFIX=${PREFIX}" \
    "DESTDIR=${DESTDIR}" \
    "HOME=${HOME:-}" \
    "PATH=${PATH}" \
    bash "${SCRIPT_SELF}" uninstall
}

ensure_cmatrix_built() {
  if [ -f "${REPO_ROOT}/cmatrix" ]; then
    return 0
  fi
  if [ ! -f "${REPO_ROOT}/Makefile" ]; then
    log_error "Cannot build: Makefile not found in ${REPO_ROOT}"
    exit 1
  fi
  if ! command -v make >/dev/null 2>&1; then
    log_error "Cannot build: make not found in PATH (install build tools, then retry)"
    exit 1
  fi
  log_info "Built binary not present; running make cmatrix in ${REPO_ROOT}"
  make -C "${REPO_ROOT}" cmatrix
}

install_data_extras() {
  if [ -f "${REPO_ROOT}/data/fonts/Matrix_rain_glyph_grid.png" ]; then
    install -m 644 "${REPO_ROOT}/data/fonts/Matrix_rain_glyph_grid.png" "${DATA_DIR}/"
    log_info "Installed Matrix_rain_glyph_grid.png → ${DATA_DIR}/"
  fi
  if [ -f "${REPO_ROOT}/data/fonts/Matrix_rain_glyph_grid.tsv" ]; then
    install -m 644 "${REPO_ROOT}/data/fonts/Matrix_rain_glyph_grid.tsv" "${DATA_DIR}/"
    log_info "Installed Matrix_rain_glyph_grid.tsv → ${DATA_DIR}/"
  fi
  if [ -f "${REPO_ROOT}/data/fonts/LICENSE" ]; then
    install -m 644 "${REPO_ROOT}/data/fonts/LICENSE" "${DATA_DIR}/"
    log_info "Installed fonts/LICENSE → ${DATA_DIR}/"
  fi
}

do_install() {
  log_info "Starting install (OS=${UNAME} PREFIX=${PREFIX} DESTDIR=${DESTDIR:-<empty>} REPO=${REPO_ROOT})"

  ensure_cmatrix_built
  if [ ! -f "${REPO_ROOT}/cmatrix" ]; then
    log_error "Build failed: ${REPO_ROOT}/cmatrix missing after make"
    exit 1
  fi
  log_info "Using binary: ${REPO_ROOT}/cmatrix"

  log_info "Creating directory ${BIN_DIR}"
  install -d -m 755 "${BIN_DIR}"
  log_info "Installing cmatrix → ${BIN_DIR}/cmatrix"
  install -m 755 "${REPO_ROOT}/cmatrix" "${BIN_DIR}/cmatrix"

  log_info "Creating directory ${MAN_DIR}"
  install -d -m 755 "${MAN_DIR}"
  log_info "Installing cmatrix.1 → ${MAN_DIR}/cmatrix.1"
  install -m 644 "${REPO_ROOT}/cmatrix.1" "${MAN_DIR}/cmatrix.1"

  log_info "Creating directory ${DATA_DIR}"
  install -d -m 755 "${DATA_DIR}"

  if [ -f "${INSTALL_FONT_HELPER}" ]; then
    install -m 755 "${INSTALL_FONT_HELPER}" "${DATA_DIR}/install-dejavu-user-font.sh"
    log_info "Installed install-dejavu-user-font.sh → ${DATA_DIR}/"
  fi
  if [ -f "${UNINSTALL_SRC}" ]; then
    install -m 755 "${UNINSTALL_SRC}" "${DATA_DIR}/uninstall.sh"
    log_info "Installed uninstall.sh → ${DATA_DIR}/"
  fi
  if [ "${UNAME}" = "Darwin" ] && [ -f "${REPO_ROOT}/data/macos/set-terminal-font.sh" ]; then
    install -m 755 "${REPO_ROOT}/data/macos/set-terminal-font.sh" "${DATA_DIR}/set-terminal-font.sh"
    log_info "Installed set-terminal-font.sh → ${DATA_DIR}/"
  fi
  install_data_extras

  case "${UNAME}" in
    Linux)
      if [ -f "${PATCHED_FONT}" ]; then
        if [ -z "${DESTDIR}" ]; then
          log_info "Installing patched DejaVu → ${LINUX_USER_FONTS}/DejaVuSansMono.ttf"
          install -d -m 755 "${LINUX_USER_FONTS}"
          install -m 644 "${PATCHED_FONT}" "${LINUX_USER_FONTS}/DejaVuSansMono.ttf"
          log_info "Patched DejaVu installed"
          if command -v fc-cache >/dev/null 2>&1; then
            log_info "Refreshing font cache: fc-cache -fv ${LINUX_USER_FONTS}/"
            fc-cache -fv "${LINUX_USER_FONTS}/" || log_warn "fc-cache failed (restart the terminal or log out/in if the font does not appear)"
          else
            log_warn "fc-cache not found in PATH; install fontconfig or restart the session to pick up the new font"
          fi
        else
          dest_font="${DESTDIR}${PREFIX}/share/cmatrix/DejaVuSansMono_patched.ttf"
          log_info "Packaging: staging patched font → ${dest_font}"
          install -m 644 "${PATCHED_FONT}" "${dest_font}"
          log_info "Staged OK"
        fi
      else
        log_warn "Patched font missing: ${PATCHED_FONT} (skipped)"
      fi
      ;;
    Darwin)
      if [ -f "${PATCHED_FONT}" ] && [ -z "${DESTDIR}" ]; then
        user_font_dir="${HOME}/Library/Fonts"
        log_info "Installing patched font → ${user_font_dir}/DejaVuSansMono.ttf"
        mkdir -p "${user_font_dir}"
        cp -f "${PATCHED_FONT}" "${user_font_dir}/DejaVuSansMono.ttf"
        log_info "User font install complete"
        log_info "Optional: set Terminal profile — ${DATA_DIR}/set-terminal-font.sh"
      elif [ -f "${PATCHED_FONT}" ] && [ -n "${DESTDIR}" ]; then
        dest_font="${DESTDIR}${PREFIX}/share/cmatrix/DejaVuSansMono_patched.ttf"
        log_info "Packaging: staging patched font → ${dest_font}"
        install -m 644 "${PATCHED_FONT}" "${dest_font}"
        log_info "Staged OK"
      else
        log_warn "Patched font missing: ${PATCHED_FONT} (macOS user font step skipped)"
      fi
      ;;
    *)
      log_warn "Unknown OS ${UNAME}; only binary, man page, and data dir were installed"
      ;;
  esac

  log_info "Install finished successfully (PREFIX=${PREFIX} DESTDIR=${DESTDIR:-})"
}

do_uninstall() {
  log_info "Starting uninstall (OS=${UNAME} PREFIX=${PREFIX} DESTDIR=${DESTDIR:-<empty>})"

  case "${UNAME}" in
    Darwin)
      log_info "Running data/uninstall.sh (user font + common binary paths)"
      bash "${REPO_ROOT}/data/uninstall.sh"
      log_info "Removing ${DESTDIR}${PREFIX}/bin/cmatrix if present"
      rm -f "${DESTDIR}${PREFIX}/bin/cmatrix"
      ;;
    Linux)
      if [ -f "${REPO_ROOT}/data/uninstall.sh" ]; then
        log_info "Running data/uninstall.sh (user font + common binary paths)"
        bash "${REPO_ROOT}/data/uninstall.sh" || log_warn "uninstall.sh returned non-zero"
      else
        log_warn "Missing ${REPO_ROOT}/data/uninstall.sh"
      fi
      log_info "Removing ${DESTDIR}${PREFIX}/bin/cmatrix if present"
      rm -f "${DESTDIR}${PREFIX}/bin/cmatrix"
      ;;
    *)
      log_info "Removing ${DESTDIR}${PREFIX}/bin/cmatrix if present"
      rm -f "${DESTDIR}${PREFIX}/bin/cmatrix"
      ;;
  esac

  log_info "Uninstall finished"
}

case "${MODE}" in
  install)
    log_info "Mode=install (effective UID $(id -u))"
    if install_needs_elevation; then
      elevate_install
    else
      log_info "Elevation not required for this install path"
    fi
    if [ "$(id -u)" -eq 0 ]; then
      printf '%s\n' "Warning: running as root; files go under /root/.local. For a normal user install, run without sudo." >&2
    fi
    echo "=== cmatrix: build ==="
    make
    echo "=== cmatrix: install ==="
    do_install
    echo ""
    echo "Install finished. Add ~/.local/bin to PATH if needed:"
    echo '  export PATH="$HOME/.local/bin:$PATH"'
    ;;
  uninstall)
    log_info "Mode=uninstall (effective UID $(id -u))"
    if uninstall_needs_elevation; then
      elevate_uninstall
    else
      log_info "Elevation not required for this uninstall path"
    fi
    do_uninstall
    ;;
  *)
    log_error "Usage: $0 [install|uninstall]"
    exit 1
    ;;
esac
