"""Derive every figure in a B3 soak write-up from a frozen serial capture.

WHY THIS EXISTS. During M4 stage D a heap-exhaustion alarm was raised from the
first and last samples of a noisy series and retracted an hour later by taking
medians. The series had not changed; the reading had. A two-point read of a noisy
series is not a measurement, and a number that reaches ARCHITECTURE section 9
must be reproducible from the evidence by something other than a person looking
at a log.

So: stdlib only, one input, every figure out, and the script is the authority. If
this disagrees with a hand-read number, the hand-read number is wrong until the
script is shown to be.

TWO CLOCKS, AND THEY ARE NOT THE SAME CLOCK. The board prints on two independent
time bases and they drift about 0.22% apart:

    I (nnn) / W (nnn)   FreeRTOS ticks, used by everything tagged `panel:`
    [nnn]               Arduino millis()/esp_timer, used by `ws_transport`

Subtracting one from the other produced two wrong published figures in this
milestone (a "14,750 ms subscribe stall" and a "14.5 s grey delay", both of them
skew). Every correlation below is therefore computed WITHIN one clock family, and
where a quantity is only available on the other clock it is reported separately
rather than joined.

Usage:  python tools/soak_report.py firmware/logs/FROZEN-kraken-b3-soak-20260822.log
"""
import gzip
import hashlib
import math
import re
import statistics
import sys

# --- line grammars, sampled from the capture rather than recalled ------------
RE_MARKER = re.compile(r'^### (\S+) (.*)$', re.M)
RE_SOAK = re.compile(
    r'^[IW] \((\d+)\) panel: SOAK venue=(\w+) up=(\d+)s live=(\d) age=(\S+) '
    r'worst_age=(\S+) baseline=(\d+)ms grey_n=(\d+) grey_ms=(\d+) wd=(\d+) '
    r'sock=(\d+) connects=(\d+) .*?resync_req=(\d+) heals=(\d+) owed=(-?\d+) '
    r'refused=(\d+) crc_fail=(\d+) heap=(\d+) largest=(\d+) frames=(\d+) drawn=(\d+)', re.M)
# `([\w-]+)` and not `(\w+)`: the reasons in the tree are `resync`, `seq-gap` and
# `checksum`, and a hyphen made this match 1 of 5 -- which then tripped the
# `not stale` abort below and killed the whole report as 'grammar drift'.
RE_STALE = re.compile(r'^W \((\d+)\) panel: \*\*\* STALE \(([\w-]+)\) at v(\d+)', re.M)
RE_LIVE = re.compile(r'^I \((\d+)\) panel: \*\*\* LIVE at v(\d+)', re.M)
RE_GREYFOR = re.compile(r'^I \((\d+)\) panel:\s+grey for (\d+) ms', re.M)
# NAMED GROUPS, and that is the fix rather than a style choice. `max_held=%u of %u`
# was inserted between `no_slot` and `qfull` by commit b9a37eb, which shifted
# qfull from index 4 to index 6 -- so a positional read of [4] would have printed
# max_held under the label `pipe qfull`, plausibly and wrongly. Named groups make
# the next insertion a no-op instead of a silent relabelling.
RE_PIPE = re.compile(
    r'^I \((?P<ticks>\d+)\) panel: -- pipe\s+: published=(?P<published>\d+) '
    r'oversize=(?P<oversize>\d+) no_slot=(?P<no_slot>\d+) '
    r'max_held=(?P<max_held>\d+) of (?P<slots>\d+) qfull=(?P<qfull>\d+)', re.M)
# `\[\s*(\d+)\]` and not `\[(\d+)\]`: the Arduino core pads millis to width 8,
# so `[  9316]` has leading spaces and these two matched ONLY on captures past
# ~2.8 h of uptime. That is why they looked right on the 25 h soak and returned
# nothing on every short one.
RE_DNS_FAIL = re.compile(r'^\[\s*(\d+)\].*warm_dns\(\).*did not resolve \(rc (\d+)\) after (\d+) ms', re.M)
RE_SOCK_UP = re.compile(r'^\[\s*(\d+)\].*socket up: dns (\d+) ms, connect\+upgrade (\d+) ms, '
                        r'fd (\d+), rssi (-?\d+) dBm', re.M)
# THE LIVENESS SIGNAL'S OWN CADENCE, added with the instrument at M5 stage D-A3.
# `-- signal` carries D-C's FIRST NAMED CHECK: `>=2x med` is the count of
# intervals that reached twice the median on a healthy socket, which is the
# falsifier stage C left for the multiplier. A tool that could not read this line
# could not answer the check the soak exists to answer.
RE_SIGNAL = re.compile(
    r'^I \((?P<ticks>\d+)\) panel: -- signal\s+: (?P<name>.*?) n=(?P<n>\d+) '
    r'max=(?P<max_ms>\d+) ms >=2x med=(?P<over>\d+) \| median (?P<median_ms>\d+) ms '
    r'threshold (?P<threshold_ms>\d+) ms (?P<state>[A-Z]+)', re.M)

# `-- age`'s liveness half. The signal NAME is free text with spaces, parentheses
# and an em dash -- it is `venue_build.hpp`'s `kLivenessSignal`, and D-A3 edited
# it -- so the capture group stays lazy and generic on purpose.
RE_AGE = re.compile(
    r'^I \((?P<ticks>\d+)\) panel: -- age\s+: (?P<age>\S+) \(worst (?P<worst>\S+)\) \| '
    r'baseline (?P<baseline_ms>\d+) ms \| (?P<signal>.*?) median (?P<median_ms>\d+) ms, '
    r'grey at (?P<threshold_ms>\d+) ms after (?P<samples>\d+) sample\(s\)', re.M)

# THE RE-SEED LEDGER (M5 stage D-A4's instrument, taught to this tool at the M5
# close-out). D-C's check 7 is READ OFF THIS LINE and nothing else — the panel
# cannot draw the marker on a live Binance header, so this is where the mechanism
# is visible at all.
#
# IT IS WHY THIS TOOL'S OWN PRE-FLIGHT WAS NOT A PRE-FLIGHT. D-C deliverable 1
# says "non-zero counts on every regex it owns", and a census over the grammars a
# tool happens to have is a statement about the tool, not about the capture —
# which is the reassuring instrument this project keeps naming, in the instrument
# built to prevent it.
#
# **AND THIS GRAMMAR HAS NEVER SEEN A REAL BOARD LINE, WHICH IS SAID HERE RATHER
# THAN LEFT TO BE DISCOVERED.** The first draft of this comment claimed the line
# "appeared thousands of times" in an existing capture; review checked, and
# `-- reseed` occurs **zero** times in `bench-2026-08-30-D-C-soak.log`, zero in
# `bench-2026-09-04-E-soak.log` and zero in every file under `firmware/logs/` —
# every one of them predates M5 stage D-A4, and they carry the older
# `-- seed … reseeds=N` line instead. An unverified claim about a capture, inside
# the fix for *a statement about the tool rather than about the capture*, is that
# same failure one level up. What IS verified: the grammar is checked field for
# field against `render_task.cpp`'s format string, `--selfcheck` exercises it and
# the `cover=-/-` sentinel form, and it parses the worked example in
# `docs/briefs/M5-stage-D-C-the-soak.md` §4. **Its first real capture will be
# D-C's second run**, which is the run that needs it.
#
# `cover=%s/%s` carries `-` for either side while `have_seed_bounds_` is false
# (the sentinel is 0xFFFFFFFF and printing four billion as a depth would be that
# same failure again), so the two cover groups admit a bare hyphen and the
# readers below check for it rather than calling int() and dying.
RE_RESEED = re.compile(
    r'^I \((?P<ticks>\d+)\) panel: -- reseed\s+: adopted=(?P<adopted>\d+) '
    r'unbracketed=(?P<unbracketed>\d+) hold-overflow=(?P<hold_overflow>\d+) '
    r'\| declined\(no-hold\)=(?P<declined>\d+) adoptable=(?P<adoptable>\d+) '
    r'\| triggers=(?P<triggers>\d+) below=(?P<below>\d+) '
    r'cover=(?P<cover_bid>[-\d]+)/(?P<cover_ask>[-\d]+) of (?P<cover_target>\d+)', re.M)

# THE BOOT CLOCK. Every `panel:` line carries the FreeRTOS tick count, which
# restarts at every reset — power-on, software, or watchdog abort alike — so a
# DECREASE in it is a reboot and its file offset is where the next boot begins.
#
# This is the segmentation key rather than `up=` because `up=` exists only on
# SOAK lines: a `-- reseed` or `-- pipe` line cannot be placed by it, and those
# are exactly the per-boot counters that need placing. `BinanceAdapter::Stats`
# is a plain member and nothing anywhere resets it, so every figure on those
# lines is per-boot and dies at reboot — reading "the last one in the file"
# silently discards every boot but the last, which is what made D-C's first run
# (seven boots, 34.55 h of summed uptime) report a 4.45 h header.
RE_PANEL_TICK = re.compile(r'^[IW] \((\d+)\) ', re.M)

