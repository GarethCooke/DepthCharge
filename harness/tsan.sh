#!/usr/bin/env bash
#
# tsan.sh — M3 stage A ThreadSanitizer driver for depthcharge::SnapshotChannel.
#
# Builds harness/tests/tsan_workload.cpp with -fsanitize=thread, runs it, and
# requires a CLEAN report: no TSan diagnostics and exit 0. Output is teed to
# harness/tests/tsan_clean.txt — the committed evidence the M3 brief's stage-A
# definition of done asks for. Exits nonzero on any race, so it drops straight
# into CI.
#
#   usage: ./harness/tsan.sh [seconds]        # workload duration, default 5
#
# Why this is not the CMake build. The workload is one self-contained TU over
# engine/include: no harness library, no nlohmann, no doctest. Compiling it
# directly keeps the report unambiguous about what was instrumented (a
# diagnostic can only be about the channel) and means the script needs nothing
# but a compiler — which matters, because this project's desk is Windows/MinGW
# and ThreadSanitizer is Linux-only, so this runs on WSL or a Linux box that may
# not have CMake at all. The same TU is built by `cmake --workflow --preset host`
# as dc_tsan_workload and run as the `dc_channel_race` ctest, so it cannot rot in
# between runs of this script.
#
# Toolchain: honours a caller-supplied CXX; otherwise prefers clang++ (best TSan
# support) and falls back to g++. The chosen compiler is printed and recorded in
# the report.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$HERE/build/tsan"
OUT="$HERE/harness/tests/tsan_clean.txt"
DUR="${1:-5}"

mkdir -p "$BUILD"

# ---- toolchain ----------------------------------------------------------------
if [ -n "${CXX:-}" ] && command -v "$CXX" >/dev/null 2>&1; then
  CXX="$(command -v "$CXX")"
elif command -v clang++ >/dev/null 2>&1; then
  CXX="$(command -v clang++)"
elif command -v g++ >/dev/null 2>&1; then
  CXX="$(command -v g++)"
else
  echo "tsan.sh: no C++ compiler (CXX override, clang++, or g++) found" >&2
  exit 2
fi
TOOLCHAIN="$("$CXX" --version 2>/dev/null | head -1)"
echo "tsan.sh: toolchain = $CXX ($TOOLCHAIN)"

# -std=c++2a rather than c++20: engine/ is already restricted to the subset the
# ESP32-S3's GCC 8.4 accepts (ARCHITECTURE §9, 2026-08-07), so the older spelling
# costs nothing and lets this run on an older host compiler too.
# -O1 -g: TSan needs frames it can symbolise; -O0 would change the interleaving
# for no benefit and -O2 costs stack accuracy.
echo "tsan.sh: building dc_tsan_workload with -fsanitize=thread"
if ! "$CXX" -std=c++2a -O1 -g -fsanitize=thread -fno-omit-frame-pointer \
      -Wall -Wextra -Wpedantic -Werror \
      -I "$HERE/engine/include" -I "$HERE/harness/tests" \
      "$HERE/harness/tests/tsan_workload.cpp" \
      -o "$BUILD/dc_tsan_workload" -pthread > "$BUILD/build.log" 2>&1; then
  echo "tsan.sh: build FAILED — see $BUILD/build.log" >&2
  tail -30 "$BUILD/build.log" >&2
  exit 1
fi

# ---- run ----------------------------------------------------------------------
# halt_on_error=1: stop at the first race. exitcode=66: a distinctive nonzero
# exit on a detected race (vs 0 clean, vs the workload's own 1 for a broken
# rule). second_deadlock_stack=1: fuller diagnostics if a deadlock is reported.
export TSAN_OPTIONS="halt_on_error=1 exitcode=66 second_deadlock_stack=1"

# libtsan maps its shadow memory at fixed addresses, and on modern kernels the
# default high-entropy ASLR can put the loader where it expects them — aborting
# at init with "FATAL: ThreadSanitizer: unexpected memory mapping" before the
# workload runs. That is a runtime-init failure, not a race. `setarch -R`
# de-randomises this process only (no root needed) and does not weaken
# detection: instrumentation and shadow bookkeeping are unchanged, only the base
# address is fixed.
RUN_UNDER=""
if command -v setarch >/dev/null 2>&1 && setarch -R true >/dev/null 2>&1; then
  RUN_UNDER="setarch -R"
fi

{
  echo "# dc_tsan_workload — ThreadSanitizer run (DepthCharge M3 stage A)"
  echo "# subject:  depthcharge::SnapshotChannel — engine/include/depthcharge/snapshot_channel.hpp"
  echo "# workload: harness/tests/tsan_workload.cpp (one feed thread, one render thread)"
  echo "# toolchain: $CXX ($TOOLCHAIN)"
  echo "# TSAN_OPTIONS=$TSAN_OPTIONS"
  echo "# ASLR: ${RUN_UNDER:-<none> (process ASLR left enabled)}"
  echo "# duration: ${DUR}s"
  echo "# $(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || echo 'date-unavailable')"
  echo "-----------------------------------------------------------------------"
} > "$OUT"

echo "tsan.sh: running ${DUR}s under TSAN_OPTIONS=\"$TSAN_OPTIONS\"${RUN_UNDER:+ ($RUN_UNDER)}"
$RUN_UNDER "$BUILD/dc_tsan_workload" "$DUR" >> "$OUT" 2>&1
rc=$?
echo "-----------------------------------------------------------------------" >> "$OUT"
echo "dc_tsan_workload exit = $rc" >> "$OUT"

# ---- verdict ------------------------------------------------------------------
# A detected race and a libtsan FATAL both exit with the configured 66 (Die()
# honours it), so classify by the report CONTENT: a genuine race prints "data
# race"; a runtime-init abort prints "FATAL: ThreadSanitizer:".
if grep -q "FATAL: ThreadSanitizer" "$OUT"; then
  echo "tsan.sh: TSan RUNTIME FAILED to initialise (a FATAL, NOT a data race) — see $OUT" >&2
  echo "         Typically high-entropy ASLR vs libtsan's fixed shadow layout. This" >&2
  echo "         script runs under 'setarch -R' when available; if that is missing," >&2
  echo "         lower vm.mmap_rnd_bits (e.g. to 28) or use a TSan-capable host." >&2
  exit 3
elif [ "$rc" -eq 0 ]; then
  echo "tsan.sh: CLEAN — dc_tsan_workload exited 0 with no TSan diagnostics."
  echo "tsan.sh: evidence -> $OUT"
  exit 0
elif grep -q "WARNING: ThreadSanitizer: data race" "$OUT" || [ "$rc" -eq 66 ]; then
  echo "tsan.sh: RACE DETECTED — ThreadSanitizer reported a data race (exit $rc)." >&2
  echo "tsan.sh: see $OUT" >&2
  exit 66
elif [ "$rc" -eq 1 ]; then
  echo "tsan.sh: the workload's OWN checks failed (torn frame or version regression)," >&2
  echo "         with no TSan diagnostic. That is a channel bug the sanitiser could" >&2
  echo "         not see — read the FAIL lines in $OUT." >&2
  exit 1
else
  echo "tsan.sh: dc_tsan_workload did NOT complete (exit $rc) and printed no TSan" >&2
  echo "         diagnostics. That is a run failure, not a detected race. See $OUT." >&2
  exit "$rc"
fi
