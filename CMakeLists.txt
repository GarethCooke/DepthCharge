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

# engine/ .cpp files that are meant to LINK INTO THE FIRMWARE and must therefore
# clear the same bar as the headers. The nlohmann parser TU is deliberately
# absent: it is host-only by design (it allocates, invariant #7) and vendored
# nlohmann does not compile under -fno-exceptions anyway.
set(DC_ENGINE_TARGET_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/engine/src/anvil/anvil_frame_streaming.cpp)

if(EXISTS ${DC_XTENSA_GXX})
  # -std=c++2a, not c++20: GCC 8.4 does not recognise the latter spelling, and
  # the point of this target is to fail the way the firmware build would.
  add_custom_target(dc_engine_target_check ALL
    COMMAND ${CMAKE_COMMAND} -E echo "engine/: checking target build (${DC_XTENSA_GXX})"
    COMMENT "Compiling engine/ for ESP32-S3 (invariant #1, target half)")
  foreach(tu ${DC_HEADER_CHECK_TUS} ${DC_ENGINE_TARGET_SOURCES})
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

# --- Anvil frame parser: TWO implementations of one declaration ---------------
# depthcharge::anvil::parse_anvil_frame is declared in engine/include and
# implemented twice: with nlohmann/json (desk-side, allocates, exhaustively
# correct — the reference the goldens were derived against) and, from M3 stage B,
# with a hand-rolled streaming scanner that never allocates (invariant #7) and is
# what links into firmware/. Everything else in the adapter — including the
# scaling, side-mapping and type-mapping in anvil_scaling.hpp — is shared header
# code, so the swap is exactly one of these two libraries.
#
# Keeping each a separate library is what makes the separation checkable rather
# than stated: dc_engine_anvil_streaming does NOT link dc_vendor, so nlohmann
# cannot reach the target parser even by accident. Note the limit of that — with
# static libraries a binary that linked BOTH would quietly take whichever the
# linker reached first, not error. Nothing here does; the executables below each
# name exactly one, and that is the property to preserve when adding a target.
add_library(dc_engine_anvil STATIC engine/src/anvil/anvil_frame_nlohmann.cpp)
target_link_libraries(dc_engine_anvil
  PUBLIC dc_engine
  PRIVATE dc_vendor dc_warnings)

add_library(dc_engine_anvil_streaming STATIC engine/src/anvil/anvil_frame_streaming.cpp)
target_link_libraries(dc_engine_anvil_streaming
  PUBLIC dc_engine
  PRIVATE dc_warnings)

# --- harness support library --------------------------------------------------
# Trace reader, replay driver (trace -> adapter -> book -> DisplaySnapshot) and
# the console ladder. nlohmann is heavyweight; it is compiled in trace.cpp and
# the Anvil parser TU only.
#
# It deliberately does NOT link a parse_anvil_frame implementation: the symbol is
# left unresolved here so that each executable below chooses one at link time.
# That is what lets the same test sources run against both parsers without a
# fork or a #ifdef (M3 brief, stage B: "same source, two link configs").
add_library(dc_harness STATIC
  harness/src/trace.cpp
  harness/src/replay_driver.cpp
  harness/src/console_ladder.cpp)
target_include_directories(dc_harness PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/harness/include)
target_link_libraries(dc_harness
  PUBLIC dc_engine
  PRIVATE dc_vendor dc_warnings)

# --- executables -------------------------------------------------------------
# Threads: the ladder's paced playback, and from M3 stage A the two-thread
# SnapshotChannel tests. engine/ itself needs none — the channel is std::atomic
# only (invariant #1), and it is the harness that supplies the second thread.
find_package(Threads REQUIRED)

add_executable(dc_replay harness/src/dc_replay_main.cpp)
target_link_libraries(dc_replay PRIVATE dc_harness dc_engine_anvil dc_warnings)

add_executable(dc_ladder harness/src/dc_ladder_main.cpp)
target_link_libraries(dc_ladder PRIVATE dc_harness dc_engine_anvil dc_warnings Threads::Threads)

add_executable(dc_tests
  harness/tests/dc_tests.cpp
  harness/tests/alloc_probe.cpp
  harness/tests/test_decimal.cpp
  harness/tests/test_anvil_adapter.cpp
  harness/tests/test_book.cpp
  harness/tests/test_console_ladder.cpp
  harness/tests/test_frame_reassembler.cpp
  harness/tests/test_parser_equivalence.cpp
  harness/tests/test_replay_goldens.cpp
  harness/tests/test_snapshot_channel.cpp)
