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
  ${CMAKE_CURRENT_SOURCE_DIR}/engine/src/anvil/anvil_frame_streaming.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/engine/src/kraken/kraken_frame_streaming.cpp)

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

# --- Kraken's parser: ONE implementation, and it is the target-bound one -------
# Unlike Anvil there is no reference/streaming pair here, and kraken_frame.hpp
# says why: the pair exists at Anvil because M1 shipped an allocating reference
# that M3 then had to prove a rewrite equivalent to. Kraken has no such reference,
# and writing an allocating parser purely to have something to agree with would
# be inventing the obligation. So this library is what the harness AND the
# firmware link, it never sees dc_vendor, and it is in DC_ENGINE_TARGET_SOURCES
# above so the xtensa compiler builds it on every host configure.
#
# Nothing links it yet -- the replay driver picks it up in the next commit. It is
# defined here because it is the adapter's build rule, and a library defined
# beside its sources is easier to read than one that appears with its first user.
add_library(dc_engine_kraken STATIC engine/src/kraken/kraken_frame_streaming.cpp)
target_link_libraries(dc_engine_kraken
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
  harness/src/trace_decoder.cpp
  harness/src/trace_report.cpp
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

# The record taxonomy of every committed trace, and its pin (M4 stage A).
# It links a parser implementation only because dc_harness leaves
# parse_anvil_frame unresolved for the replay driver; the taxonomy pass itself
# never parses an Anvil frame — it reads the envelope and asks the venue's
# decoder to name each record.
add_executable(dc_taxonomy harness/src/dc_taxonomy_main.cpp)
target_link_libraries(dc_taxonomy PRIVATE dc_harness dc_engine_anvil dc_warnings)

add_executable(dc_tests
  harness/tests/dc_tests.cpp
  harness/tests/alloc_probe.cpp
  harness/tests/test_age_estimator.cpp
  harness/tests/test_decimal.cpp
  harness/tests/test_anvil_adapter.cpp
  harness/tests/test_book.cpp
  harness/tests/test_console_ladder.cpp
  harness/tests/test_frame_reassembler.cpp
  harness/tests/test_gap_histogram.cpp
  harness/tests/test_ladder_render.cpp
  harness/tests/test_panel_budget.cpp
  harness/tests/test_parser_equivalence.cpp
  harness/tests/test_reject_log.cpp
  harness/tests/test_replay_goldens.cpp
  harness/tests/test_rx_budget.cpp
  harness/tests/test_snapshot_channel.cpp
  harness/tests/test_stall_probe.cpp
  harness/tests/test_staleness.cpp
  harness/tests/test_trace_venue.cpp
  harness/tests/test_ws_frame.cpp
  harness/tests/test_ws_ping.cpp
  harness/tests/test_ws_supervisor.cpp)
target_compile_definitions(dc_tests PRIVATE
  DC_REPLAY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/harness/replay")
# M3 stages C, D, E and the transport rebuild: the firmware's WebSocket frame
# parser and reassembler, its gap histograms,
# the stall probe that classifies a >1 s book-hole, the reject log that captures
# what the parser threw away, the reconnect policy that decides how long the
# panel stays grey, the 64x64 ladder renderer and the font its row budget is
# computed from, the summary-deficit age estimator from stage E, and — from D5 —
# the client-ping round-trip that prices this socket's server-side send queue,
# are deliberately ESP-IDF-free so they can be tested here. These are the ONLY
# firmware/ paths the host build knows about, and every one of them is a header —
# no firmware .cpp is compiled on the host, so nothing in firmware/ can quietly
# become a host dependency.
#
# ladder_render.hpp is the one that most needed it. Stage D's claim is that a
# stale book CANNOT be drawn in colour, and the panel is a bench instrument two
# people can look at and disagree about; here it is a 64x64 grid of Ink and three
# mechanical questions (test_ladder_render.cpp). The row budget — 27 levels a
# side, a header, and 64 rows exactly — is likewise a static_assert on this desk
# rather than a discovery on the bench.
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

# --- the pingless arm's one branch (D5) ---------------------------------------
# `kClientPingEnabled` is a compile-time constant, so the branch it guards is
# unreachable from dc_tests, which builds with the ping on. Same trick as
# dc_tests_streaming above — one source, a second link configuration — for the
# one property that only exists in the other arm: the pingless build must say it
# is not asking, never that nothing came back. The two read identically on a
# bench line and mean opposite things.
add_executable(dc_tests_noping
  harness/tests/doctest_main.cpp
  harness/tests/test_ws_ping_disabled.cpp)
target_compile_definitions(dc_tests_noping PRIVATE DC_WS_PING=0)
target_include_directories(dc_tests_noping PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/firmware/src)
target_link_libraries(dc_tests_noping PRIVATE dc_vendor dc_warnings)

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
add_test(NAME dc_tests_noping COMMAND dc_tests_noping)
add_test(NAME dc_replay_baseline
  COMMAND dc_replay ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_baseline.ndjson)
add_test(NAME dc_replay_reconnect
  COMMAND dc_replay ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_reconnect.ndjson)
add_test(NAME dc_replay_baseline_20260809
  COMMAND dc_replay
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_baseline_20260809.ndjson)
# A7's wire, captured the day it went live (2026-08-16). The board now subscribes
# with `&depth=27` and is served 30 levels a side by Anvil's tier ladder, so this
# is the ONLY committed trace that records what the firmware actually receives —
# the other three are full-depth history. Kept alongside them rather than
# replacing them: the shallow book is the new normal, the deep one is what every
# golden before today was derived from, and both must keep parsing.
add_test(NAME dc_replay_depth27
  COMMAND dc_replay
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_depth27_20260816.ndjson)
add_test(NAME dc_replay_streaming_baseline
  COMMAND dc_replay_streaming
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_baseline.ndjson)
add_test(NAME dc_replay_streaming_baseline_20260809
  COMMAND dc_replay_streaming
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_baseline_20260809.ndjson)
add_test(NAME dc_replay_streaming_reconnect
  COMMAND dc_replay_streaming
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_reconnect.ndjson)
# The shallow book through the parser that actually links into the firmware —
# which is the one that matters for A7, since the streaming parser is what will
# meet a 30-level frame on the board.
add_test(NAME dc_replay_streaming_depth27
  COMMAND dc_replay_streaming
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_depth27_20260816.ndjson)
# The ladder runs end-to-end on both traces: a clean exit means the whole chain
# (parse -> adapt -> adopt -> publish -> draw) survived every committed frame.
add_test(NAME dc_ladder_baseline
  COMMAND dc_ladder ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_baseline.ndjson
          --no-color)
add_test(NAME dc_ladder_reconnect
  COMMAND dc_ladder ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_reconnect.ndjson
          --no-color)
# The whole chain against the shallow book. This is the one that would catch a
# depth the ladder cannot fill: the panel renders 27 a side and Anvil now serves
# 30, so the margin is three levels rather than the ~175 it used to be.
add_test(NAME dc_ladder_depth27
  COMMAND dc_ladder ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_depth27_20260816.ndjson
          --no-color)
# The idle server (2026-08-17): 1,753 frames over an EMPTY book, so the whole
# chain runs with zero levels on either side from the first frame to the last.
# Every other committed trace has a populated book within one frame, so nothing
# before this exercised the empty case end to end — and the panel's answer to
# "no orders exist" must be a clean empty ladder, not a crash and not a stale
# state. That is a different claim from "no data arrived", which is what the
# watchdog covers.
add_test(NAME dc_replay_feederoff
  COMMAND dc_replay
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_feederoff_20260817.ndjson)
add_test(NAME dc_ladder_feederoff
  COMMAND dc_ladder ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_feederoff_20260817.ndjson
          --no-color)
# One second of the real two-thread race, uninstrumented. The TSan run in
# harness/tsan.sh is the same binary with the sanitiser on; this keeps the
# workload compiling and passing on every host build in between.
add_test(NAME dc_channel_race COMMAND dc_tsan_workload 1)

# --- the second venue's traces, read end to end (M4 stage A) ------------------
# Before stage A, dc_replay could not read any of these: it rejected the
# metadata header on the required `ticker`, and would then have rejected 61 of
# the depth-25 slice's 1,599 records for carrying no string `type`. These four
# tests are deliverable 2's provable end state — the reader dispatches to
# Kraken's decoder, the decoder names every record, and the run exits clean.
#
# Only dc_replay, not dc_ladder: the ladder runs the replay DRIVER, which feeds
# the Anvil adapter, and stage A ships Kraken's decoder as a classifier. The
# driver refuses a non-Anvil trace loudly for that reason, and
# dc_replay_kraken_refused_by_driver below pins the refusal.
foreach(_k d10 d25 d100)
  add_test(NAME dc_replay_kraken_btcusd_${_k}
    COMMAND dc_replay
            ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_btcusd_${_k}_20260816.ndjson)
endforeach()
# The quiet pair, which is the only artefact in the repository containing a
# nine-second healthy silence — deliverable 4's demonstration runs on this file.
add_test(NAME dc_replay_kraken_quiet_pair
  COMMAND dc_replay
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_minagbp_d25_20260816.ndjson)
# ...and the EXTREME of the same case, captured at a second hour (2026-08-17):
# 25,843 ms of healthy book silence with the heartbeat keeping 936-1,042 ms time
# straight through it. This is the trace the staleness ruling exists to serve,
# and the one that retired the 15,000 ms book-silence constant — at which it
# would have invented three disconnects on a feed that never lost a packet.
add_test(NAME dc_replay_kraken_quiet_pair_extreme
  COMMAND dc_replay
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_minagbp_d25_20260817.ndjson)

# The taxonomy pin, over EVERY committed trace, Anvil included. An unpinned
# trace fails rather than skips (see dc_taxonomy_main.cpp): adding a committed
# trace without recording its figures would otherwise leave a green test that
# measures nothing about it.
file(GLOB DC_COMMITTED_TRACES
  ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_*.ndjson
  ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_*.ndjson)
add_test(NAME trace_taxonomy_selfcheck
  COMMAND dc_taxonomy --selfcheck ${DC_COMMITTED_TRACES})
set_tests_properties(trace_taxonomy_selfcheck PROPERTIES TIMEOUT 300)

# Explicit timeouts on the two targets that now run threads. A hand-off bug that
# blocks the writer does not fail an assertion — it hangs — so the useful report
# is ctest saying "Timeout" in a minute rather than inheriting the 1500 s
# default and looking like the machine died.
set_tests_properties(dc_tests dc_tests_streaming dc_tests_noping dc_channel_race
  PROPERTIES TIMEOUT 120)

# --- the Python measurement tools, held to the same standard (M4 stage 0) -----
# The FIRST test in this file that is not C++, and it exists because of a specific
# failure: tools/kraken_frame_economics.py produced every figure in
# harness/replay/NOTES-kraken.md and the M4 stage-0 brief, and it shipped two bugs
# on the way there — a truncation column that discarded every level entering the
# top N, and a verify pass whose float shadow silently skipped 92 real checksum
# comparisons. Both printed believable numbers. The firmware's instruments are
# pinned by host goldens; the Python ones were not, and that asymmetry cost real
# time twice in one evening (owner's process item, docs/briefs/).
#
# `--selfcheck` recomputes the committed slices and compares against the figures
# recorded in the tool's KNOWN_ANSWERS table, so a tooling regression fails in the
# normal build loop instead of reporting something plausible. Mutation-verified:
# removing the truncation, keeping leading zeros in the checksum token, and
# widening the CRC to the top 12 are each caught.
#
# Optional by design, exactly like dc_engine_target_check above — a contributor
# without a Python 3 interpreter still gets a complete C++ build and a STATUS line
# saying which check was skipped.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  file(GLOB DC_KRAKEN_TRACES
    ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_*.ndjson)
  if(DC_KRAKEN_TRACES)
    add_test(NAME kraken_tool_selfcheck
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/kraken_frame_economics.py
              ${DC_KRAKEN_TRACES} --selfcheck)
    set_tests_properties(kraken_tool_selfcheck PROPERTIES TIMEOUT 300)
  endif()
else()
  message(STATUS
    "kraken_tool_selfcheck SKIPPED: no Python 3 interpreter found. The Python "
    "measurement tools are unpinned in this configuration.")
endif()
