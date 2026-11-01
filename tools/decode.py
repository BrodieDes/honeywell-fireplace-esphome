"""Decode RT8220A captures: raw durations -> half-bit stream -> repeating frame.

Usage:  python tools/decode.py captures/session-XXXX.jsonl
"""
import json
import sys
from collections import Counter

UNIT = 210  # us, half-bit period measured from captures


def halfbits(durations: list[int]) -> str:
    """Quantise each pulse to 1 or 2 units. Highs read ~45 us short and lows ~45 us long
    (receiver asymmetry), so split at different thresholds."""
    out = []
    for v in durations:
        if v > 0:
            n = 1 if v < 290 else 2 if v < 600 else round(v / UNIT)
            out.append("1" * n)
        else:
            n = 1 if -v < 330 else 2 if -v < 650 else round(-v / UNIT)
            out.append("0" * n)
    return "".join(out)


def period(s: str, lo: int = 40, hi: int = 400) -> int:
    """Smallest shift at which the stream matches itself best."""
    best = max(range(lo, min(hi, len(s) // 2)),
               key=lambda p: sum(a == b for a, b in zip(s, s[p:])) / (len(s) - p))
    return best


def frame(s: str, p: int) -> str:
    """One period, rotated to start at the beginning of the longest alternating run
    (the preamble). Uses the middle of the stream to avoid edge glitches."""
    mid = len(s) // 2 - p // 2
    f = s[mid:mid + p]
    ring = f + f
    best_start, best_len = 0, 0
    for start in range(p):
        n = 1
        while n < p and ring[start + n] != ring[start + n - 1]:
            n += 1
        if n > best_len:
            best_start, best_len = start, n
    return ring[best_start:best_start + p]


def manchester(f: str) -> tuple[str, int]:
    """Decode half-bit pairs; try both phases, return bits and count of invalid pairs."""
    best = None
    for phase in (0, 1):
        pairs = [f[i:i + 2] for i in range(phase, len(f) - 1, 2)]
        bits = "".join("1" if q == "10" else "0" if q == "01" else "?" for q in pairs)
        bad = bits.count("?")
        if best is None or bad < best[1]:
            best = (bits, bad)
    return best


def main(path: str) -> None:
    rows = [json.loads(line) for line in open(path)]
    frames = []
    for row in rows:
        for i, cap in enumerate(row["captures"]):
            s = halfbits(cap)
            p = period(s)
            f = frame(s, p)
            bits, bad = manchester(f)
            frames.append((row["step"], row["display"].upper(), i, f, bits, bad))
            print(f"{row['step']:8s} {row['display'].upper():5s} cap{i} period={p} bad_pairs={bad}")
    print()
    for step, disp, i, f, bits, bad in frames:
        print(f"{step:8s} {disp:5s} cap{i} {f}")
    print()
    for step, disp, i, f, bits, bad in frames:
        print(f"{step:8s} {disp:5s} cap{i} {bits}")


if __name__ == "__main__":
    main(sys.argv[1])
