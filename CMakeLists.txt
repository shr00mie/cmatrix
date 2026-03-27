# CMake build system for CMatrix (Linux amd64 only)
# Tested with: CMake 3.31+, ncurses (ncursesw) 6.4+

cmake_minimum_required(VERSION 3.31)

project(CMatrix LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "CMatrix is supported on Linux only. Detected: ${CMAKE_SYSTEM_NAME}")
endif()
set(VERSION "2.0")

# Default to Release so "cmake .." and "make" produce an optimized binary (-O3).
# Without this, CMAKE_BUILD_TYPE is empty and no optimization flags are applied.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type: Debug, Release, RelWithDebInfo, MinSizeRel" FORCE)
endif()

include(GNUInstallDirs)

# These are relative to CMAKE_INSTALL_PREFIX
# which by default is "/usr/local"
message(STATUS "Looking for include file config.h")
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/config.h")
    set(CMATRIX_EXCLUDE_CONFIG_H 1)
    message(STATUS "Looking for include file config.h - missing")
else()
    message(STATUS "Looking for include file config.h - found")
endif()

# configure_file(config.h.in config.h)

include(CheckIncludeFiles)
check_include_files("sys/ioctl.h" HAVE_SYS_IOCTL_H)
check_include_files("unistd.h" HAVE_UNISTD_H)
check_include_files("termios.h" HAVE_TERMIOS_H)
check_include_files("termio.h" HAVE_TERMIO_H)
check_include_files("getopt.h" HAVE_GETOPT_H)

set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses)
include_directories(${CURSES_INCLUDE_DIR})

include(CheckSymbolExists)
list(APPEND CMAKE_REQUIRED_LIBRARIES ncurses)
check_symbol_exists(use_default_colors "ncurses.h" HAVE_USE_DEFAULT_COLORS)

add_executable(cmatrix src/cmatrix.c src/util.c)
target_include_directories(cmatrix PRIVATE
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
)

target_compile_definitions(cmatrix PRIVATE VERSION="${VERSION}" HAVE_NCURSES_H)
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
if(HAVE_USE_DEFAULT_COLORS)
    target_compile_definitions(cmatrix PRIVATE HAVE_USE_DEFAULT_COLORS)
endif()

target_link_libraries(cmatrix ${CURSES_LIBRARIES} m)

install(TARGETS cmatrix DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES cmatrix.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

# System-wide fonts: install directly under /usr/share/fonts/truetype.
set(CMATRIX_FONT_DIR "/usr/share/fonts/truetype")
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
set(CMATRIX_DEJAVU_PATCHED "${CMAKE_SOURCE_DIR}/data/fonts/DejaVuSansMono_patched.ttf")
if(EXISTS "${CMATRIX_DEJAVU_PATCHED}")
  install(CODE "message(STATUS \"Patching system DejaVu Sans Mono with backup/uninstall support...\")
                execute_process(COMMAND bash \"${CMAKE_SOURCE_DIR}/data/fonts/install-dejavu-user-font.sh\")")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/Matrix-Code-NFI.ttf")
  install(FILES "${CMAKE_SOURCE_DIR}/data/fonts/Matrix-Code-NFI.ttf"
          DESTINATION ${CMATRIX_FONT_DIR})
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/old/Matrix-Code-mono-pua.ttf" OR EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/old/Matrix-Code-mono.ttf" OR EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/old/Matrix-Code.ttf" OR EXISTS "${CMAKE_SOURCE_DIR}/data/fonts/Matrix-Code-NFI.ttf" OR EXISTS "${CMATRIX_DEJAVU_PATCHED}")
  install(CODE "message(STATUS \"Running fc-cache -fv (system font cache)...\")
                execute_process(COMMAND fc-cache -fv RESULT_VARIABLE FC_EXIT)
                if(NOT FC_EXIT EQUAL 0)
                  message(WARNING \"fc-cache returned ${FC_EXIT}; font may not be visible until you run fc-cache -fv\")
                endif()")
endif()
install(FILES "${CMAKE_SOURCE_DIR}/data/fonts/LICENSE" "${CMAKE_SOURCE_DIR}/data/fonts/FONT_SOURCE.md"
        DESTINATION ${CMATRIX_FONT_DIR} OPTIONAL)

