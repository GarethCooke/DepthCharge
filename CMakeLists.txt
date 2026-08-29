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
#
# WRITE-IF-DIFFERENT, AND IT IS NOT AN OPTIMISATION. `file(WRITE)` runs at
# CONFIGURE time and truncates its target unconditionally, so writing these on
# every configure did two things — one wasteful, one a real defect:
#
#   * every configure updated all ~20 mtimes, so every one of these objects
#     rebuilt whether or not anything had changed; and
#   * a configure overlapping a build TRUNCATES A FILE THE COMPILER IS OPENING.
#     Observed twice on 2026-08-28/29: `mingw32-make` Error 1 on
#     `depthcharge_age_estimator_hpp.cpp.obj` — the ALPHABETICALLY FIRST
#     generated TU, and therefore the one a parallel make reaches first — with
#     no compiler diagnostic at all, because the failure is in OPENING the
#     source rather than in compiling it. Both times it passed on retry, and
#     that is the part that matters: **a build that reports red for no reason is
#     worse than a slow one**, because the next red gets retried instead of
#     read. This project already has a §9 row about instruments that lie.
#
# Writing only when the content actually differs removes both. The generated
# text is one line per header, so the comparison costs nothing.
set(DC_HEADER_CHECK_TUS "")
foreach(hdr ${DC_ENGINE_HEADERS})
  string(REGEX REPLACE "[/.]" "_" tu_name "${hdr}")
  set(tu "${CMAKE_CURRENT_BINARY_DIR}/header_checks/${tu_name}.cpp")
  set(_dc_tu_want "#include <${hdr}>\n")
  set(_dc_tu_have "")
  if(EXISTS "${tu}")
    file(READ "${tu}" _dc_tu_have)
  endif()
  if(NOT _dc_tu_have STREQUAL _dc_tu_want)
    file(WRITE "${tu}" "${_dc_tu_want}")
  endif()
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
  ${CMAKE_CURRENT_SOURCE_DIR}/engine/src/kraken/kraken_frame_streaming.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/engine/src/binance/binance_frame_streaming.cpp)

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
# --- Binance's parser: the same arrangement as Kraken's, for the same reasons --
# One implementation, target-bound, never linked against dc_vendor, and in
# DC_ENGINE_TARGET_SOURCES above so the xtensa compiler builds it on every host
# configure. It is a THIRD PARSER AND NOT A THIRD SCANNER: the JSON token layer
# is depthcharge/json_scan.hpp, extracted from Kraken's parser at M5 stage B1 and
# reused here without a venue flag (DESIGN strain 25).
add_library(dc_engine_binance STATIC engine/src/binance/binance_frame_streaming.cpp)
target_link_libraries(dc_engine_binance
  PUBLIC dc_engine
  PRIVATE dc_warnings)

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
target_link_libraries(dc_replay PRIVATE dc_harness dc_engine_anvil dc_engine_kraken dc_engine_binance dc_warnings)

add_executable(dc_ladder harness/src/dc_ladder_main.cpp)
target_link_libraries(dc_ladder PRIVATE dc_harness dc_engine_anvil dc_engine_kraken dc_engine_binance dc_warnings Threads::Threads)

# THE REAL ADAPTER, GRADED AGAINST THE VENUE'S OWN BOOK (M5 stage B1).
# tools/binance_oracle.py grades a PYTHON reference and proves the oracle
# discriminates; this grades `depthcharge::binance::BinanceAdapter`, which is the
# book that will actually be drawn. Different question, same instrument.
add_executable(dc_binance_oracle harness/src/dc_binance_oracle_main.cpp)
target_link_libraries(dc_binance_oracle PRIVATE
  dc_harness dc_engine_anvil dc_engine_kraken dc_engine_binance dc_warnings)