# The ROM's own reset line, printed before any application code runs — so it
# counts boots without reference to the tick, and says WHY each one happened
# (`POWERON`, `RTC_SW_CPU_RST`, `TG0WDT_SYS_RST`, …). Used to cross-check
# `boot_starts`, never to replace it: a capture that attaches mid-boot has no
# `rst:` line for the boot it walked into, which is the case the tick covers.
RE_RESET_REASON = re.compile(r'^rst:0x[0-9a-fA-F]+', re.M)

RE_AUTOPSY = re.compile(r'assoc=(\d+)')
RE_WIFI = re.compile(r'wifi down|rejoining|wifi up:')
# The two shapes that are actually a LOSS. `wifi up:` is the association
# succeeding and appears once in every capture that includes a boot.
RE_WIFI_DROP = re.compile(r'wifi down|rejoining')

# The `time` monitor filter's prefix. Anchored so it can only ever take a real
# timestamp off the front of a line.
RE_TIME_PREFIX = re.compile(r'(?m)^\d{2}:\d{2}:\d{2}\.\d{3} > ')

# --- the regex census -------------------------------------------------------
#
# EVERY GRAMMAR THIS TOOL OWNS REPORTS ITS OWN MATCH COUNT, because the failure
# this file kept having is not a wrong number, it is a MISSING SECTION. Six
# guards here were of the shape `if pipe:` with no else -- when a grammar
# drifted the block simply did not print, and a report with a section missing
# reads exactly like a run in which that section had nothing to say.
#
# It does not exit non-zero on its own: a healthy short capture legitimately has
# no DNS failures and no autopsies. What it removes is the SILENCE.
#
# EVERY GRAMMAR MEANS EVERY GRAMMAR (M5 close-out, and it did not before). D-C's
# session log recorded four that were read and printed but never enrolled —
# `RE_STALE`, `RE_LIVE`, `RE_GREYFOR`, `RE_SOCK_UP` — so a drift in any of them
# still produced a silently missing section, which is the exact failure the
# census exists to remove, surviving inside the census. `RE_RESEED` was worse: it
# did not exist, and D-C's deliverable-1 pre-flight — *"non-zero counts on every
# regex it owns"* — therefore PASSED while covering none of the reading that
# stage was inherited to make. **A census over the grammars a tool happens to
# have is a statement about the tool, not about the capture.**
#
# THE EXCEPTIONS, EACH NAMED WITH ITS REASON, because "enrol everything" without
# a stated boundary is the next thing to go quiet:
#
#   RE_TIME_PREFIX  Not a line grammar. Its count is reported on its own line in
#                   PROVENANCE (`ts-prefix`), where a half-prefixed file — two
#                   captures concatenated — is the thing being watched for.
#   RE_MARKER       The capture tool's own `###` annotations, not the firmware's
#                   output. Reported in PROVENANCE with its opens/losses/gaps
#                   breakdown; a capture legitimately has none.
#   RE_WIFI         A strict superset of RE_WIFI_DROP kept only for one printed
#                   total. Enrolling both would double-count one event class and
#                   make the census read as more coverage than there is.
#   RE_PANEL_TICK   The boot clock, not a payload. It matches every ESP-IDF
#                   `I (n)`/`W (n)` line of ANY tag — `idle:`, `rest:`, `seed:`
#                   as well as `panel:`, which is what makes the segmentation
#                   work — so a zero here is already a zero on every other
#                   grammar. Reported instead as the boot-boundary count.
#   RE_RESET_REASON The ROM's `rst:0x..`, printed before any application code and
#                   used only to cross-check the boot count. Reported on its own
#                   line beside that count, where a disagreement is the finding;
#                   a legitimate zero (a capture that attached mid-boot) would
#                   read as drift in the census.
_CENSUS = []


def owned(name, value):
    # Takes either a count or the match list itself and returns it UNCHANGED, so
    # a call site can be wrapped around an existing expression without moving it.
    _CENSUS.append((name, value if isinstance(value, int) else len(value)))
    return value


def census():
    section('REGEX CENSUS - what this tool claims to have read')
    dead = [n for n, c in _CENSUS if c == 0]
    for name, count in _CENSUS:
        flag = '   <-- NO MATCHES' if count == 0 else ''
        print(f'  {name:<24} {count:>7}{flag}')
    if dead:
        print()
        print(f'{len(dead)} grammar(s) matched nothing: {", ".join(dead)}')
        print('Some are legitimately empty on a healthy run (DNS failures, autopsies).')
        print('A grammar that SHOULD have matched and did not is drift, and it is the')
        print('reason this table exists.')

NEWLINE = b'\n'


def fmt(n):
    return f'{n:,}'


def section(title):
    print()
    print('=' * 78)
    print(title)
    print('=' * 78)


# --- boots ------------------------------------------------------------------
#
# WHY THIS EXISTS: a capture is not a run, it is a SEQUENCE of runs, and every
# counter on a `panel:` line is per-boot because nothing resets them in software.
# Reading the last line of a file as "the run" understated D-C's first capture by
# 30 of its 34.55 hours and would have discarded six boots' worth of the one
# counter that stage was inherited to read.

# A decrease is only a reboot if the clock RESTARTED: the new value has to land
# inside the first minute of a boot AND the drop has to be far larger than two
# tasks can race by. See boot_starts() for what each clause is for, and for the
# two wrong rules that came before them.
BOOT_TICK_CEILING_MS = 60_000
MIN_RESTART_DROP_MS = 1_000


def boot_starts(text, verbose=False):
    """File offsets at which a new boot begins, from the panel clock's resets.

    THE NAIVE RULE — *any* decrease is a reboot — IS WRONG, AND THE D-C CAPTURE
    SAYS SO OUT LOUD: it reports **27 boots where there are 7**. The log
    timestamp is stamped when a line is FORMATTED and the line is written by
    whichever task formatted it, so two tasks logging concurrently put their
    lines in the file slightly out of order. Every spurious boundary in that
    capture is a `rest:` line landing 4-46 ms behind a `panel:` line:

        I (7163561) panel: SOAK note: ...
        I (7163548) rest:  fetch OK(none) HTTP 200 64046 B in 4129 ms ...

    A real reboot looks completely different — the tick RESTARTS, to `I (341)
    idle: per-core idle probe up at 240 MHz`, from wherever it had reached:

        I (14383346) panel: v1571506 seq=1562247 LIVE ...
        I (341) idle: per-core idle probe up at 240 MHz ...

    So the test is on the NEW value, not on the size of the drop: a boundary is a
    decrease landing inside the first `BOOT_TICK_CEILING_MS` of a boot. An
    out-of-order write is the same magnitude as the line it overtook and can
    never satisfy that, at any uptime; a reset always does, because the idle
    probe logs at ~341 ms on every boot.

    The out-of-order decreases are COUNTED AND REPORTED rather than silently
    swallowed — 20 of them in 490,498 lines here, which is a fact about the
    board's logging that nothing else in this tool would show.
    """
    starts = [0]
    prev = None
    out_of_order = 0
    for m in RE_PANEL_TICK.finditer(text):
        tick = int(m.group(1))
        if prev is not None and tick < prev:
            # BOTH THE DESTINATION AND THE SIZE OF THE DROP ARE TESTED, and the
            # second clause is the one review had to point out. Requiring only
            # `tick < ceiling` says nothing about where the drop came FROM, so an
            # out-of-order write INSIDE the first minute of a boot forges a
            # boundary -- and forges it silently, because the branch that would
            # have counted it as out-of-order is the one it escaped. The exposure
            # is not hypothetical: every boot of the D-C capture carries 168-225
            # lines with a tick under 60 s, from four concurrently logging tasks,
            # the highest reaching 59,734 ms. It has not fired on any committed
            # capture (0 across all three) and it is the identical species as the
            # 27-boots-from-7 defect, relocated into the first minute.
            #
            # **THE OBVIOUS GUARD -- `prev >= ceiling`, i.e. a restart must come
            # down from a RUNNING clock -- IS THE WRONG ONE**, and the selfcheck
            # said so immediately: it makes a boot that resets within 60 s of
            # starting invisible, merging it into its predecessor. That trades a
            # false split for a false MERGE, and a board crash-looping in its
            # first minute is exactly when per-boot reading is worth having.
            #
            # The size of the drop separates the two cleanly instead. An
            # out-of-order write is two tasks racing to the UART: 4-46 ms across
            # the whole D-C capture. A restart returns to ~341 ms from wherever
            # the clock had reached. One second is a 20x margin over the observed
            # worst race and still catches a boot that lived only a few seconds.
            if tick < BOOT_TICK_CEILING_MS and (prev - tick) >= MIN_RESTART_DROP_MS:
                starts.append(m.start())
            else:
                out_of_order += 1
        prev = tick
    if verbose:
        # THE SECOND WITNESS, AND IT IS INDEPENDENT OF THE TICK ENTIRELY.
        # The ROM prints a reset reason on every boot, so `rst:0x..` counts
        # resets without reference to the clock this function is reading -- 7
        # for 7 on the D-C capture, 2 for 2 on stage E's. It is reported rather
        # than substituted because it is absent when the monitor attaches
        # mid-boot (the capture, not the board, decides), which is exactly the
        # case the tick handles and this does not. Two witnesses that disagree
        # is a thing to say out loud, not to resolve by preferring one.
        resets = len(RE_RESET_REASON.findall(text))
        print(f'boot boundaries   : {len(starts)} (panel clock restarts)')
        print(f'out-of-order lines: {out_of_order}   (two tasks logging '
              f'concurrently; a decrease that is not a restart)')
        print(f'ROM reset lines   : {resets} `rst:0x..`   '
              f'(independent of the tick; a mid-boot attach has fewer)')
        if resets and resets != len(starts):
            print(f'  *** the two boot witnesses DISAGREE: {len(starts)} by panel '
                  f'clock, {resets} by ROM reset reason. A capture that attached')
            print('  *** mid-boot explains ROM < clock; anything else wants reading.')
    return starts


