# DepthCharge — host build.
#
# Builds the portable engine and the host harness (replay reader, adapter,
# phase-1 book, console ladder, goldens). Firmware is a separate PlatformIO
# project under firmware/ and is not built here. See ARCHITECTURE.md; the loop is
#   cmake --workflow --preset host
# 3.25 to match CMakePresets.json's own cmakeMinimumRequired: a 3.20-3.24 CMake
# satisfied this line and then failed on the presets file with a much less
# obvious error.
cmake_minimum_required(VERSION 3.25)
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

# --- engine: portable, header-only ------------------------------------------
# INTERFACE library => no MCU headers can leak in, and it is exercised entirely
# by the host harness (invariant #1). Contract types, decimal conversion, the
# phase-1 book, the Anvil adapter logic and the publish seam all live here.
add_library(dc_engine INTERFACE)
target_include_directories(dc_engine INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/engine/include)
target_compile_features(dc_engine INTERFACE cxx_std_20)

# --- invariant #1, enforced rather than asserted ------------------------------
# One generated TU per engine header, each including only that header. It fails
# the build if a header stops being self-contained, and — because the host has no
# ESP-IDF/FreeRTOS/Arduino headers to find — it fails the build the moment one
# leaks into engine/. "engine/ builds on the host" stops being a claim a session
# has to remember to check.
file(GLOB_RECURSE DC_ENGINE_HEADERS CONFIGURE_DEPENDS
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/engine/include
  ${CMAKE_CURRENT_SOURCE_DIR}/engine/include/*.hpp)
set(DC_HEADER_CHECK_TUS "")
foreach(hdr ${DC_ENGINE_HEADERS})
  string(REGEX REPLACE "[/.]" "_" tu_name "${hdr}")
  set(tu "${CMAKE_CURRENT_BINARY_DIR}/header_checks/${tu_name}.cpp")
  file(WRITE "${tu}" "#include <${hdr}>\n")
  list(APPEND DC_HEADER_CHECK_TUS "${tu}")
endforeach()
add_library(dc_engine_header_check OBJECT ${DC_HEADER_CHECK_TUS})
target_link_libraries(dc_engine_header_check PRIVATE dc_engine dc_warnings)

# --- invariant #1, the other half: engine/ builds for the TARGET -------------
# The check above proves engine/ compiles on the host. It cannot prove it
# compiles for the ESP32-S3, and the two disagree: the target toolchain is a
# different compiler generation with a different libstdc++, so a header can be
# perfectly valid here and reject on the board. That is not hypothetical — a
# constexpr std::reverse_copy compiles on GCC 15.2 and is ill-formed on the
# xtensa GCC 8.4 that espressif32 6.5.0 pins, and nothing in this build would
# have said so before M3 tried to link.
#
# So: compile the same generated per-header TUs with the PlatformIO xtensa
# compiler when it is installed, with ESP-IDF-shaped flags. Optional by design —
# a contributor without PlatformIO still gets a working host build, and the
# check simply reports that it was skipped.
set(DC_XTENSA_GXX "$ENV{USERPROFILE}/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++.exe"
    CACHE FILEPATH "xtensa-esp32s3 g++ used to prove engine/ compiles for the target")
if(NOT EXISTS ${DC_XTENSA_GXX})
  file(GLOB _dc_xtensa_candidates
    "$ENV{USERPROFILE}/.platformio/packages/toolchain-xtensa-esp32s3*/bin/xtensa-esp32s3-elf-g++.exe"
    "$ENV{HOME}/.platformio/packages/toolchain-xtensa-esp32s3*/bin/xtensa-esp32s3-elf-g++")
  if(_dc_xtensa_candidates)
    list(GET _dc_xtensa_candidates 0 DC_XTENSA_GXX)
  endif()
endif()