target_compile_definitions(dc_tests PRIVATE
  DC_REPLAY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/harness/replay")
# M3 stage C: the firmware's WebSocket reassembler is deliberately ESP-IDF-free
# so it can be tested here. This is the ONLY firmware/ path the host build knows
# about, and it is a header — no firmware .cpp is compiled on the host, so
# nothing in firmware/ can quietly become a host dependency.
target_include_directories(dc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/firmware/src)
target_link_libraries(dc_tests PRIVATE
  dc_harness dc_engine_anvil dc_vendor dc_warnings Threads::Threads)

# --- the same tests, against the streaming parser (M3 stage B) ----------------
# THE ACCEPTANCE GATE FOR THE PARSER SWAP. Not a fork of the suite: the two files
# below are the exact sources dc_tests compiles, linked against
# dc_engine_anvil_streaming instead of dc_engine_anvil. If a byte of decoded
# output differs between the two implementations, one of these binaries goes red
# and the other stays green.
#   test_replay_goldens.cpp     both committed traces, end to end (invariant #6)
#   test_anvil_adapter.cpp      the 19-frame malformed corpus + the seam's
#                               postcondition, which anvil_frame.hpp says a
#                               second implementation must inherit rather than
#                               re-earn
#   test_parser_equivalence.cpp the JSON grammar the traces never exercise —
#                               escapes, duplicate keys, over-range integers,
#                               invalid UTF-8, error precedence
#   test_streaming_parser.cpp   the parts that are only true of THIS parser: the
#                               allocation proof (invariant #7) and the two
#                               deliberate divergences from the reference
add_executable(dc_tests_streaming
  harness/tests/doctest_main.cpp
  harness/tests/alloc_probe.cpp
  harness/tests/test_anvil_adapter.cpp
  harness/tests/test_parser_equivalence.cpp
  harness/tests/test_replay_goldens.cpp
  harness/tests/test_streaming_parser.cpp)
target_compile_definitions(dc_tests_streaming PRIVATE
  DC_REPLAY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/harness/replay")
target_link_libraries(dc_tests_streaming PRIVATE
  dc_harness dc_engine_anvil_streaming dc_vendor dc_warnings Threads::Threads)

# The replay report, produced by the other parser. dc_replay prints per-frame
# statistics and the final ladder state; running both over both traces and
# diffing the output is the coarse-grained twin of the goldens above — it proves
# equality of the whole pipeline's output text, not just of the pinned fields.
add_executable(dc_replay_streaming harness/src/dc_replay_main.cpp)
target_link_libraries(dc_replay_streaming PRIVATE
  dc_harness dc_engine_anvil_streaming dc_warnings)

# --- the ThreadSanitizer workload, built here without the sanitiser -----------
# harness/tsan.sh compiles this same TU with -fsanitize=thread on a Linux box and
# commits the report. TSan needs Linux; this project's desk is Windows/MinGW, so
# without a plain build of it here the workload would only ever be compiled by
# the machine that is hardest to reach and would rot between M3 sessions. Run as
# a ctest it is a second, differently-shaped concurrency check: doctest's suite
# asserts, this one races for a second and reports.
add_executable(dc_tsan_workload harness/tests/tsan_workload.cpp)
target_include_directories(dc_tsan_workload PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/harness/tests)
target_link_libraries(dc_tsan_workload PRIVATE dc_engine dc_warnings Threads::Threads)

# --- tests -------------------------------------------------------------------
enable_testing()
add_test(NAME dc_tests COMMAND dc_tests)
add_test(NAME dc_tests_streaming COMMAND dc_tests_streaming)
add_test(NAME dc_replay_baseline
  COMMAND dc_replay ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_baseline.ndjson)
add_test(NAME dc_replay_reconnect
  COMMAND dc_replay ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_reconnect.ndjson)
add_test(NAME dc_replay_streaming_baseline
  COMMAND dc_replay_streaming
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_baseline.ndjson)
add_test(NAME dc_replay_streaming_reconnect
  COMMAND dc_replay_streaming
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_reconnect.ndjson)
# The ladder runs end-to-end on both traces: a clean exit means the whole chain
# (parse -> adapt -> adopt -> publish -> draw) survived every committed frame.
add_test(NAME dc_ladder_baseline
  COMMAND dc_ladder ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_baseline.ndjson
          --no-color)
add_test(NAME dc_ladder_reconnect
  COMMAND dc_ladder ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_reconnect.ndjson
          --no-color)
# One second of the real two-thread race, uninstrumented. The TSan run in
# harness/tsan.sh is the same binary with the sanitiser on; this keeps the
# workload compiling and passing on every host build in between.
add_test(NAME dc_channel_race COMMAND dc_tsan_workload 1)

# Explicit timeouts on the two targets that now run threads. A hand-off bug that
# blocks the writer does not fail an assertion — it hangs — so the useful report
# is ctest saying "Timeout" in a minute rather than inheriting the 1500 s
# default and looking like the machine died.
set_tests_properties(dc_tests dc_tests_streaming dc_channel_race PROPERTIES TIMEOUT 120)