def boot_of(offset, starts):
    """0-based boot index for a match at `offset`. Linear-scan free: bisect."""
    lo, hi = 0, len(starts) - 1
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if starts[mid] <= offset:
            lo = mid
        else:
            hi = mid - 1
    return lo


def by_boot(matches, starts, n_boots):
    """Bucket (offset, value) pairs into a list of per-boot lists."""
    out = [[] for _ in range(n_boots)]
    for offset, value in matches:
        out[boot_of(offset, starts)].append(value)
    return out


def per_boot_table(per, header, row_of, total_of=None, absent='(no lines)'):
    """One per-boot table, for every section that prints one.

    THE FOUR SECTIONS HAD FOUR SHAPES, and the differences were all accidental —
    two guarded the table on `n_boots > 1` and one did not, and three silently
    SKIPPED a boot with no lines while THE RUN listed it. A boot present in one
    table and absent from another, with nothing said, is the sort of gap this
    tool exists to close, so absence is now printed rather than skipped and the
    numbering is `boot index + 1` in one place instead of four.

    `per` is `by_boot`'s output; `row_of(last_line, all_lines_this_boot)` returns
    the row text; `total_of(per)` the optional TOTAL line.
    """
    print(header)
    for n, g in enumerate(per):
        if g:
            print(f'  B{n + 1:<4} {row_of(g[-1], g)}')
        else:
            print(f'  B{n + 1:<4} {absent}')
    if total_of is not None:
        print(f'  TOTAL {total_of(per)}')


def read_capture(path):
    """The capture's bytes, gzipped or not, with the sha256 of what is ON DISK.

    THE COMMITTED CAPTURES ARE `.gz` AND THIS TOOL COULD NOT READ THEM, which is
    a gap between the instrument and the evidence rather than a convenience:
    `hardware/bench-2026-08-30-D-C-soak.log.gz` is the artefact in the tree, and
    running the tool on it exited 1 with *"NO SOAK LINES PARSED - the grammar has
    drifted from the firmware"* — a grammar verdict on a compression format.
    That is a false diagnosis of exactly the kind this file exists to remove, and
    it meant every write-up citing "reproduces from `python tools/soak_report.py
    <capture>`" was citing a command that fails on the committed input.

    The sha256 stays the sha256 of the FILE, not of the inflated bytes, so it
    still matches what a bench record pins and what `git` stores.
    """
    blob = open(path, 'rb').read()
    digest = hashlib.sha256(blob).hexdigest()
    if blob[:2] == b'\x1f\x8b':
        # BOTH DIGESTS, because the bench records pin the INFLATED one and git
        # stores the compressed one, and a reader checking a provenance line
        # needs whichever the record used. `bench-2026-08-22-kraken-b3-soak.md`
        # and M4 stage D pin `6a9139f6…fa8` — the sha of the 33,481,892 B frozen
        # log, not of the 3,373,814 B gzip beside it — and D-C's record pins
        # `d4c4fd12…` the same way. Printing only the file's own sha would leave
        # every one of those claims unverifiable from the committed artefact,
        # which is the gap this whole change is closing.
        raw = gzip.decompress(blob)
        return raw, digest, len(blob), hashlib.sha256(raw).hexdigest()
    return blob, digest, len(blob), None


