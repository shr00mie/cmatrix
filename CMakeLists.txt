# CMake build system for CMatrix (Linux amd64 only)
# Tested with: CMake 3.31+, Notcurses 3.x

cmake_minimum_required(VERSION 3.31)

project(CMatrix LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "CMatrix is supported on Linux only. Detected: ${CMAKE_SYSTEM_NAME}")
endif()
set(VERSION "2.0")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type: Debug, Release, RelWithDebInfo, MinSizeRel" FORCE)
endif()

include(GNUInstallDirs)
set(CMATRIX_FONT_DIR "/usr/share/fonts/truetype/dejavu")
set(CMATRIX_PKG_DATADIR "${CMAKE_INSTALL_DATADIR}/cmatrix")

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/config.h")
    set(CMATRIX_EXCLUDE_CONFIG_H 1)
    message(STATUS "config.h absent — building without it (EXCLUDE_CONFIG_H)")
else()
    message(STATUS "config.h present")
endif()

include(CheckIncludeFiles)
check_include_files("sys/ioctl.h" HAVE_SYS_IOCTL_H)
check_include_files("unistd.h" HAVE_UNISTD_H)
check_include_files("termios.h" HAVE_TERMIOS_H)
check_include_files("termio.h" HAVE_TERMIO_H)
check_include_files("getopt.h" HAVE_GETOPT_H)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NOTCURSES REQUIRED IMPORTED_TARGET notcurses)

find_package(Threads REQUIRED)

add_executable(cmatrix src/cmatrix.c src/util.c)
target_include_directories(cmatrix PRIVATE
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
)

target_compile_definitions(cmatrix PRIVATE VERSION="${VERSION}")
if(CMATRIX_EXCLUDE_CONFIG_H)
    target_compile_definitions(cmatrix PRIVATE EXCLUDE_CONFIG_H)
endif()
if(HAVE_SYS_IOCTL_H)
    target_compile_definitions(cmatrix PRIVATE HAVE_SYS_IOCTL_H)
endif()
if(HAVE_UNISTD_H)
    target_compile_definitions(cmatrix PRIVATE HAVE_UNISTD_H)
endif()
if(HAVE_TERMIOS_H)
    target_compile_definitions(cmatrix PRIVATE HAVE_TERMIOS_H)
endif()
if(HAVE_TERMIO_H)
    target_compile_definitions(cmatrix PRIVATE HAVE_TERMIO_H)
endif()
if(HAVE_GETOPT_H)
    target_compile_definitions(cmatrix PRIVATE HAVE_GETOPT_H)
endif()

target_link_libraries(cmatrix PRIVATE
  PkgConfig::NOTCURSES
  m
  Threads::Threads
)

install(TARGETS cmatrix DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES cmatrix.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

set(CMATRIX_DEJAVU_PATCHED "${CMAKE_SOURCE_DIR}/data/fonts/DejaVuSansMono_patched.ttf")

if(EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/old/Matrix-Code-mono-pua.ttf")
  install(FILES "${CMAKE_SOURCE_DIR}/data/fonts/old/Matrix-Code-mono-pua.ttf"
          DESTINATION ${CMATRIX_FONT_DIR} RENAME Matrix-Code.ttf)
elseif(EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/old/Matrix-Code-mono.ttf")
  install(FILES "${CMAKE_SOURCE_DIR}/data/fonts/old/Matrix-Code-mono.ttf"
          DESTINATION ${CMATRIX_FONT_DIR} RENAME Matrix-Code.ttf)
elseif(EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/old/Matrix-Code.ttf")
  install(FILES "${CMAKE_SOURCE_DIR}/data/fonts/old/Matrix-Code.ttf"
          DESTINATION ${CMATRIX_FONT_DIR})
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/Matrix-Code-NFI.ttf")
  install(FILES "${CMAKE_SOURCE_DIR}/data/fonts/Matrix-Code-NFI.ttf"
          DESTINATION ${CMATRIX_FONT_DIR})
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/install-dejavu-user-font.sh")
  install(PROGRAMS "${CMAKE_SOURCE_DIR}/data/fonts/install-dejavu-user-font.sh"
          DESTINATION ${CMATRIX_PKG_DATADIR})
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/data/uninstall.sh")
  install(PROGRAMS "${CMAKE_SOURCE_DIR}/data/uninstall.sh" DESTINATION ${CMATRIX_PKG_DATADIR})
endif()

if(EXISTS "${CMATRIX_DEJAVU_PATCHED}" AND EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/install-dejavu-user-font.sh")
  install(CODE "
    message(STATUS \"Backing up system DejaVuSansMono.ttf before installing patched copy (requires root)...\")
    execute_process(
      COMMAND bash \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/cmatrix/install-dejavu-user-font.sh\" --backup-only
      RESULT_VARIABLE _cmatrix_ff_backup_rc
    )
    if(NOT _cmatrix_ff_backup_rc EQUAL 0)
      message(WARNING \"install-dejavu-user-font.sh --backup-only failed (\${_cmatrix_ff_backup_rc}). With sudo: sudo bash ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/cmatrix/install-dejavu-user-font.sh --backup-only\")
    endif()
  ")
endif()
if(EXISTS "${CMATRIX_DEJAVU_PATCHED}")
  install(FILES "${CMATRIX_DEJAVU_PATCHED}" DESTINATION ${CMATRIX_FONT_DIR} RENAME DejaVuSansMono.ttf)
endif()
if(EXISTS "${CMATRIX_DEJAVU_PATCHED}" AND EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/install-dejavu-user-font.sh")
  install(CODE "
    message(STATUS \"Refreshing font cache and copying uninstall helper (requires root)...\")
    execute_process(
      COMMAND bash \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/cmatrix/install-dejavu-user-font.sh\" --finalize
      RESULT_VARIABLE _cmatrix_ff_finalize_rc
    )
    if(NOT _cmatrix_ff_finalize_rc EQUAL 0)
      message(WARNING \"install-dejavu-user-font.sh --finalize failed (\${_cmatrix_ff_finalize_rc}). With sudo: sudo bash ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/cmatrix/install-dejavu-user-font.sh --finalize\")
    endif()
  ")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/Matrix_rain_glyph_grid.png")
  install(FILES "${CMAKE_SOURCE_DIR}/data/fonts/Matrix_rain_glyph_grid.png"
          DESTINATION ${CMATRIX_PKG_DATADIR})
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/Matrix_rain_glyph_grid.tsv")
  install(FILES "${CMAKE_SOURCE_DIR}/data/fonts/Matrix_rain_glyph_grid.tsv"
          DESTINATION ${CMATRIX_PKG_DATADIR})
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/LICENSE")
  install(FILES "${CMAKE_SOURCE_DIR}/data/fonts/LICENSE" DESTINATION ${CMATRIX_PKG_DATADIR})
endif()
