cmake_minimum_required(VERSION 3.20)

set(TASK_NAME cpu_zstd)

project(${TASK_NAME} LANGUAGES CXX VERSION 0.1.0)

if(NOT DEFINED BLITZ_BUILD_MODE)
    set(BLITZ_BUILD_MODE STATIC)
endif()

if(NOT TARGET blitz_task_cpp)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../task-lib/cpp blitz_task_cpp)
endif()

find_package(Threads REQUIRED)

# Builds the vendored zstd submodule and exposes it as the imported target
# zstd::zstd. zstd is BSD-3-Clause, so the task's own license is unaffected.
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cpp/cmake/BuildZstd.cmake)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/TASK.json" TASK_JSON_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," TASK_JSON_ARRAY "${TASK_JSON_HEX}")
set(TASK_JSON_VAR "CPU_ZSTD_INFO_JSON")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/info.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/info.cpp"
    @ONLY
)

add_library(${TASK_NAME} ${BLITZ_BUILD_MODE}
    src/cpu_zstd.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/info.cpp"
)
target_include_directories(${TASK_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(${TASK_NAME}
    PUBLIC blitz_task_cpp
    PRIVATE zstd::zstd Threads::Threads
)
target_compile_features(${TASK_NAME} PUBLIC cxx_std_17)
set_target_properties(${TASK_NAME} PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
)

# Path to the bundled corpus, baked in for local/sample-app builds. A deployed
# plugin can override at runtime with the BLITZ_ZSTD_CORPUS environment variable.
target_compile_definitions(${TASK_NAME} PRIVATE
    BLITZ_ZSTD_CORPUS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/corpus.txt"
)

if ( ${BUILD_SAMPLE_APP} )
    add_subdirectory(app)
endif()