def main(path):
    raw, digest, on_disk, inflated_digest = read_capture(path)
    blob = raw
    text = blob.decode('utf-8', 'replace').replace('\r', '')

    # THE TIMESTAMP PREFIX, STRIPPED ONCE AND IN ONE PLACE.
    #
    # `firmware/platformio.ini:215` bakes `time` into `monitor_filters`, so
    # every line of every capture taken since begins `HH:MM:SS.mmm > `. Every
    # `^`-anchored grammar below predates that and matched NOTHING on a current
    # log -- the tool printed "the grammar has drifted from the firmware" and
    # exited 1, which is the right message for the wrong reason.
    #
    # Idempotent by construction: on an older capture with no prefix this is a
    # no-op, so one tool still reads both eras. The count is PRINTED because a
    # half-prefixed file -- two captures concatenated, say -- is otherwise
    # invisible and would silently halve every series.
    text, prefixed = RE_TIME_PREFIX.subn('', text)

    section('PROVENANCE')
    print(f'file    : {path}')
    print(f'sha256  : {digest}   (of the file on disk)')
    if inflated_digest:
        print(f'sha256  : {inflated_digest}   (INFLATED — the one bench records pin)')
    print(f'bytes   : {fmt(on_disk)} on disk'
          + (f', {fmt(len(blob))} inflated (gzip)' if inflated_digest else ''))
    print(f'lines   : {fmt(blob.count(NEWLINE))}')
    print(f'ts-prefix: {fmt(prefixed)} line(s) carried the monitor timestamp')
    marks = RE_MARKER.findall(text)
    print(f'markers : {len(marks)}')
    if marks:
        print(f'  first : {marks[0][0]}  {marks[0][1][:60]}')
        print(f'  last  : {marks[-1][0]}  {marks[-1][1][:60]}')
    lost = [m for m in marks if m[1].startswith('PORT LOST')]
    gaps = [m for m in marks if m[1].startswith('GAP')]
    fails = [m for m in marks if m[1].startswith('OPEN FAILED')]
    opens = [m for m in marks if m[1].startswith('PORT OPEN')]
    print(f'capture : {len(opens)} port opens, {len(lost)} losses, '
          f'{len(gaps)} gaps, {len(fails)} open failures')

    # --- the boots -----------------------------------------------------------
    print()
    starts = boot_starts(text, verbose=True)
    n_boots = len(starts)

    # --- the SOAK series ----------------------------------------------------
    soak_m = list(RE_SOAK.finditer(text))
    soak = owned('SOAK', [m.groups() for m in soak_m])
    if not soak:
        # THE CENSUS PRINTS EVEN HERE, and that is the whole point of it. This
        # abort used to `return 1` straight out, skipping the one table that says
        # WHICH grammars matched — on the single run where a grammar has provably
        # drifted, which is the run the census was built for. Every figure below
        # is derived from the SOAK series and there is genuinely nothing else to
        # compute, so the report still stops; it stops after saying what it read.
        print('NO SOAK LINES PARSED - the grammar has drifted from the firmware.')
        print('The census below is what this run DID match, which is the diagnostic.')
        census()
        return 1
    soak_boot = [boot_of(m.start(), starts) for m in soak_m]
    up = [int(r[2]) for r in soak]
    live = [int(r[3]) for r in soak]
    worst_age = [r[5] for r in soak]
    grey_n = [int(r[7]) for r in soak]
    grey_ms = [int(r[8]) for r in soak]
    wd = [int(r[9]) for r in soak]
    sock = [int(r[10]) for r in soak]
    connects = [int(r[11]) for r in soak]
    resync = [int(r[12]) for r in soak]
    heals = [int(r[13]) for r in soak]
    owed = [int(r[14]) for r in soak]
    refused = [int(r[15]) for r in soak]
    crc_fail = [int(r[16]) for r in soak]
    heap = [int(r[17]) for r in soak]
    largest = [int(r[18]) for r in soak]

    # Row indices of each boot's SOAK lines, in file order. Boots with no SOAK
    # line keep their slot and are skipped when printing, so that `B3` means the
    # same boot in every section below rather than the third boot that happened
    # to produce a SOAK line.
    boot_rows = [[] for _ in range(n_boots)]
    for i, b in enumerate(soak_boot):
        boot_rows[b].append(i)
    with_soak = [n for n, rows in enumerate(boot_rows) if rows]

    section('THE RUN')
    print(f'SOAK lines        : {fmt(len(soak))}')
    print(f'boots seen        : {n_boots} by panel clock, {len(with_soak)} of them '
          f'with SOAK lines')

    # THE TWO REBOOT COUNTS ARE PRINTED TOGETHER AND CROSS-CHECKED, because they
    # come from different evidence: `up=` decreasing across consecutive SOAK
    # lines, and the FreeRTOS tick decreasing across every `panel:` line. A boot
    # that produced no SOAK line at all is invisible to the first and visible to
    # the second, and a mangled tick would show up as a boot the first has never
    # heard of. Disagreement is reported rather than resolved silently.
    reboots = [(up[i - 1], up[i]) for i in range(1, len(up)) if up[i] < up[i - 1]]
    print(f'REBOOTS (up=)     : {reboots if reboots else "NONE - uptime monotonic"}')
    if len(reboots) + 1 != n_boots:
        print(f'  *** the two boot counts DISAGREE: {len(reboots) + 1} by uptime, '
              f'{n_boots} by panel clock. A boot with no SOAK line, or a mangled')
        print('  *** tick. The per-boot table below is on the PANEL CLOCK.')

    # PER BOOT, AND THIS IS THE SECTION D-C's DECISION 3 EXISTS FOR. Reading the
    # last SOAK line as "the run" reported 4.45 h of a 34.55 h capture: every
    # counter on the line is per-boot, so the final row is the final BOOT.
    print()
    print('  boot   uptime span            grey            wd  sock  conn  live_end')
    total_up = 0
    total_grey = 0
    for n in with_soak:
        rows = boot_rows[n]
        a, b = rows[0], rows[-1]
        total_up += up[b]
        total_grey += grey_ms[b]
        pct = 100.0 * grey_ms[b] / (up[b] * 1000) if up[b] else 0.0
        print(f'  B{n + 1:<5} {up[a]:>6}s -> {up[b]:>7}s ({up[b] / 3600:>5.2f} h)  '
              f'{grey_ms[b] / 60000:>7.1f} min {pct:>5.1f}%  '
              f'{wd[b]:>3} {sock[b]:>5} {connects[b]:>5} {live[b]:>9}')
    print(f'  TOTAL  {total_up / 3600:.2f} h of board uptime across {len(with_soak)} boot(s), '
          f'grey {total_grey / 60000:.1f} min = '
          f'{(100.0 * total_grey / (total_up * 1000)) if total_up else 0.0:.2f}%')

    print()
    print(f'final boot counters: grey_n={grey_n[-1]} wd={wd[-1]} sock={sock[-1]} '
          f'connects={connects[-1]}')
    print(f'                    resync_req={resync[-1]} heals={heals[-1]} '
          f'crc_fail={crc_fail[-1]} refused={refused[-1]} owed={owed[-1]}')
    print(f'worst_age (final boot): {worst_age[-1]}')
    if len(with_soak) > 1:
        print('  ^ FINAL BOOT ONLY. These counters are not reset in software, so')
        print('    they die at every reboot. The per-boot table above is the run.')

    # --- counter step ordering: wd vs sock ----------------------------------
    section('WATCHDOG VERSUS SOCKET - which noticed first')
    # WITHIN A BOOT, and that is a correction rather than a refinement: `wd` and
    # `sock` restart at 0 at every reset, so a comparison spanning a boundary
    # reads the restart as a counter going backwards and pairs a watchdog firing
    # in one boot with a socket loss in the next.
    for n in with_soak:
        rows = boot_rows[n]
        steps = []
        for prev, i in zip(rows, rows[1:]):
            if wd[i] != wd[prev]:
                steps.append((up[i], 'wd', wd[prev], wd[i]))
            if sock[i] != sock[prev]:
                steps.append((up[i], 'sock', sock[prev], sock[i]))
        b = rows[-1]
        print(f'  B{n + 1}: watchdog firings {wd[b]}, socket losses {sock[b]} -- '
              f'{"NOT every socket loss was preceded by one" if sock[b] > wd[b] else "paired"}')
        for u, what, a2, b2 in steps:
            print(f'       up={u:>7}s  {what:<4} {a2}->{b2}')
        # Pair each wd step with the next sock step, on the SAME clock and in
        # the same boot.
        for w in [s for s in steps if s[1] == 'wd']:
            later = [s for s in steps if s[1] == 'sock' and s[0] >= w[0]]
            if later:
                print(f'       wd at {w[0]}s -> next sock at {later[0][0]}s '
                      f'= watchdog was {later[0][0] - w[0]}s EARLIER '
                      f'(SOAK cadence is 10s, so +/-10s)')

    # --- grey episodes ------------------------------------------------------
    section('GREY EPISODES')
    grey_m = list(RE_GREYFOR.finditer(text))
    owned('grey for', len(grey_m))
    greys = [int(m.group(2)) for m in grey_m]
    if n_boots > 1:
        per = by_boot([(m.start(), int(m.group(2))) for m in grey_m], starts, n_boots)
        print('per boot: ' + '  '.join(
            f'B{n + 1}={len(g)} ep/{sum(g) / 60000:.1f} min'
            for n, g in enumerate(per) if g))
        print()
    if greys:
        s = sorted(greys)
        LONG = 10_000
        quick = [x for x in s if x <= LONG]
        big = [x for x in s if x > LONG]
        print(f'episodes          : {len(s)}   total {sum(s) / 1000:.0f}s '
              f'({sum(s) / 60000:.1f} min)')
        print(f'median            : {statistics.median(s):.0f} ms')
        # `quick` GUARDED, LIKE `big` ALREADY WAS. On the D-C capture the MEDIAN
        # grey episode is 18,249 ms, so a shorter capture of that same board
        # trivially has no episode at or under 10 s — and `statistics.median([])`
        # raises, taking the whole report with it. The asymmetry was there from
        # the start; the per-boot change is what made a board that greys for
        # minutes at a time the normal case to read.
        if quick:
            print(f'  <= {LONG} ms    : n={len(quick)}  '
                  f'median={statistics.median(quick):.0f} ms  '
                  f'max={max(quick)} ms  total={sum(quick) / 1000:.0f}s')
        else:
            print(f'  <= {LONG} ms    : n=0   (every episode was longer)')
        # CAPPED, because this printed the whole list. On the Kraken B3 capture
        # `big` held a handful of episodes and naming each was the reading; on a
        # board that greys for 71% of its uptime it holds 4,408, and the line
        # became four hundred wrapped rows of "0.2 min" that pushed every other
        # section off the screen. A list is a reading when it is short and noise
        # when it is not, and the cutoff has to be in the code rather than in the
        # capture.
        SHOWN = 12
        shown = [f'{x / 60000:.1f} min' for x in sorted(big, reverse=True)[:SHOWN]]
        tail = f'  ... and {len(big) - SHOWN} more' if len(big) > SHOWN else ''
        print(f'  >  {LONG} ms    : n={len(big)}  longest {shown}{tail}  '
              f'total={sum(big) / 1000:.0f}s')
        if big:
            print(f'  the {len(big)} long ones are {100.0 * sum(big) / sum(s):.0f}% of all grey time')

    # --- (a) CRC clustering -------------------------------------------------
    section('(a) ARE THE CRC FAILURES CLUSTERED? - panel clock only')
    stale_m = list(RE_STALE.finditer(text))
    live_m = list(RE_LIVE.finditer(text))
    stale = owned('*** STALE', [(int(m.group(1)), m.group(2)) for m in stale_m])
    lives = owned('*** LIVE', [int(m.group(1)) for m in live_m])
    # NOT `return 1`, AND THAT WAS BACKWARDS TWICE OVER. This used to abort the
    # whole report — skipping the census, which is the one thing worth printing
    # on a run where a grammar HAS drifted — and it aborted on a capture that
    # legitimately has neither line, which every clean short Binance run is. The
    # `(b2)` and `(b3)` sections already handle the same situation the right way:
    # say what is absent and carry on.
    if not stale or not lives:
        print('no `*** STALE`/`*** LIVE` lines. Either the panel never greyed on this')
        print('capture, or the grammar has drifted. The REGEX CENSUS at the end says')
        print('which: a zero beside a capture that plainly greyed is drift.')
    reasons = {}
    for _, r in stale:
        reasons[r] = reasons.get(r, 0) + 1

    # PER BOOT, AND WITHOUT IT THIS SECTION PUBLISHES A NEGATIVE PROBABILITY.
    # Every figure below is a difference of panel ticks, and the tick restarts at
    # every boot — so a later boot's checksum STALE can sit at a LOWER tick than
    # an earlier boot's, making `csum[-1] - csum[0]` negative, the mean
    # inter-arrival negative, the Poisson baseline negative and **the verdict
    # flip**. It is the same defect as reading the last SOAK line as the run,
    # arriving in the one section that ends in a printed conclusion, and the
    # first version of the per-boot change segmented four other sections and
    # left this one alone.
    #
    # The criterion is per-boot by nature anyway: "did a failure land within 10 s
    # of the preceding heal" cannot span a reboot, because the heal before a
    # reset is not the heal before the failure.
    csum_by_boot = by_boot([(m.start(), int(m.group(1)))
                            for m in stale_m if m.group(2) == 'checksum'],
                           starts, n_boots)
    lives_by_boot = by_boot([(m.start(), int(m.group(1))) for m in live_m],
                            starts, n_boots)
    csum = [t for g in csum_by_boot for t in g]
    # The strict grammar needs the trailing ` at vNNN` to get a version; a
    # lenient count catches events whose line was mangled by two tasks writing
    # the UART at once (9 such lines in the 2026-08-22 capture, 0.003%).
    #
    # ENROLLED, because it is a grammar and the census says EVERY grammar. It was
    # the seventeenth and it was neither enrolled nor named as an exception, which
    # is the census's own failure mode in the paragraph that lists the exceptions.
    # Its drift is *visible* — the "too mangled" count would go negative rather
    # than silent — but "visible if you read it carefully" is the standard this
    # tool exists to replace.
    lenient = len(owned('*** STALE (checksum) lenient',
                        re.findall(r'\*\*\* STALE \(checksum\)', text)))
    print(f'STALE events by reason : {reasons}  (total {len(stale)})')
    print(f'STALE(checksum) lenient: {lenient}   '
          f'({lenient - len(csum)} line(s) too mangled for the strict grammar)')
    print(f'LIVE events            : {len(lives)}')
    print(f'counter crc_fail       : {crc_fail[-1]}   '
          f'-- {crc_fail[-1] - lenient} more than even the lenient STALE(checksum) count, '
          f'NOT explained here')
    # Spans and gaps are accumulated WITHIN each boot and then pooled, so no
    # difference ever crosses a tick reset. `span_ms` is the sum of the boots'
    # own first-to-last spans, which is the quantity the mean inter-arrival
    # needs; the count it divides by drops one event per contributing boot,
    # because a boot's first failure has no predecessor inside that boot.
    gaps_between = []
    span_ms = 0.0
    intervals = 0
    for g in csum_by_boot:
        if len(g) >= 2:
            span_ms += float(g[-1] - g[0])
            intervals += len(g) - 1
            gaps_between.extend(g[i] - g[i - 1] for i in range(1, len(g)))
    if intervals >= 1:
        # BOTH SIDES OF THIS RATIO ARE ON THE PANEL CLOCK. An earlier version took
        # the span from `up=`, which is esp_timer, while the events are FreeRTOS
        # ticks - a 0.22% cross-clock error in a file whose docstring promises
        # not to make one. The span is the events' own first-to-last, per boot.
        mean_gap = span_ms / intervals
        print(f'span (panel clock, summed over boots) : {span_ms / 3_600_000:.2f} h '
              f'over {intervals} interval(s)')
        print(f'mean inter-arrival     : {mean_gap / 1000:.0f}s')
        print(f'observed median gap    : {statistics.median(gaps_between) / 1000:.0f}s')
        # ONE MEASUREMENT PER FAILURE, from the heal that immediately preceded it
        # IN THE SAME BOOT. An earlier version measured forward from every LIVE,
        # which (a) let one failure be counted by several heals and (b) counted
        # LIVEs that ended resync and disconnect greys as though they were heals.
        # The 1:1 mapping is what the criterion actually names — and the heal
        # before a reset is not the heal before a failure after it, which is why
        # the search is inside the boot rather than over the pooled list.
        deltas = []
        for cs, lv in zip(csum_by_boot, lives_by_boot):
            for c in cs:
                prev = [t for t in lv if t < c]
                if prev:
                    deltas.append(c - prev[-1])
        WINDOW = 10_000
        within = sum(1 for x in deltas if x <= WINDOW)
        p_obs = within / len(deltas) if deltas else 0.0
        p_base = 1.0 - math.exp(-WINDOW / mean_gap)
        print()
        print(f'THE CRITERION: P(a checksum failure lands within {WINDOW // 1000}s '
              f'of the preceding heal)')
        print(f'  observed             : {within}/{len(deltas)} = {100 * p_obs:.2f}%')
        print(f'  Poisson baseline     : {100 * p_base:.2f}%  '
              f'(1 - exp(-{WINDOW // 1000}/{mean_gap / 1000:.0f}); memorylessness makes '
              f'the reference point immaterial under the null)')
        ratio = (p_obs / p_base) if p_base else float('inf')
        print(f'  ratio                : {ratio:.2f}x')
        print(f'  VERDICT              : ' + (
            'MATERIALLY ABOVE BASELINE - the heal/drop loop is real'
            if ratio >= 2.0 else
            'not materially above baseline - no self-sustaining loop visible'))
        # inter-arrival histogram
        print()
        print('inter-arrival histogram (s):')
        buckets = [(0, 10), (10, 30), (30, 60), (60, 120), (120, 300),
                   (300, 600), (600, 1800), (1800, 10 ** 9)]
        for lo, hi in buckets:
            n = sum(1 for g in gaps_between if lo * 1000 <= g < hi * 1000)
            bar = '#' * min(60, n)
            label = f'{lo}-{hi}' if hi < 10 ** 9 else f'{lo}+'
            print(f'  {label:>10}s  {n:>4}  {bar}')

    # --- pipe drops ---------------------------------------------------------
    #
    # `finditer` + named groups rather than `findall` + indices: this section
    # printed max_held under the label `pipe qfull` for as long as it took anyone
    # to notice, which was M5 stage D-A3, because both are small plausible
    # integers. See RE_PIPE.
    pipe_m = list(RE_PIPE.finditer(text))
    pipe = [m.groupdict() for m in pipe_m]
    owned('-- pipe', len(pipe))
    # ITS OWN HEADING, WHICH IT DID NOT HAVE. Every figure below printed under
    # "(a) ARE THE CRC FAILURES CLUSTERED?" because the block was appended after
    # that section and never opened one of its own — so a reader scanning
    # headings found the frame-pipe reading filed under checksum clustering.
    section('(a2) THE FRAME PIPE - occupancy first, drops second')
    if pipe:
        # PER BOOT, because `FramePipe`'s counters are per-boot like every other
        # `panel:` counter: a run-wide `oversize` read off the last line reports
        # the last boot's, and D-C's single oversize event was in B1 of seven.
        per = by_boot([(m.start(), m.groupdict()) for m in pipe_m], starts, n_boots)
        per_boot_table(
            per,
            '  boot   published   oversize  no_slot  max_held  qfull',
            lambda p, _g: (f'{int(p["published"]):>9}  {int(p["oversize"]):>8}  '
                           f'{int(p["no_slot"]):>7}  {int(p["max_held"]):>4} of '
                           f'{int(p["slots"])}  {int(p["qfull"]):>5}'),
            lambda pp: (f'{sum(int(g[-1]["published"]) for g in pp if g):>9}  '
                        f'{sum(int(g[-1]["oversize"]) for g in pp if g):>8}  '
                        f'{sum(int(g[-1]["no_slot"]) for g in pp if g):>7}      '
                        f'(summed over boots, which is the run)'))
    if pipe:
        ns = [int(p['no_slot']) for p in pipe]
        held = [int(p['max_held']) for p in pipe]
        slots = int(pipe[-1]['slots'])
        print()
        print(f'pipe no_slot           : start={ns[0]} end={ns[-1]}  '
              f'(drops attributable to snapshot bursts)')
        print(f'pipe qfull             : {int(pipe[-1]["qfull"])}   '
              f'oversize={int(pipe[-1]["oversize"])}')
        # MAX_HELD FIRST, because it is the leading indicator and `no_slot` is
        # the trailing one: occupancy is the mechanism and frame time is one
        # contributor to it (NOTES-binance.md, D-A2 addendum section 9, which
        # supersedes ROADMAP's "watch slow(>25ms)"). Reaching kFrameSlots is the
        # reading that matters, and it fires before anything is lost.
        print(f'pipe max_held          : peak {max(held)} of {slots}'
              f'{"   *** REACHED kFrameSlots ***" if max(held) >= slots else ""}')
        if ns[-1] and len(csum):
            print(f'  no_slot per STALE(checksum): {ns[-1] / len(csum):.2f}')
            # Guarded: `crc_fail` is 0 at any venue that publishes no checksum,
            # and this line divided by it unconditionally.
            if crc_fail[-1]:
                print(f'  no_slot per counter crc_fail: {ns[-1] / crc_fail[-1]:.2f}')

    # --- (b) largest free block --------------------------------------------
    section('(b) LARGEST FREE BLOCK - full series, not endpoints')
    print(f'free heap    : start {heap[0]} end {heap[-1]} min {min(heap)} max {max(heap)}')
    print(f'largest block: start {largest[0]} end {largest[-1]} '
          f'min {min(largest)} max {max(largest)}')
    # PER BOOT, AND BOTH HALVES BELOW WERE WRONG ACROSS BOOTS FOR THE SAME
    # REASON: `up` restarts at 10 s at every reset. The plateau walk read each
    # restart as a new plateau and emitted 4,209 lines on a seven-boot capture,
    # and the 3-hour bins were driven by `up[-1]` — the LAST BOOT's uptime — so a
    # 34.55 h run was binned into three buckets with seven boots' first three
    # hours mashed together in the first. That is the read-the-last-line defect
    # this change was written to remove, in a section it did not touch.
    #
    # THE PLATEAU WALK IS A CAP RATHER THAN A SEGMENTATION, and the cap is the
    # honest answer: the reading it exists for is *"distinct plateaus, in order"*
    # on a heap that moves in steps, and a capture where the value changes
    # thousands of times has no plateaus to report — so say that instead of
    # printing the churn.
    PLATEAU_CAP = 40
    for n in with_soak:
        rows = boot_rows[n]
        runs = []
        for i in rows:
            if not runs or runs[-1][0] != largest[i]:
                runs.append([largest[i], up[i], up[i]])
            else:
                runs[-1][2] = up[i]
        print()
        print(f'B{n + 1} distinct largest-block plateaus (value: first..last uptime):')
        if len(runs) > PLATEAU_CAP:
            print(f'  {len(runs)} distinct values in {len(rows)} samples — this is churn, '
                  f'not plateaus.')
            print(f'  min {min(largest[i] for i in rows)} B, '
                  f'max {max(largest[i] for i in rows)} B, '
                  f'median {statistics.median(largest[i] for i in rows):.0f} B')
        else:
            for v, a, b in runs:
                print(f'  {v:>7} B   up {a:>7}s .. {b:>7}s   ({(b - a) / 3600.0:.2f} h)')

    print()
    print('3-hour medians, per boot (the reading that retracted the heap alarm):')
    for n in with_soak:
        rows = boot_rows[n]
        for q in range(0, int(up[rows[-1]] / 3600) + 3, 3):
            idx = [i for i in rows if q * 3600 <= up[i] < (q + 3) * 3600]
            if idx:
                print(f'  B{n + 1} {q:>2}-{q + 3:>2} h  '
                      f'free med={statistics.median(heap[i] for i in idx):>7.0f}  '
                      f'largest med={statistics.median(largest[i] for i in idx):>7.0f}  '
                      f'n={len(idx)}')

    # --- (c) DNS ------------------------------------------------------------
    # --- the liveness signal, and D-C's first named check ------------------
    section("(b2) THE LIVENESS SIGNAL - D-C's multiplier falsifier")
    sig_m = list(RE_SIGNAL.finditer(text))
    sig = [m.groupdict() for m in sig_m]
    age = [m.groupdict() for m in RE_AGE.finditer(text)]
    owned('-- signal', len(sig))
    owned('-- age', len(age))
    if sig and n_boots > 1:
        # PER BOOT. `n` is the histogram's sample count and it restarts at every
        # reset, so a run-wide "intervals sampled" read off the last line is one
        # boot's. D-C summed seven boots' final histograms by hand to get 6,183.
        per = by_boot([(m.start(), m.groupdict()) for m in sig_m], starts, n_boots)
        print()
        per_boot_table(
            per,
            '  boot   intervals   median   worst   threshold   >=2x med   state',
            lambda s, g: (f'{int(s["n"]):>9}   {int(s["median_ms"]):>6}   '
                          f'{max(int(r["max_ms"]) for r in g):>5}   '
                          f'{int(s["threshold_ms"]):>9}   '
                          f'{max(int(r["over"]) for r in g):>8}   {s["state"]}'),
            lambda pp: (f'{sum(int(g[-1]["n"]) for g in pp if g):>9}   '
                        "(summed final histograms - the run's healthy-interval "
                        'population)'))
        print()
    if not sig:
        print('no `-- signal` lines: this capture predates M5 stage D-A3, or the')
        print('liveness signal is not wired on the build that produced it.')
    else:
        last = sig[-1]
        over = [int(r['over']) for r in sig]
        print(f'signal            : {last["name"].strip()}')
        print(f'intervals sampled : {fmt(int(last["n"]))}   (healthy only - an')
        print('                    outage-spanning gap is not a cadence sample)')
        print(f'median            : {int(last["median_ms"])} ms')
        print(f'worst interval    : {int(last["max_ms"])} ms')
        print(f'threshold         : {int(last["threshold_ms"])} ms  [{last["state"]}]')
        print()
        print(f'FALSIFIER (>=2x median) : {max(over)}')
        if max(over) == 0:
            print('  k stands. No interval on a healthy socket reached twice the median.')
        else:
            print('  *** THE FALSIFIER FIRED. Stage C\'s rule: k RISES, and it rises ALONE --')
            print('  *** the 60,000 ms ceiling already admits k <= 3.005 (3.0 -> 59,891.91 ms).')
    if age:
        uncal = sum(1 for r in age if int(r['samples']) < 8)
        print()
        print(f'`-- age` lines    : {len(age)}   of which uncalibrated (<8 samples): {uncal}')

    # --- the re-seed ledger, and D-C's check 7 ------------------------------
    #
    # WHAT MAKES THIS A READING AND NOT A PASS/FAIL, stated here because it is
    # the whole reason the line has four counters instead of one. The ONLY route
    # to a re-seed on a LIVE book is `note_depth`'s coverage trigger, so
    # `adopted=0` has four completely different causes and the ledger's other
    # fields are what separate them (`render_task.cpp` carries the same table
    # beside the format string it prints):
    #
    #   below=N>0                 the seed arrived already under its own margin
    #                             and never armed the trigger. Re-fetching at the
    #                             same depth would change nothing.
    #   triggers=0, cover >> N    armed and never approached. The book did not
    #                             live long enough, or the market did not walk.
    #                             NOT a verdict on the mechanism.
    #   triggers=0, cover ~ N     it came close. A longer run would do it.
    #   triggers=M, adopted=0     the mechanism RAN and did not adopt. THAT is a
    #                             verdict, and `unbracketed` says which kind.
    #
    # PER BOOT, AND THAT HALF IS NOT A REFINEMENT EITHER. `BinanceAdapter::Stats`
    # is a plain member and nothing anywhere resets it, so the last line of the
    # file is the last BOOT's ledger: on D-C's seven-boot run, "the line at the
    # end of the run" would have discarded six of them, including any boot in
    # which the trigger fired.
    reseed_m = list(RE_RESEED.finditer(text))
    owned('-- reseed', len(reseed_m))
    section("(b3) THE RE-SEED LEDGER - D-C's check 7")
    if not reseed_m:
        print('no `-- reseed` lines: this capture predates M5 stage D-A4, or it is')
        print('not a Binance build (the line is inside `#if DC_VENUE == BINANCE`).')
    else:
        per = by_boot([(m.start(), m.groupdict()) for m in reseed_m], starts, n_boots)
        tot = {k: 0 for k in ('adopted', 'unbracketed', 'hold_overflow',
                              'declined', 'adoptable', 'triggers', 'below')}
        targets = set()
        closest = None
        for g in per:
            if not g:
                continue
            r = g[-1]
            for k in tot:
                tot[k] += int(r[k])
            targets.add(int(r['cover_target']))
            # The low-water marks are already run-of-boot minima on the board, so
            # the boot's LAST line carries them; `-` means the seed bounds were
            # never established in that boot and there is nothing to compare.
            #
            # `isdigit` and not a bare `int()`: `[-\d]+` accepts `46-0`, which
            # the firmware cannot emit but a UART with two tasks writing it can
            # — this file already records nine mangled `*** STALE` lines in one
            # capture and made THAT reader lenient for the same reason. A report
            # that dies on a mangled character produces no census and no output
            # at all, which is worse than the figure it was protecting.
            for side in (r['cover_bid'], r['cover_ask']):
                if side.isdigit() and (closest is None or int(side) < closest):
                    closest = int(side)
        per_boot_table(
            per,
            '  boot  adopted unbrack hold-ovf declined adoptable triggers below   cover',
            lambda r, _g: (f'{int(r["adopted"]):>7} {int(r["unbracketed"]):>7} '
                           f'{int(r["hold_overflow"]):>8} {int(r["declined"]):>8} '
                           f'{int(r["adoptable"]):>9} {int(r["triggers"]):>8} '
                           f'{int(r["below"]):>5}   '
                           f'{r["cover_bid"] + "/" + r["cover_ask"]:>9} of '
                           f'{r["cover_target"]}'),
            lambda _pp: (f'{tot["adopted"]:>7} {tot["unbracketed"]:>7} '
                         f'{tot["hold_overflow"]:>8} {tot["declined"]:>8} '
                         f'{tot["adoptable"]:>9} {tot["triggers"]:>8} '
                         f'{tot["below"]:>5}'))
        if len(targets) > 1:
            print(f'  *** the cover target is not constant across boots: {sorted(targets)}.')
            print('  *** kBinanceReseedCoverLevels changed mid-capture, so the rows are')
            print('  *** not comparable and the reading below uses the smallest.')
        target = min(targets) if targets else None
        print()
        print('  READING (check 7 is a reading, not a pass/fail):')
        # THE LADDER IS THE FIRMWARE'S, IN THE FIRMWARE'S ORDER. `render_task.cpp`
        # puts `below=N>0` FIRST — the trigger was never armed — and the first
        # draft here tested `triggers>0` first, so a run with `below>0` in one
        # boot and `triggers>0` in another printed the triggers verdict and never
        # mentioned `below` at all. Two tables of the same four cases in two
        # orders is the "one rule, two copies" this project keeps deleting; the
        # order is now the same and the case list is checked against it.
        if tot['adopted'] > 0:
            print(f'  *** {tot["adopted"]} RE-SEED(S) ADOPTED ON A LIVE BOOK. The mechanism ran')
            print('  *** and reconciled. Cross-check: grey episodes over the same boots.')
        elif tot['below'] > 0 and tot['triggers'] == 0:
            print(f'  below={tot["below"]}: the seed arrived under its own margin and the')
            print('  trigger was never armed. Not a verdict on the mechanism.')
        elif tot['triggers'] > 0 and tot['adoptable'] == 0:
            # THE FIFTH CASE, WHICH THE FIRMWARE'S FOUR DO NOT SEPARATE.
            # `cover_triggers` counts the LATCH, not an arrived body: triggers
            # with nothing adoptable means the fetch never reached the adopt
            # decision, which is a verdict on the transport and not on the
            # adapter. Calling it "the mechanism ran and did not adopt" would
            # blame the wrong half.
            print(f'  triggers={tot["triggers"]} but adoptable=0: the trigger latched and no')
            print(f'  body ever reached the adopt decision (declined={tot["declined"]}).')
            print('  That is a verdict on the FETCH, not on the re-seed mechanism.')
        elif tot['triggers'] > 0:
            print(f'  *** THE MECHANISM RAN AND ADOPTED NOTHING: triggers={tot["triggers"]},')
            print(f'  *** adopted=0, adoptable={tot["adoptable"]}, '
                  f'unbracketed={tot["unbracketed"]}. That IS a verdict.')
        elif tot['below'] > 0:
            print(f'  below={tot["below"]} AND triggers={tot["triggers"]} in different boots —')
            print('  read the table per boot; the run-wide totals mix two states.')
        elif closest is not None and target:
            near = closest <= target * 11 // 10
            print(f'  triggers=0. Closest the book came to the trigger: {closest} of {target}'
                  f'{"  -- CLOSE, a longer run would reach it" if near else ""}')
            if not near:
                print('  The trigger armed and was never approached: the book did not live')
                print('  long enough, or the market did not walk. NOT a verdict on the')
                print('  mechanism, which was never asked to run.')
        else:
            print('  triggers=0 and no cover reading (`-` on both sides every boot):')
            print('  the seed bounds were never established. Nothing is being measured.')

    section('(c) DNS')
    fails_dns = owned('warm_dns FAILED',
                      [(int(a), int(b), int(c)) for a, b, c in RE_DNS_FAIL.findall(text)])
    ups = owned('socket up:',
                [(int(a), int(b), int(c), int(d), int(e))
                 for a, b, c, d, e in RE_SOCK_UP.findall(text)])
    print(f'resolution FAILURES : {len(fails_dns)}')
    if fails_dns:
        d = [f[2] for f in fails_dns]
        print(f'  durations         : min={min(d)} max={max(d)} '
              f'median={statistics.median(d):.0f} ms   rc={sorted({f[1] for f in fails_dns})}')
    print(f'successful connects : {len(ups)} `socket up:` lines against counter '
          f'connects={connects[-1]} -- a connect predating the capture attach prints none')
    if ups:
        print('  dns_ms  conn+upg_ms  rssi')
        for _, dns_ms, cu, _fd, rssi in ups:
            print(f'  {dns_ms:>6}  {cu:>11}  {rssi:>5}')
        succ = [u[1] for u in ups]
        print(f'  successes median dns: {statistics.median(succ):.0f} ms')
        slow = sum(1 for x in succ if x >= 13_000)
        print(f'  successes >= 13,000 ms: {slow}/{len(succ)}')
        print('  HYPOTHESIS (primary resolver dead, stack falls through to secondary): ' + (
            'CONSISTENT - every success also paid the full timeout'
            if slow == len(succ) and len(succ) else
            'NOT supported - at least one success resolved promptly'))
    assoc = owned('autopsy assoc=', RE_AUTOPSY.findall(text))
    print()
    print(f'autopsy assoc= values: {sorted(set(assoc)) if assoc else "none"}  '
          f'(n={len(assoc)})')
    print(f'wifi down / rejoin / wifi-up events: {len(RE_WIFI.findall(text))}')
    # `wifi up:` IS NOT AN EVENT, and counting it as one made every short capture
    # report a false alarm. EVERY capture that includes a boot has exactly one
    # `wifi up:` line -- it is the association succeeding, not dropping. Only the
    # two failure shapes count.
    drops = owned('wifi down|rejoining', RE_WIFI_DROP.findall(text))
    print(f'wifi DROP / rejoin events: {len(drops)}   '
          f'(`wifi up:` lines are not drops and are not counted)')
    print('  ASSOCIATION: ' + (
        'never dropped - every autopsy reports assoc=1 and no drop or rejoin was seen'
        if set(assoc) <= {'1'} and not drops else
        'CHECK - association drop or rejoin present'))

    census()
    return 0




