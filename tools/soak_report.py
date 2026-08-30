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


def main(path):
    blob = open(path, 'rb').read()
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
    print(f'sha256  : {hashlib.sha256(blob).hexdigest()}')
    print(f'bytes   : {fmt(len(blob))}')
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

    # --- the SOAK series ----------------------------------------------------
    soak = owned('SOAK', RE_SOAK.findall(text))
    if not soak:
        print('NO SOAK LINES PARSED - the grammar has drifted from the firmware.')
        return 1
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

    section('THE RUN')
    print(f'SOAK lines        : {fmt(len(soak))}')
    print(f'board uptime      : {up[0]}s -> {up[-1]}s  ({up[-1] / 3600:.2f} h)')
    reboots = [(up[i - 1], up[i]) for i in range(1, len(up)) if up[i] < up[i - 1]]
    print(f'REBOOTS           : {reboots if reboots else "NONE - uptime monotonic"}')
    print(f'live at end       : {live[-1]}')
    print(f'final counters    : grey_n={grey_n[-1]} wd={wd[-1]} sock={sock[-1]} '
          f'connects={connects[-1]}')
    print(f'                    resync_req={resync[-1]} heals={heals[-1]} '
          f'crc_fail={crc_fail[-1]} refused={refused[-1]} owed={owed[-1]}')
    print(f'worst_age (final) : {worst_age[-1]}')
    print(f'grey total        : {grey_ms[-1] / 1000:.0f}s = {grey_ms[-1] / 60000:.1f} min '
          f'= {100.0 * grey_ms[-1] / (up[-1] * 1000):.2f}% of uptime')

    # --- counter step ordering: wd vs sock ----------------------------------
    section('WATCHDOG VERSUS SOCKET - which noticed first')
    steps = []
    for i in range(1, len(soak)):
        if wd[i] != wd[i - 1]:
            steps.append((up[i], 'wd', wd[i - 1], wd[i]))
        if sock[i] != sock[i - 1]:
            steps.append((up[i], 'sock', sock[i - 1], sock[i]))
    for u, what, a, b in steps:
        print(f'  up={u:>7}s  {what:<4} {a}->{b}')
    # Pair each wd step with the next sock step, on the SAME clock.
    wds = [s for s in steps if s[1] == 'wd']
    socks = [s for s in steps if s[1] == 'sock']
    print()
    for w in wds:
        later = [s for s in socks if s[0] >= w[0]]
        if later:
            print(f'  wd at {w[0]}s -> next sock at {later[0][0]}s '
                  f'= watchdog was {later[0][0] - w[0]}s EARLIER '
                  f'(SOAK cadence is 10s, so +/-10s)')
    print(f'\n  watchdog firings {wd[-1]}, socket losses {sock[-1]} -- '
          f'{"NOT every socket loss was preceded by one" if sock[-1] > wd[-1] else "paired"}')

    # --- grey episodes ------------------------------------------------------
    section('GREY EPISODES')
    greys = [int(m[1]) for m in RE_GREYFOR.findall(text)]
    if greys:
        s = sorted(greys)
        LONG = 10_000
        quick = [x for x in s if x <= LONG]
        big = [x for x in s if x > LONG]
        print(f'episodes          : {len(s)}   total {sum(s) / 1000:.0f}s '
              f'({sum(s) / 60000:.1f} min)')
        print(f'median            : {statistics.median(s):.0f} ms')
        print(f'  <= {LONG} ms    : n={len(quick)}  median={statistics.median(quick):.0f} ms  '
              f'max={max(quick)} ms  total={sum(quick) / 1000:.0f}s')
        print(f'  >  {LONG} ms    : n={len(big)}  {[f"{x / 60000:.1f} min" for x in big]}  '
              f'total={sum(big) / 1000:.0f}s')
        if big:
            print(f'  the {len(big)} long ones are {100.0 * sum(big) / sum(s):.0f}% of all grey time')

    # --- (a) CRC clustering -------------------------------------------------
    section('(a) ARE THE CRC FAILURES CLUSTERED? - panel clock only')
    stale = [(int(t), r) for t, r, _ in RE_STALE.findall(text)]
    lives = [int(t) for t, _ in RE_LIVE.findall(text)]
    if not stale or not lives:
        print('NO STALE/LIVE LINES PARSED - grammar drift; refusing to report a zero.')
        return 1
    reasons = {}
    for _, r in stale:
        reasons[r] = reasons.get(r, 0) + 1
    csum = [t for t, r in stale if r == 'checksum']
    # The strict grammar needs the trailing ` at vNNN` to get a version; a
    # lenient count catches events whose line was mangled by two tasks writing
    # the UART at once (9 such lines in the 2026-08-22 capture, 0.003%).
    lenient = len(re.findall(r'\*\*\* STALE \(checksum\)', text))
    print(f'STALE events by reason : {reasons}  (total {len(stale)})')
    print(f'STALE(checksum) lenient: {lenient}   '
          f'({lenient - len(csum)} line(s) too mangled for the strict grammar)')
    print(f'LIVE events            : {len(lives)}')
    print(f'counter crc_fail       : {crc_fail[-1]}   '
          f'-- {crc_fail[-1] - lenient} more than even the lenient STALE(checksum) count, '
          f'NOT explained here')
    if len(csum) >= 2:
        # BOTH SIDES OF THIS RATIO ARE ON THE PANEL CLOCK. An earlier version took
        # the span from `up=`, which is esp_timer, while the events are FreeRTOS
        # ticks - a 0.22% cross-clock error in a file whose docstring promises
        # not to make one. The span is now the events' own first-to-last.
        span_ms = float(csum[-1] - csum[0])
        gaps_between = [csum[i] - csum[i - 1] for i in range(1, len(csum))]
        mean_gap = span_ms / (len(csum) - 1)
        print(f'run span (panel clock) : {span_ms / 3_600_000:.2f} h')
        print(f'mean inter-arrival     : {mean_gap / 1000:.0f}s')
        print(f'observed median gap    : {statistics.median(gaps_between) / 1000:.0f}s')
        # ONE MEASUREMENT PER FAILURE, from the heal that immediately preceded it.
        # An earlier version measured forward from every LIVE, which (a) let one
        # failure be counted by several heals and (b) counted LIVEs that ended
        # resync and disconnect greys as though they were heals. The 1:1 mapping
        # is what the criterion actually names.
        deltas = []
        for c in csum:
            prev = [lv for lv in lives if lv < c]
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
    pipe = [m.groupdict() for m in RE_PIPE.finditer(text)]
    owned('-- pipe', len(pipe))
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
    print()
    print('distinct largest-block plateaus, in order (value: first..last uptime):')
    runs = []
    for i, v in enumerate(largest):
        if not runs or runs[-1][0] != v:
            runs.append([v, up[i], up[i]])
        else:
            runs[-1][2] = up[i]
    for v, a, b in runs:
        print(f'  {v:>7} B   up {a:>7}s .. {b:>7}s   ({(b - a) / 3600.0:.2f} h)')
    print()
    print('3-hour medians (the reading that retracted the heap alarm):')
    for q in range(0, int(up[-1] / 3600) + 3, 3):
        idx = [i for i, u in enumerate(up) if q * 3600 <= u < (q + 3) * 3600]
        if idx:
            print(f'  {q:>2}-{q + 3:>2} h  free med={statistics.median(heap[i] for i in idx):>7.0f}  '
                  f'largest med={statistics.median(largest[i] for i in idx):>7.0f}  n={len(idx)}')

    # --- (c) DNS ------------------------------------------------------------
    # --- the liveness signal, and D-C's first named check ------------------
    section("(b2) THE LIVENESS SIGNAL - D-C's multiplier falsifier")
    sig = [m.groupdict() for m in RE_SIGNAL.finditer(text)]
    age = [m.groupdict() for m in RE_AGE.finditer(text)]
    owned('-- signal', len(sig))
    owned('-- age', len(age))
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

    section('(c) DNS')
    fails_dns = [(int(a), int(b), int(c)) for a, b, c in RE_DNS_FAIL.findall(text)]
    ups = [(int(a), int(b), int(c), int(d), int(e)) for a, b, c, d, e in RE_SOCK_UP.findall(text)]
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
    drops = RE_WIFI_DROP.findall(text)
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
    '05:46:07.000 > [  9316][I][ws_transport.cpp:637] socket up: dns 26 ms, '
    'connect+upgrade 1904 ms, fd 48, rssi -41 dBm | largest internal '
    'before=51188 after=17396 (a session needs 2 x 16717 B)',
    '05:46:08.000 > [  9400][W][ws_transport.cpp:1] warm_dns() for host '
    'did not resolve (rc 202) after 14000 ms',
    '05:46:09.000 > [  9500][W][autopsy] assoc=1',
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