# WHAT `age_ms` READS THROUGH A KNOWN BACKLOG (M5 stage B2, section 5).
# The desk twin of tools/kraken_backpressure_probe.py: that one settled
# queue-versus-shed on a live socket, because the question was about the venue's
# send queue. This one is about WHICH SUBSYSTEM EMITS THE SIGNAL THE METER
# COUNTS, which is answerable from a committed file — and it injects a lag it
# therefore knows exactly, which is what anvil_freshness_probe.py needed two
# sockets to obtain.
add_executable(dc_age_probe harness/src/dc_age_probe_main.cpp)
target_link_libraries(dc_age_probe PRIVATE
  dc_harness dc_engine_anvil dc_engine_kraken dc_engine_binance dc_warnings)

# The record taxonomy of every committed trace, and its pin (M4 stage A).
# It links a parser implementation only because dc_harness leaves
# parse_anvil_frame unresolved for the replay driver; the taxonomy pass itself
# never parses an Anvil frame — it reads the envelope and asks the venue's
# decoder to name each record.
add_executable(dc_taxonomy harness/src/dc_taxonomy_main.cpp)
target_link_libraries(dc_taxonomy PRIVATE dc_harness dc_engine_anvil dc_engine_kraken dc_engine_binance dc_warnings)

add_executable(dc_tests
  harness/tests/dc_tests.cpp
  harness/tests/alloc_probe.cpp
  harness/tests/test_age_estimator.cpp
  harness/tests/test_decimal.cpp
  harness/tests/test_anvil_adapter.cpp
  harness/tests/test_binance_adapter.cpp
  harness/tests/test_book.cpp
  harness/tests/test_console_ladder.cpp
  harness/tests/test_frame_reassembler.cpp
  harness/tests/test_gap_histogram.cpp
  harness/tests/test_http_response.cpp
  harness/tests/test_kraken_adapter.cpp
  harness/tests/test_kraken_checksum.cpp
  harness/tests/test_ladder_render.cpp
  harness/tests/test_panel_budget.cpp
  harness/tests/test_parser_equivalence.cpp
  harness/tests/test_reject_log.cpp
  harness/tests/test_replay_goldens.cpp
  harness/tests/test_resync.cpp
  harness/tests/test_venue_build.cpp
  harness/tests/test_rx_budget.cpp
  harness/tests/test_seed_schedule.cpp
  harness/tests/test_snapshot_channel.cpp
  harness/tests/test_stall_probe.cpp
  harness/tests/test_liveness_watchdog.cpp
  harness/tests/test_trace_records.cpp
  harness/tests/test_trace_venue.cpp
  harness/tests/test_ws_frame.cpp
  harness/tests/test_ws_ping.cpp
  harness/tests/test_window.cpp
  harness/tests/test_ws_supervisor.cpp)
# DC_TESTS_DIR is where the SHARED record-shape corpus lives (M5 stage A).
# `harness/tests/record_shapes.json` is replayed by test_trace_records.cpp AND
# by tools/tracefile.py's selfcheck, which is what makes "the C++ reader and the
# Python one agree on what they reject" a test rather than an inspection — the
# cases live outside both readers, so neither can quietly relax one.
target_compile_definitions(dc_tests PRIVATE
  DC_REPLAY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/harness/replay"
  DC_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/harness/tests"
  # The silent-stream defect fixture, named here so the test and the build agree
  # on one spelling. See DC_BINANCE_FIXTURE_NAME below for what it is and why it
  # is excluded from the graded lists.
  DC_BINANCE_FIXTURE="binance_btcusdt_DEFECT_silent_stream_20260826.ndjson"
  # The one committed capture long enough to CALIBRATE the liveness clock at a
  # ~20 s ping cadence (M5 stage B2). Spelled here rather than in the test for
  # the reason above it: a rename becomes a configure-time question instead of a
  # test that quietly stops reading the file it is named after.
  DC_BINANCE_LIVENESS_TRACE="binance_atomeur_d100ms_liveness_20260826.ndjson")