# =============================================================================
# SELFCHECK - because this tool went silently blind for two stages
# =============================================================================
#
# WHAT IT IS FOR, STATED PRECISELY. Every grammar in this file is a copy of a
# format string in `firmware/src/render_task.cpp` or `ws_transport.cpp`, and
# nothing has ever held the two together. They drifted three ways at once and
# nobody saw it, because the failure mode is not a wrong number -- it is a
# MISSING SECTION, which reads exactly like a run that had nothing to report:
#
#   1. `monitor_filters` gained `time`, so every line grew an `HH:MM:SS.mmm > `
#      prefix and every `^`-anchored grammar matched nothing at all.
#   2. `max_held=%u of %u` was inserted into the `-- pipe` line between
#      `no_slot` and `qfull`, so the positional read printed max_held under the
#      label `qfull` -- plausibly, and wrongly.
#   3. `^\[(\d+)\]` could not match `[  9316]`, because the Arduino core pads
#      millis to width 8. That one worked only on captures past ~2.8 h of
#      uptime, which is why it looked fine on the 25 h soak.
#
# So the selfcheck feeds a SYNTHETIC capture -- one line of every shape this
# tool claims to read, prefix and all -- through the real grammars and asserts
# that each matches. It cannot prove the firmware still prints these shapes; it
# proves that what this file believes it reads, it does read. Pair it with the
# census printed at the end of every real run, which says what a REAL capture
# matched.

