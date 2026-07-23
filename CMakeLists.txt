# DepthCharge — host build (M0 skeleton).
#
# Builds the portable engine (header-only for now) and the host harness
# (replay reader + goldens). Firmware is a separate PlatformIO project under
# firmware/ and is not built here. See ARCHITECTURE.md; the loop is
#   cmake --preset host && ctest --preset host
cmake_minimum_required(VERSION 3.20)
project(DepthCharge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# Warnings-as-errors on our code (CLAUDE.md toolchain rule). Applied privately to
# the harness targets; vendored third-party headers are SYSTEM includes below so
# their warnings never fail our build.
add_library(dc_warnings INTERFACE)
target_compile_options(dc_warnings INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
  $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>)

# --- engine: portable, header-only for now -----------------------------------
# INTERFACE library => no MCU headers can leak in, and it is exercised entirely
# by the host harness (invariant #1).
add_library(dc_engine INTERFACE)
target_include_directories(dc_engine INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/engine/include)
target_compile_features(dc_engine INTERFACE cxx_std_20)

# --- vendored third-party (harness only) -------------------------------------
# nlohmann/json + doctest, included as SYSTEM so -Werror does not police them.
add_library(dc_vendor INTERFACE)
target_include_directories(dc_vendor SYSTEM INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/harness/third_party)

# --- harness support library: replay-trace reader ----------------------------
# nlohmann/json is heavyweight; compile it in this one TU and link it into both
# executables.
add_library(dc_harness STATIC harness/src/trace.cpp)
target_include_directories(dc_harness PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/harness/include)
target_link_libraries(dc_harness
  PUBLIC dc_engine
  PRIVATE dc_vendor dc_warnings)

# --- executables -------------------------------------------------------------
add_executable(dc_replay harness/src/dc_replay_main.cpp)
target_link_libraries(dc_replay PRIVATE dc_harness dc_warnings)

add_executable(dc_tests harness/tests/dc_tests.cpp)
target_compile_definitions(dc_tests PRIVATE
  DC_REPLAY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/harness/replay")
target_link_libraries(dc_tests PRIVATE dc_harness dc_vendor dc_warnings)

# --- tests -------------------------------------------------------------------
enable_testing()
add_test(NAME dc_tests COMMAND dc_tests)
add_test(NAME dc_replay_baseline
  COMMAND dc_replay ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_baseline.ndjson)
add_test(NAME dc_replay_reconnect
  COMMAND dc_replay ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_reconnect.ndjson)