# M3 stages C, D, E and the transport rebuild: the firmware's WebSocket frame
# parser and reassembler, its gap histograms,
# the stall probe that classifies a >1 s book-hole, the reject log that captures
# what the parser threw away, the reconnect policy that decides how long the
# panel stays grey, the 64x64 ladder renderer and the font its row budget is
# computed from, the liveness watchdog that decides WHEN the panel greys (M4
# stage D — it replaced stage E's summary-deficit estimator, whose test went with
# it), and — from D5 — the client-ping round-trip that prices this socket's
# server-side send queue, are deliberately ESP-IDF-free so they can be tested
# here. These are the ONLY
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
  dc_harness dc_engine_anvil dc_engine_kraken dc_engine_binance dc_vendor dc_warnings Threads::Threads)

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
  dc_harness dc_engine_anvil_streaming dc_engine_kraken dc_engine_binance dc_vendor dc_warnings
  Threads::Threads)

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

# --- the Kraken arm's branch (M4 stage D, A3) ---------------------------------
# `venue_build.hpp` is a preprocessor fork and the default build only ever
# compiles the Anvil side of it. A branch proved solely by a flash is a branch
# nothing checks, so the second arm gets a link configuration of its own — the
# same argument, and the same shape, as dc_tests_noping above.
#
# It compiles the venue-conditional firmware headers — `venue_build.hpp` itself,
# and `reject_log.hpp`, whose status enum, its names, the width of its tally and
# its spliced-frame scan all come from the selected venue since this stage — plus
# the endpoint whose subscribe bytes are pinned against the committed corpus.
# `test_resync.cpp` and `test_liveness_watchdog.cpp` are venue-agnostic and are
# not duplicated here.
#
# THE `reject_log.hpp` HALF OF THAT SENTENCE WAS FALSE UNTIL REVIEW. This comment
# claimed the coverage and the link line did not provide it: nothing in the
# target's sources included the header, so a Kraken `parse_status_name` returning
# "?" for a value, or a `kParseStatusCount` off by one, went green on every one
# of the four ctest binaries. `test_venue_build.cpp` includes it now and asserts
# the tally on both arms. A comment that claims coverage the link line does not
# provide is the thing this project keeps writing rules about.
add_executable(dc_tests_kraken
  harness/tests/doctest_main.cpp
  harness/tests/test_venue_build.cpp)