SELFCHECK_CAPTURE = '\n'.join([
    '05:45:51.643 > ESP-ROM:esp32s3-20210327',
    '05:45:51.643 > [   523][I][main.cpp:114] setup(): [main] DepthCharge',
    '05:45:57.861 > I (9871) panel: SOAK venue=binance up=10s live=1 age=- '
    'worst_age=0.0s baseline=0ms grey_n=1 grey_ms=9483 wd=0 sock=0 connects=1 '
    'rows=54/54 unknown=0 crc_rows=0 (0.0%) resync_req=0 heals=0 owed=0 '
    'refused=0 crc_fail=0 heap=23500 largest=13300 frames=0 drawn=1',
    '05:46:01.000 > W (10000) panel: *** STALE (seq-gap) at v3 - panel greys here',
    '05:46:02.000 > I (11000) panel: *** LIVE at v4',
    '05:46:03.000 > I (12000) panel:   grey for 1200 ms',
    '05:46:04.000 > I (13000) panel: -- pipe   : published=1008 oversize=0 '
    'no_slot=3 max_held=4 of 4 qfull=0 abandoned=0 cont=0 ctrl=13',
    '05:46:05.000 > I (14000) panel: -- age    : 1.2s (worst 3.4s) | baseline 20 ms '
    '| server-ping median 19964 ms, grey at 39928 ms after 12 sample(s) '
    '| back-stamps 0 | seq 41',
    '05:46:06.000 > I (15000) panel: -- signal : server-ping n=12 max=20011 ms '
    '>=2x med=0 | median 19964 ms threshold 39928 ms CALIBRATED',
    '05:46:06.500 > I (15500) panel: -- reseed : adopted=0 unbracketed=0 '
    'hold-overflow=0 | declined(no-hold)=0 adoptable=0 | triggers=0 below=0 '
    'cover=460/473 of 448',
    '05:46:07.000 > [  9316][I][ws_transport.cpp:637] socket up: dns 26 ms, '
    'connect+upgrade 1904 ms, fd 48, rssi -41 dBm | largest internal '
    'before=51188 after=17396 (a session needs 2 x 16717 B)',
    '05:46:08.000 > [  9400][W][ws_transport.cpp:1] warm_dns() for host '
    'did not resolve (rc 202) after 14000 ms',
    '05:46:09.000 > [  9500][W][autopsy] assoc=1',
    '',
])

