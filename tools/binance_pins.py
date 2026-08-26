"""Pinned figures for the committed Binance slices (M5 stage 0).

Regenerate with:  python tools/binance_frame_economics.py <slices> --pin

ADD ROWS ONLY. A figure here moving means either a tooling regression or a
deliberate re-capture -- and in the second case this table and the figures in
harness/replay/NOTES-binance.md move in the SAME commit. Never the third thing.

Every name here must also appear in DC_BINANCE_TRACE_NAMES in CMakeLists.txt.
The two are checked against each other: --selfcheck fails on a trace that is
pinned here and not passed to it, and the configure fails on a name there whose
file is missing. Neither list may be edited without the other.
"""
KNOWN_ANSWERS = {
    "binance_atomeur_d100ms_20260824.ndjson": dict(
        frames=59, payload_bytes=36162, level_entries=1583, exponent_tokens=0, rest_ok=5, pings=5, seq_ok=29, seq_break=0, removals=9, phantom_removals=1),
    "binance_atomeur_deepseed_20260824.ndjson": dict(
        frames=16, payload_bytes=10510, level_entries=938, exponent_tokens=0, rest_ok=2, pings=5, seq_ok=7, seq_break=0, removals=5, phantom_removals=4),
    "binance_btcusdt_d1000ms_20260824.ndjson": dict(
        frames=122, payload_bytes=488930, level_entries=16076, exponent_tokens=0, rest_ok=6, pings=3, seq_ok=60, seq_break=0, removals=6753, phantom_removals=3928),
    "binance_btcusdt_d100ms_20260824.ndjson": dict(
        frames=301, payload_bytes=285296, level_entries=8666, exponent_tokens=0, rest_ok=4, pings=0, seq_ok=149, seq_break=0, removals=720, phantom_removals=125),
    "binance_btcusdt_deepseed_20260824.ndjson": dict(
        frames=500, payload_bytes=749501, level_entries=25679, exponent_tokens=0, rest_ok=2, pings=1, seq_ok=249, seq_break=0, removals=5031, phantom_removals=1682),
    "binance_btcusdt_deepseed2_20260824.ndjson": dict(
        frames=500, payload_bytes=617326, level_entries=19548, exponent_tokens=0, rest_ok=1, pings=1, seq_ok=249, seq_break=0, removals=2667, phantom_removals=904),
    "binance_btcusdt_reconnect_20260824.ndjson": dict(
        frames=548, payload_bytes=707989, level_entries=21696, exponent_tokens=0, rest_ok=6, pings=2, seq_ok=546, seq_break=1, removals=8473, phantom_removals=2419),
    # --- M5 stage B2: the ruling's evidence, committed WHOLE -----------------
    # The only two rows in this table taken from a complete capture rather than
    # from a window cut out of one, and the only two at the 1000 ms audit tick.
    # `frames` is the whole 90 s and must stay that way: these files pin a
    # DECISION rather than a behaviour, and the figures the decision was taken on
    # are rates over the complete capture. See ARCHITECTURE.md 9.
    "binance_btcusdt_mixed1_20260825.ndjson": dict(
        frames=989, payload_bytes=1609880, level_entries=54283, exponent_tokens=0, rest_ok=4, pings=4, seq_ok=898, seq_break=0, removals=16766, phantom_removals=4813),
    "binance_btcusdt_mixed2_20260825.ndjson": dict(
        frames=991, payload_bytes=1130760, level_entries=39301, exponent_tokens=0, rest_ok=4, pings=4, seq_ok=900, seq_break=0, removals=10102, phantom_removals=3055),
}