target_compile_definitions(dc_tests_kraken PRIVATE
  DC_VENUE=2
  DC_REPLAY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/harness/replay")
target_include_directories(dc_tests_kraken PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/firmware/src)
target_link_libraries(dc_tests_kraken PRIVATE
  dc_engine dc_engine_kraken dc_vendor dc_warnings)

# The replay report, produced by the other parser. dc_replay prints per-frame
# statistics and the final ladder state; running both over both traces and
# diffing the output is the coarse-grained twin of the goldens above — it proves
# equality of the whole pipeline's output text, not just of the pinned fields.
add_executable(dc_replay_streaming harness/src/dc_replay_main.cpp)
target_link_libraries(dc_replay_streaming PRIVATE
  dc_harness dc_engine_anvil_streaming dc_engine_kraken dc_engine_binance dc_warnings)

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
add_test(NAME dc_tests_kraken COMMAND dc_tests_kraken)
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
# M4 STAGE B1: dc_ladder RUNS THESE NOW TOO. Stage A could only offer dc_replay,
# because the ladder drives the replay DRIVER and the driver had no Kraken
# adapter — it refused the venue loudly instead. With the adapter in, the whole
# chain (parse -> ladder -> truncate -> apply -> publish -> draw) runs end to
# end on every committed slice, which is the invariant-#6 claim for this stage
# and is a strictly stronger statement than "the reader can name the records".
foreach(_k d10 d25 d100)
  add_test(NAME dc_replay_kraken_btcusd_${_k}
    COMMAND dc_replay
            ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_btcusd_${_k}_20260816.ndjson)
  add_test(NAME dc_ladder_kraken_btcusd_${_k}
    COMMAND dc_ladder
            ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_btcusd_${_k}_20260816.ndjson
            --no-color)
endforeach()
# The quiet pair, which is the only artefact in the repository containing a
# nine-second healthy silence — deliverable 4's demonstration runs on this file.
add_test(NAME dc_replay_kraken_quiet_pair
  COMMAND dc_replay
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_minagbp_d25_20260816.ndjson)
add_test(NAME dc_ladder_kraken_quiet_pair
  COMMAND dc_ladder
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_minagbp_d25_20260816.ndjson
          --no-color)
# ...and the EXTREME of the same case, captured at a second hour (2026-08-17):
# 25,843 ms of healthy book silence with the heartbeat keeping 936-1,042 ms time
# straight through it. This is the trace the staleness ruling exists to serve,
# and the one that retired the 15,000 ms book-silence constant — at which it
# would have invented three disconnects on a feed that never lost a packet.
add_test(NAME dc_replay_kraken_quiet_pair_extreme
  COMMAND dc_replay
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_minagbp_d25_20260817.ndjson)
# The mid-stream slice through the LADDER: no subscribe, no ack, no snapshot, so
# the whole chain runs with a permanently stale book and an empty ladder. That is
# a different claim from "no data arrived" and nothing else committed makes it.
add_test(NAME dc_ladder_kraken_quiet_pair_extreme
  COMMAND dc_ladder
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_minagbp_d25_20260817.ndjson
          --no-color)

# THE RESYNC SLICE (M4 stage B2) — the artefact the triage has owed since stage
# A, and the only committed trace at any venue in which the CLIENT SPEAKS TWICE.
# One connection, one deliberate unsubscribe/re-subscribe provoked mid-capture,
# and a genuine mid-stream `book/snapshot` from the venue. Nothing about it is
# synthetic: the resubscribe is a thing a client does and the snapshot is a thing
# Kraken sent.
#
# What it holds that nothing else does: an ADAPTER-RAISED Gap — the unsubscribe
# ack drops the book and raises Gap{Resync} — a 1,047 ms grey window that no
# watchdog produced, a second baseline, and 19/19 checksums verified across both
# subscriptions. Committed on the quiet pair because a resync needs a wall-clock
# window long enough for two heartbeat calibrations and only a slow pair fits
# that in 14 KiB.
add_test(NAME dc_replay_kraken_resync
  COMMAND dc_replay
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_minagbp_d25_resync_20260818.ndjson)
add_test(NAME dc_ladder_kraken_resync
  COMMAND dc_ladder
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_minagbp_d25_resync_20260818.ndjson
          --no-color)

# THE COMMITTED BINANCE SLICES ARE NAMED, NOT GLOBBED, AND CHECKED BEFORE THE
# PYTHON GUARD — because whether a committed artefact is present is a question
# about this repository, not about whether a Python interpreter is installed.
#
# `file(GLOB)` resolves at CONFIGURE time, so a fresh clone, a stale cache or a
# renamed trace silently drops the test and the suite still reports green — at a
# total that reads as full coverage to whoever quotes it. This repository already
# holds the rule, from the Kraken pin table: an unpinned trace is a FAILURE, not a
# skip. A missing file here is a configure-time error naming the file, because the
# file list is part of what is being pinned.
#
# It sits outside `if(Python3_Interpreter_FOUND)` deliberately: review found that
# inside it, a machine without Python tolerated a missing committed slice in
# silence — the exact hole the rule exists to close.
set(DC_BINANCE_TRACE_NAMES
  binance_btcusdt_deepseed_20260824.ndjson
  binance_btcusdt_deepseed2_20260824.ndjson
  binance_atomeur_deepseed_20260824.ndjson
  binance_btcusdt_d100ms_20260824.ndjson
  binance_btcusdt_d1000ms_20260824.ndjson
  binance_atomeur_d100ms_20260824.ndjson
  binance_btcusdt_reconnect_20260824.ndjson
  # --- THE RULING'S EVIDENCE, COMMITTED WHOLE (M5 stage B2) ------------------
  # These two are NOT slices and must not be re-cut. They are the captures the
  # 2026-08-25 audit-stream ruling was signed on, and its figures -- 90/90
  # coincidence and 88/88 GREEN, twice -- are RATES OVER A COMPLETE CAPTURE. Cut
  # a window out of one and it no longer evidences the decision it was taken
  # for; you would be amending the ruling to fit the artefact. See
  # ARCHITECTURE.md 9's guard-trace-versus-ruling's-evidence row, and the
  # taxonomy rows for these two files, which say the same thing where a reader
  # cutting a window would be standing.
  binance_btcusdt_mixed1_20260825.ndjson
  binance_btcusdt_mixed2_20260825.ndjson
  # --- THE CALIBRATION CAPTURE (M5 stage B2) ---------------------------------
  # 221 s on the quiet pair, and the length is the property: nothing shorter can
  # reach `kMinSamples = 8` ping intervals, so nothing shorter lets a host test
  # enter the self-calibrating branch C has to tune. Also named separately as
  # DC_BINANCE_LIVENESS_TRACE for dc_tests.
  binance_atomeur_d100ms_liveness_20260826.ndjson)
set(DC_BINANCE_TRACES "")
foreach(dc_trace IN LISTS DC_BINANCE_TRACE_NAMES)
  set(dc_trace_path ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/${dc_trace})
  if(NOT EXISTS ${dc_trace_path})
    message(FATAL_ERROR
      "Committed Binance slice missing: ${dc_trace}. It is named in CMakeLists.txt "
      "because it is pinned by binance_tool_selfcheck and (for the deep-seed ones) "
      "by binance_oracle_mutants. Restore it, or remove it from BOTH this list and "
      "tools/binance_pins.py in the same commit — never from one.")
  endif()
  list(APPEND DC_BINANCE_TRACES ${dc_trace_path})
endforeach()
unset(dc_trace)
unset(dc_trace_path)

# --- THE DEFECT FIXTURE, AND IT IS DELIBERATELY NOT IN THE LIST ABOVE ---------
#
# `binance_btcusdt_DEFECT_silent_stream_20260826.ndjson` is the first artefact in
# this repository that can drive invariant #5's forbidden output ON DEMAND. M3's
# frozen ladder was real but incidental — a condition that happened once at the
# bench. This one is staged: a misspelled stream, captured live, in which the
# venue accepted the socket (HTTP 101), answered our pongs, sent three pings on
# the normal cadence, served a successful REST seed from a different host, and
# delivered ZERO depth frames. Replayed BEFORE M5 stage C, it produced a
# populated, coloured, LIVE ladder over a feed that had never spoken.
#
# **C LANDED AND THE TEST INVERTED (2026-08-26).** Replayed today it produces an
# honest GREY: remedy (a) withholds the Snapshot until a diff brackets the seed,
# so a seed the feed never confirmed is never published. The fixture now pins a
# CONTRACT where it used to pin a defect — see the READ THIS FIRST block in
# `harness/tests/test_binance_adapter.cpp`, which keeps the same account. The
# file keeps the `DEFECT` name deliberately: the defective CONDITION on the wire
# is exactly as real after the remedy as before it; what changed is what
# DepthCharge does with it. Renaming it would cost the provenance and buy a word.
#
# **IT IS NOT A GOLDEN AND IT IS NOT A WITNESS. IT IS A DEFECT FIXTURE**, which
# is a third thing, and the name is in capitals so that nobody has to read this
# comment to find that out. The convention is `NOT_A_CHECKSUM_GOLDEN`'s (M4 stage
# A): *cannot be graded* is a state beside *pinned* and *forgotten*, and it is
# stated rather than left to be inferred from an absence.
#
# SO IT IS EXCLUDED FROM THREE THINGS ON PURPOSE, and the exclusions are the
# point rather than an oversight:
#   * `binance_adapter_oracle`  — there is nothing to grade. No `@depth20`
#     payload, no diff, no opportunity. Handing it to the oracle would produce a
#     VACUOUS verdict that reads like a pass.
#   * `binance_tool_selfcheck`  — `tools/binance_pins.py` pins wire economics
#     over slices that carry a wire. This one carries none.
#   * `binance_oracle_mutants`  — the mutants need a book to break.
# It IS in the taxonomy pin and it IS read by dc_replay, because "an unpinned
# trace is a FAILURE, not a skip" applies to every committed file whatever it is
# for.
set(DC_BINANCE_FIXTURE_NAME binance_btcusdt_DEFECT_silent_stream_20260826.ndjson)
set(DC_BINANCE_FIXTURE
    ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/${DC_BINANCE_FIXTURE_NAME})
if(NOT EXISTS ${DC_BINANCE_FIXTURE})
  message(FATAL_ERROR
    "The silent-stream defect fixture is missing: ${DC_BINANCE_FIXTURE_NAME}. It is "
    "the only artefact that can drive invariant #5's forbidden output on demand, and "
    "`test_binance_adapter.cpp` asserts M5 stage C's remedy against it — the panel "
    "greys rather than colouring a seed the feed never confirmed. It is not the only "
    "red-before-green behind that remedy, but it is the only one over a LIVE capture "
    "and the only case reaching the never-bracketed path (`seed_bracket_ok == 0` AND "
    "`seed_bracket_failed == 0`). Restore it, or remove it in one commit from this "
    "block, the DC_BINANCE_FIXTURE compile definition near the top of this file, the "
    "taxonomy pin and that test — never from one.")
endif()

# --- the third venue's traces, read end to end (M5 stage A) -------------------
# Before this stage, dc_replay could read none of these: `binance` was a venue
# the build rejected by name, and the reader could not have represented two of
# the three things a Binance session produces even if it had known the venue —
# a REST snapshot body is not a frame the venue sent, and a ping payload is not
# JSON at all.
#
# THESE ARE dc_replay ONLY, AND dc_ladder IS DELIBERATELY ABSENT. The ladder
# drives the replay DRIVER, and the driver has no Binance adapter — it refuses
# the venue loudly, which is exactly where Kraken sat between M4 stage A and B1.
# A ladder test here would run the whole chain over a real capture, draw nothing,
# and exit 0: a decoder that emits nothing looks exactly like a feed that said
# nothing. B1 brings the adapter and these gain their ladder half then.
#
# The list is the NAMED one above, never a glob, for the reason stated there.
foreach(dc_trace IN LISTS DC_BINANCE_TRACES DC_BINANCE_FIXTURE)
  get_filename_component(dc_trace_stem ${dc_trace} NAME_WE)
  add_test(NAME dc_replay_${dc_trace_stem} COMMAND dc_replay ${dc_trace})
endforeach()
unset(dc_trace)
unset(dc_trace_stem)

# The taxonomy pin, over EVERY committed trace, Anvil included. An unpinned
# trace fails rather than skips (see dc_taxonomy_main.cpp): adding a committed
# trace without recording its figures would otherwise leave a green test that
# measures nothing about it.
#
# THE BINANCE SLICES JOIN IT AT M5 STAGE A, and they are appended from the named
# list rather than by widening the glob: a globbed pin is a pin that goes quiet
# when a file is renamed, which is the failure the Python-tool block below spells
# out at length. The Anvil and Kraken halves stay globbed because their rows have
# been in the table since M4 and a missing file there fails as "no pinned
# taxonomy on record" rather than by vanishing.
file(GLOB DC_COMMITTED_TRACES
  ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_*.ndjson
  ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_*.ndjson)
# THE REAL ADAPTER, GRADED (M5 stage B1). Pinned per slice rather than merely
# required to be green: one of the seven is RED by construction -- it was
# captured at `rest_limit: 100`, which stage 0 measured as wrong within 90 s on
# BTCUSDT -- and a check that demanded green everywhere would have to drop that
# slice or lie about it. Every matched count equals tools/binance_oracle.py's for
# the same file, which is two implementations in two languages agreeing on a
# book at every opportunity the venue gives.
add_test(NAME binance_adapter_oracle
  COMMAND dc_binance_oracle --check ${DC_BINANCE_TRACES})
set_tests_properties(binance_adapter_oracle PROPERTIES TIMEOUT 300)

# THE ANSWER IS PINNED IN BOTH DIRECTIONS AND AT ALL THREE VENUES, because a
# probe that only ever printed BLIND would be a probe that had stopped working.
# The socket-backlog column must TRACK everywhere — the arithmetic is venue-free
# and this is what says so — and the feed-backlog column must be BLIND
# everywhere, which is a statement about the ESTIMATOR rather than about Binance.
# What is Binance-only is that a feed-only backlog is a state the venue can
# actually be in; at Anvil and Kraken the liveness record and the book come from
# one subsystem and the column is a fiction. That distinction is the whole
# content of ARCHITECTURE.md 9's transport-versus-feed row, and it is here as
# three rows rather than a sentence.
add_test(NAME binance_age_probe
  COMMAND dc_age_probe --check
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/binance_atomeur_d100ms_liveness_20260826.ndjson
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/anvil_101_baseline.ndjson
          ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/kraken_btcusd_d25_20260816.ndjson)
set_tests_properties(binance_age_probe PROPERTIES TIMEOUT 300)

add_test(NAME trace_taxonomy_selfcheck
  COMMAND dc_taxonomy --selfcheck ${DC_COMMITTED_TRACES} ${DC_BINANCE_TRACES}
          ${DC_BINANCE_FIXTURE})
set_tests_properties(trace_taxonomy_selfcheck PROPERTIES TIMEOUT 300)

# Explicit timeouts on the two targets that now run threads. A hand-off bug that
# blocks the writer does not fail an assertion — it hangs — so the useful report
# is ctest saying "Timeout" in a minute rather than inheriting the 1500 s
# default and looking like the machine died.
set_tests_properties(dc_tests dc_tests_streaming dc_tests_noping dc_tests_kraken dc_channel_race
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

  # The SECOND Python tool held to the same standard (M4 stage B2), and it is
  # the one that writes committed artefacts rather than merely measuring them.
  #
  # `slice_trace.py` had no test of any kind until B2. Its reconnect mode cut
  # windows without checking that they contained the event they were named for,
  # which is the `t+65..t+160` defect of stage A one layer up: an order for a
  # 4-gap window that would have produced a 3-gap file. It takes NO trace
  # argument on purpose — the cases that matter are the ones no committed
  # capture contains, because every committed capture satisfies both rules and a
  # corpus-driven test here would be five copies of one observation.
  add_test(NAME slice_tool_selfcheck
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/slice_trace.py --selfcheck)
  set_tests_properties(slice_tool_selfcheck PROPERTIES TIMEOUT 120)

  # The THIRD venue's measurement tool (M5 stage 0), pinned on arrival rather
  # than after it has been believed — which is the whole lesson of the two
  # paragraphs above, applied before the cost is paid a third time.
  #
  # The reader every Python tool shares, tested for the first time (M5 stage 0,
  # at review). `tracefile.py` is the ONE definition of how a capture line is
  # read and it had no test of its own -- while its venue predicates each fall
  # through to the PREVIOUS venue's answer, so a venue added without an explicit
  # branch returns a confident wrong answer instead of failing. Mutation-verified:
  # deleting the binance branch from is_book_event turns three checks red.
  add_test(NAME tracefile_selfcheck
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/tracefile.py --selfcheck)
  set_tests_properties(tracefile_selfcheck PROPERTIES TIMEOUT 120)

  # The tool that WRITES ground truth, tested for the first time. It drives the
  # real capture loop against tools/ws_loopback.py -- no network, no venue -- and
  # covers the three paths a live capture cannot be asked for on demand: a server
  # PING answered and recorded, a REST fetch that FAILS and is recorded rather
  # than dropped, and rx_ns monotonicity across interleaved record kinds.
  #
  # It found two real defects on its first run: an in-flight REST fetch was
  # silently lost at end of capture (a fixed 0.3 s sleep against a 1.0-2.1 s
  # round trip), and the loopback was replaying REST bodies as WS text frames.
  add_test(NAME capture_binance_selfcheck
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/capture_binance.py --selfcheck)
  set_tests_properties(capture_binance_selfcheck PROPERTIES TIMEOUT 180)

  add_test(NAME binance_tool_selfcheck
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/binance_frame_economics.py
            ${DC_BINANCE_TRACES} --selfcheck)
  set_tests_properties(binance_tool_selfcheck PROPERTIES TIMEOUT 300)

  # THE ORACLE ITSELF IS THE THING WORTH PINNING, and this is the test that
  # says so. `--check` replays each deep-seed slice five times — once honestly,
  # once per deliberate breakage, and once for the mutant asserted to be a no-op
  # here — and fails unless the honest run is GREEN and all three mutants are
  # RED. That is ARCHITECTURE §9's 2026-08-16 clause (prove the trace catches a
  # broken implementation before pinning it) turned into a build product instead
  # of an evening's claim.
  #
  # THREE SLICES, NOT ONE, added at review (2026-08-24). With one witness the
  # exactness claim, the 884/884 and the whole mutant reach rested on one
  # capture, one pair, one day — and the tool measuring the coincidence is the
  # tool whose correctness that coincidence establishes.
  #
  # `deepseed2` is a SECOND BTCUSDT window taken later the same evening and is
  # the one that genuinely de-single-sources the mutant suite: 901/901
  # coincidence, 886/886 honest, all three mutants red, independently.
  #
  # The ATOMEUR one is a quiet-pair witness for the EXACTNESS claim only, and
  # the tool says so out loud: on a book that never churns, the
  # bounded-window mutant is byte-identical to the honest implementation at
  # every graded tick, so that trace CANNOT ask that question. It reports
  # NOT EXERCISABLE rather than passing, and `--check` additionally fails if
  # NO trace in the set exercises all three — because a suite whose every
  # witness is too quiet to break reports green for the same reason an empty
  # one does.
  #
  # NOT run against the other four slices. They are RED for a REAL reason — a
  # REST seed too shallow for their window — which is a finding recorded in
  # NOTES-binance.md rather than a defect to be tuned away here.
  add_test(NAME binance_oracle_mutants
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/binance_oracle.py
            ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/binance_btcusdt_deepseed_20260824.ndjson
            ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/binance_btcusdt_deepseed2_20260824.ndjson
            ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/binance_atomeur_deepseed_20260824.ndjson
            # THE TWO MIXED-CADENCE CAPTURES, ADDED AT M5 STAGE B2, AND THEY
            # CLOSE A GAP IN THE MUTANT SUITE'S REACH RATHER THAN ADDING A
            # FOURTH AND FIFTH COPY OF ITS EXISTING ONE. Every witness above
            # runs `@depth20@100ms`. **The board runs the audit stream at the
            # 1000 ms tick** (the 2026-08-25 ruling, decision 2), so until this
            # stage NO mutant had ever been run at the cadence the shipped
            # configuration will actually use -- a coalesced second of changes
            # per audit payload rather than a tenth. All three are exercised and
            # RED on both, measured before either file was pinned, which is the
            # 2026-08-16 clause discharged for them the same way it was for the
            # three above.
            ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/binance_btcusdt_mixed1_20260825.ndjson
            ${CMAKE_CURRENT_SOURCE_DIR}/harness/replay/binance_btcusdt_mixed2_20260825.ndjson
            --check)
  set_tests_properties(binance_oracle_mutants PROPERTIES TIMEOUT 300)
else()
  message(STATUS
    "SKIPPED (no Python 3 interpreter): kraken_tool_selfcheck, slice_tool_selfcheck, "
    "tracefile_selfcheck, capture_binance_selfcheck, binance_tool_selfcheck and "
    "binance_oracle_mutants. The Python measurement tools are unpinned in this "
    "configuration; the committed-slice existence check above still ran.")
endif()