# A SECOND BOOT, APPENDED TO THE FIRST, and the `cover=-/-` form in it is
# deliberate: `have_seed_bounds_` is false until a seed lands, and a reader that
# calls int() on the sentinel dies on the first line of every fresh boot.
SELFCHECK_SECOND_BOOT = '\n'.join([
    '06:10:00.000 > ESP-ROM:esp32s3-20210327',
    '06:10:01.000 > I (9871) panel: SOAK venue=binance up=10s live=0 age=- '
    'worst_age=0.0s baseline=0ms grey_n=1 grey_ms=1000 wd=0 sock=0 connects=1 '
    'rows=54/54 unknown=0 crc_rows=0 (0.0%) resync_req=0 heals=0 owed=0 '
    'refused=0 crc_fail=0 heap=23500 largest=13300 frames=0 drawn=1',
    '06:10:02.000 > I (10000) panel: -- pipe   : published=7 oversize=1 '
    'no_slot=0 max_held=2 of 4 qfull=0 abandoned=0 cont=0 ctrl=1',
    '06:10:03.000 > I (11000) panel: -- reseed : adopted=2 unbracketed=1 '
    'hold-overflow=0 | declined(no-hold)=3 adoptable=4 | triggers=3 below=0 '
    'cover=-/- of 448',
    '',
])


