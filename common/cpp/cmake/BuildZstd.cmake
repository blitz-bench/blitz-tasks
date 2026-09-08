# Builds the vendored external/zstd submodule and exposes it as zstd::zstd.
# zstd ships a first-class CMake build, so it is pulled in via add_subdirectory
# with the cache pinned to a minimal static library (no shared lib, programs,
# tests or legacy formats). Works unchanged on Linux/macOS/Windows-MSVC.

include_guard(GLOBAL)

if(TARGET zstd::zstd)
    return()
endif()

get_filename_component(_zstd_src "${CMAKE_CURRENT_LIST_DIR}/../../../external/zstd" ABSOLUTE)
if(NOT EXISTS "${_zstd_src}/lib/zstd.h")
    message(FATAL_ERROR
        "zstd submodule not found at ${_zstd_src}.\n"
        "Run:  git submodule update --init external/zstd")
endif()

set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
# Tasks run independent single-shot compressions per thread; zstd's internal
# multithreading is unused, so leave it (and its pthread dependency) out.
set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)

add_subdirectory("${_zstd_src}/build/cmake" "${CMAKE_BINARY_DIR}/zstd_build" EXCLUDE_FROM_ALL)

# libzstd_static already carries the INTERFACE include dir (external/zstd/lib).
add_library(zstd::zstd ALIAS libzstd_static)
