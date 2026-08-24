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
RE_STALE = re.compile(r'^W \((\d+)\) panel: \*\*\* STALE \((\w+)\) at v(\d+)', re.M)
RE_LIVE = re.compile(r'^I \((\d+)\) panel: \*\*\* LIVE at v(\d+)', re.M)
RE_GREYFOR = re.compile(r'^I \((\d+)\) panel:\s+grey for (\d+) ms', re.M)
RE_PIPE = re.compile(r'^I \((\d+)\) panel: -- pipe\s+: published=(\d+) oversize=(\d+) '
                     r'no_slot=(\d+) qfull=(\d+)', re.M)
RE_DNS_FAIL = re.compile(r'^\[(\d+)\].*warm_dns\(\).*did not resolve \(rc (\d+)\) after (\d+) ms', re.M)
RE_SOCK_UP = re.compile(r'^\[(\d+)\].*socket up: dns (\d+) ms, connect\+upgrade (\d+) ms, '
                        r'fd (\d+), rssi (-?\d+) dBm', re.M)
RE_AUTOPSY = re.compile(r'assoc=(\d+)')
RE_WIFI = re.compile(r'wifi down|rejoining|wifi up:')

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

    section('PROVENANCE')
    print(f'file    : {path}')
    print(f'sha256  : {hashlib.sha256(blob).hexdigest()}')
    print(f'bytes   : {fmt(len(blob))}')
    print(f'lines   : {fmt(blob.count(NEWLINE))}')
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
    soak = RE_SOAK.findall(text)
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
    pipe = RE_PIPE.findall(text)
    if pipe:
        ns = [int(p[3]) for p in pipe]
        print()
        print(f'pipe no_slot           : start={ns[0]} end={ns[-1]}  '
              f'(drops attributable to snapshot bursts)')
        print(f'pipe qfull             : {int(pipe[-1][4])}   oversize={int(pipe[-1][2])}')
        if ns[-1] and len(csum):
            print(f'  no_slot per STALE(checksum): {ns[-1] / len(csum):.2f}  '
                  f'(per counter crc_fail: {ns[-1] / crc_fail[-1]:.2f})')

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
    assoc = RE_AUTOPSY.findall(text)
    print()
    print(f'autopsy assoc= values: {sorted(set(assoc)) if assoc else "none"}  '
          f'(n={len(assoc)})')
    print(f'wifi down / rejoin / wifi-up events: {len(RE_WIFI.findall(text))}')
    print('  ASSOCIATION: ' + (
        'never dropped - every autopsy reports assoc=1 and there are no wifi events'
        if set(assoc) <= {'1'} and not RE_WIFI.findall(text) else
        'CHECK - association events present'))
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        raise SystemExit(2)
    raise SystemExit(main(sys.argv[1]))