def selfcheck():
    cases = [
        ('RE_TIME_PREFIX', RE_TIME_PREFIX, 12),
        ('RE_SOAK', RE_SOAK, 1),
        ('RE_STALE', RE_STALE, 1),
        ('RE_LIVE', RE_LIVE, 1),
        ('RE_GREYFOR', RE_GREYFOR, 1),
        ('RE_PIPE', RE_PIPE, 1),
        ('RE_AGE', RE_AGE, 1),
        ('RE_SIGNAL', RE_SIGNAL, 1),
        ('RE_SOCK_UP', RE_SOCK_UP, 1),
        ('RE_DNS_FAIL', RE_DNS_FAIL, 1),
        ('RE_AUTOPSY', RE_AUTOPSY, 1),
        ('RE_RESEED', RE_RESEED, 1),
    ]

    # The prefix is counted on the raw text; everything else on the stripped
    # text, which is exactly the order `main()` does it in.
    raw = SELFCHECK_CAPTURE
    stripped, prefixed = RE_TIME_PREFIX.subn('', raw)

    fails = 0
    checks = 0
    for name, rx, want in cases:
        got = prefixed if name == 'RE_TIME_PREFIX' else len(rx.findall(stripped))
        checks += 1
        if got < want:
            print(f'[soak_report] FAIL {name}: expected >= {want} match(es), got {got}')
            fails += 1

    # THE INDEX TRAP, PINNED BY NAME. `qfull` moved from group 4 to group 6 when
    # `max_held` was inserted, and the old code read [4] and labelled it qfull.
    # Named groups make that impossible; this asserts the values are the ones the
    # synthetic line actually carries, so a future insertion cannot re-label them.
    m = RE_PIPE.search(stripped)
    checks += 1
    if not m or m.group('qfull') != '0' or m.group('max_held') != '4' \
            or m.group('slots') != '4' or m.group('no_slot') != '3':
        print('[soak_report] FAIL RE_PIPE: named groups do not carry the '
              'expected values (qfull/max_held/slots/no_slot)')
        fails += 1

    # And the falsifier's own field, which is the one number D-C reads first.
    m = RE_SIGNAL.search(stripped)
    checks += 1
    if not m or m.group('over') != '0' or m.group('median_ms') != '19964':
        print('[soak_report] FAIL RE_SIGNAL: the falsifier count or the median '
              'is not where the grammar expects it')
        fails += 1

    # THE RE-SEED LEDGER'S OWN FIELDS, and `cover=` above all, because that is
    # the field that stops `adopted=0` meaning nothing and it is the one whose
    # position a future counter insertion would move.
    m = RE_RESEED.search(stripped)
    checks += 1
    if not m or m.group('adopted') != '0' or m.group('triggers') != '0' \
            or m.group('cover_bid') != '460' or m.group('cover_ask') != '473' \
            or m.group('cover_target') != '448':
        print('[soak_report] FAIL RE_RESEED: the ledger fields are not where the '
              'grammar expects them (adopted/triggers/cover)')
        fails += 1

    # THE SENTINEL FORM. `cover=-/-` is what every boot prints until a seed
    # lands, so a grammar that requires digits there matches nothing at the start
    # of every capture -- and a reader that calls int() on it dies outright.
    two_boot = raw + SELFCHECK_SECOND_BOOT
    two_stripped = RE_TIME_PREFIX.sub('', two_boot)
    checks += 1
    ledgers = list(RE_RESEED.finditer(two_stripped))
    if len(ledgers) != 2 or ledgers[1].group('cover_bid') != '-' \
            or ledgers[1].group('cover_ask') != '-':
        print('[soak_report] FAIL RE_RESEED: the `cover=-/-` sentinel form does '
              'not parse, or the second boot\'s ledger was not seen')
        fails += 1

    # BOOT SEGMENTATION, PROVED RATHER THAN ASSERTED IN A COMMENT. Two boots,
    # the second with LOWER panel ticks, and every per-boot counter in the second
    # smaller than the first's -- which is exactly the shape that made "the last
    # line of the file" report one boot of seven as the run.
    checks += 1
    starts = boot_starts(two_stripped)
    if len(starts) != 2:
        print(f'[soak_report] FAIL boot_starts: expected 2 boots, got {len(starts)}')
        fails += 1
    else:
        got = [boot_of(m.start(), starts) for m in ledgers]
        if got != [0, 1]:
            print(f'[soak_report] FAIL boot_of: the two `-- reseed` lines landed in '
                  f'boots {got}, expected [0, 1]')
            fails += 1

    # AND A SINGLE-BOOT CAPTURE MUST STILL BE ONE BOOT: the segmentation must not
    # invent a boundary out of the ordinary rise of a tick counter.
    checks += 1
    if len(boot_starts(stripped)) != 1:
        print('[soak_report] FAIL boot_starts: a single-boot capture was split')
        fails += 1

    # THE OUT-OF-ORDER WRITE, WHICH IS THE CASE THE FIRST VERSION OF THIS GOT
    # WRONG — 27 boots reported on a 7-boot capture. Two tasks logging
    # concurrently put a `rest:` line in the file a few ms behind the `panel:`
    # line it overtook. That is a DECREASE and it is not a boot, and the only
    # thing distinguishing it from one is that the new value is nowhere near a
    # restart. Verbatim from `bench-2026-08-30-D-C-soak.log`, both shapes.
    interleaved = '\n'.join([
        'I (7163561) panel: SOAK note: binance publishes no checksum',
        'I (7163548) rest: fetch OK(none) HTTP 200 64046 B in 4129 ms',
        'I (7163602) panel: -- pipe   : published=1 oversize=0 no_slot=0 '
        'max_held=1 of 4 qfull=0',
        'W (7163590) rest: largest internal block 4596 B is BELOW the 16717 B',
        '',
    ])
    checks += 1
    if len(boot_starts(interleaved)) != 1:
        print('[soak_report] FAIL boot_starts: an out-of-order log write was read '
              'as a reboot (this is the 27-boots-from-7 defect)')
        fails += 1

    # ...and the real thing, which must still be seen: the tick RESTARTS.
    restart = interleaved + '\n'.join([
        'I (341) idle: per-core idle probe up at 240 MHz (continuity 100 us)',
        'I (9871) panel: -- pipe   : published=2 oversize=0 no_slot=0 '
        'max_held=1 of 4 qfull=0',
        '',
    ])
    checks += 1
    if len(boot_starts(restart)) != 2:
        print('[soak_report] FAIL boot_starts: a genuine panel-clock restart was '
              'not read as a reboot')
        fails += 1

    # AN OUT-OF-ORDER WRITE INSIDE THE FIRST MINUTE OF A BOOT, which is where the
    # `tick < ceiling` rule alone forges a boundary — and forges it silently,
    # because it escapes the branch that counts out-of-order lines. Every boot of
    # the D-C capture carries 168-225 lines in this range, so this is the shape
    # that would have fired next.
    early_race = '\n'.join([
        'I (341) idle: per-core idle probe up at 240 MHz (continuity 100 us)',
        'I (9804) panel: SOAK note: still inside the first minute',
        'I (9791) rest: fetch OK(none) HTTP 200 64046 B in 4129 ms',
        'I (9860) panel: -- pipe   : published=1 oversize=0 no_slot=0 '
        'max_held=1 of 4 qfull=0',
        '',
    ])
    checks += 1
    if len(boot_starts(early_race)) != 1:
        print('[soak_report] FAIL boot_starts: an out-of-order write inside the '
              'first minute of a boot was read as a reboot')
        fails += 1

    # AND THE CASE THAT RULES OUT THE OBVIOUS FIX. Requiring the drop to come
    # from a clock past the ceiling would merge this — a boot that reset 12 s in,
    # which is exactly when per-boot reading matters most.
    short_boot = '\n'.join([
        'I (341) idle: per-core idle probe up at 240 MHz (continuity 100 us)',
        'I (11980) panel: SOAK note: about to be reset',
        'I (338) idle: per-core idle probe up at 240 MHz (continuity 100 us)',
        'I (9871) panel: SOAK note: second boot',
        '',
    ])
    checks += 1
    if len(boot_starts(short_boot)) != 2:
        print('[soak_report] FAIL boot_starts: a boot that reset inside 60 s was '
              'merged into its predecessor')
        fails += 1

    # THE MANGLED `cover=` FIELD. `[-\\d]+` accepts `46-0`, which the firmware
    # cannot emit and a UART with two tasks writing it can — this file already
    # records nine mangled STALE lines in one capture. The reader must not die on
    # it: a report that raises produces no census and no output at all.
    mangled = ('I (13000) panel: -- reseed : adopted=0 unbracketed=0 '
               'hold-overflow=0 | declined(no-hold)=0 adoptable=0 | triggers=0 '
               'below=0 cover=46-0/473 of 448')
    checks += 1
    mm = RE_RESEED.search(mangled)
    if not mm:
        print('[soak_report] FAIL RE_RESEED: a mangled cover field stopped the '
              'line matching at all')
        fails += 1
    else:
        try:
            for side in (mm.group('cover_bid'), mm.group('cover_ask')):
                if side.isdigit():
                    int(side)
        except ValueError:
            print('[soak_report] FAIL: the cover reader still calls int() on a '
                  'field that is not all digits')
            fails += 1

    # A capture with NO prefix must still parse -- one tool, both eras.
    old_era = RE_TIME_PREFIX.sub('', raw)
    checks += 1
    if len(RE_SOAK.findall(old_era)) != 1:
        print('[soak_report] FAIL: an unprefixed capture no longer parses')
        fails += 1

    print(f"[soak_report] selfcheck {'FAILED' if fails else 'OK'}: {checks} checks")
    return 1 if fails else 0


if __name__ == '__main__':
    if len(sys.argv) == 2 and sys.argv[1] == '--selfcheck':
        raise SystemExit(selfcheck())
    if len(sys.argv) != 2:
        print(__doc__)
        raise SystemExit(2)
    raise SystemExit(main(sys.argv[1]))