if(EXISTS ${DC_XTENSA_GXX})
  # -std=c++2a, not c++20: GCC 8.4 does not recognise the latter spelling, and
  # the point of this target is to fail the way the firmware build would.
  add_custom_target(dc_engine_target_check ALL
    COMMAND ${CMAKE_COMMAND} -E echo "engine/: checking target build (${DC_XTENSA_GXX})"
    COMMENT "Compiling engine/ headers for ESP32-S3 (invariant #1, target half)")
  foreach(tu ${DC_HEADER_CHECK_TUS})
    get_filename_component(_tu_name "${tu}" NAME_WE)
    add_custom_command(TARGET dc_engine_target_check POST_BUILD
      COMMAND ${DC_XTENSA_GXX} -std=c++2a -Os -mlongcalls
              -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti
              -Wall -Wextra -Wpedantic -Werror
              -I ${CMAKE_CURRENT_SOURCE_DIR}/engine/include
              -c ${tu} -o ${CMAKE_CURRENT_BINARY_DIR}/header_checks/${_tu_name}.xtensa.o
      VERBATIM)
  endforeach()
else()
  message(STATUS
    "engine/ target check SKIPPED: no xtensa-esp32s3 toolchain found. "
    "engine/ is proven host-buildable only; set -DDC_XTENSA_GXX=<path> to check "
    "the ESP32-S3 build too.")
endif()

# --- vendored third-party (harness + host adapter parser) --------------------
# nlohmann/json + doctest, included as SYSTEM so -Werror does not police them.
add_library(dc_vendor INTERFACE)
target_include_directories(dc_vendor SYSTEM INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/harness/third_party)

# --- Anvil frame parser: the HOST implementation of the adapter's parse seam --
# depthcharge::anvil::parse_anvil_frame is declared in engine/include and
# implemented twice: here with nlohmann/json (desk-side, allocates, exhaustively
# correct), and — from M3 — in firmware/ with a streaming allocation-free parser
# (invariant #7). Everything else in the adapter is shared header code, so the
# swap is exactly this one target. Keeping it a separate library is what makes
# that claim checkable: nothing else in the build may link dc_vendor and land on
# the target.
add_library(dc_engine_anvil STATIC engine/src/anvil/anvil_frame_nlohmann.cpp)
target_link_libraries(dc_engine_anvil
  PUBLIC dc_engine
  PRIVATE dc_vendor dc_warnings)

# --- harness support library --------------------------------------------------
# Trace reader, replay driver (trace -> adapter -> book -> DisplaySnapshot) and
# the console ladder. nlohmann is heavyweight; it is compiled in trace.cpp and
# the Anvil parser TU only.
add_library(dc_harness STATIC
  harness/src/trace.cpp
  harness/src/replay_driver.cpp
  harness/src/console_ladder.cpp)
target_include_directories(dc_harness PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/harness/include)
target_link_libraries(dc_harness
  PUBLIC dc_engine dc_engine_anvil
  PRIVATE dc_vendor dc_warnings)

# --- executables -------------------------------------------------------------
add_executable(dc_replay harness/src/dc_replay_main.cpp)
target_link_libraries(dc_replay PRIVATE dc_harness dc_warnings)

add_executable(dc_ladder harness/src/dc_ladder_main.cpp)
target_link_libraries(dc_ladder PRIVATE dc_harness dc_warnings)
find_package(Threads REQUIRED)
target_link_libraries(dc_ladder PRIVATE Threads::Threads)

add_executable(dc_tests
  harness/tests/dc_tests.cpp
  harness/tests/alloc_probe.cpp
  harness/tests/test_decimal.cpp
  harness/tests/test_anvil_adapter.cpp
  harness/tests/test_book.cpp
  harness/tests/test_console_ladder.cpp
  harness/tests/test_replay_goldens.cpp)
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
# The ladder runs end-to-end on both traces: a clean exit means the whole chain
# (parse -> adapt -> adopt -> publish -> draw) survived every committed frame.
add_test(NAME dc_ladder_baseline
  COMMAND dc_ladder ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_baseline.ndjson
          --no-color)
add_test(NAME dc_ladder_reconnect
  COMMAND dc_ladder ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_reconnect.ndjson
          --no-color)
