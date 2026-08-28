#!/usr/bin/env python3
"""Generate the polyphase tables in clouds_src.h.

The tables shipped in that header were computed offline and the script was
not kept, which meant the only way to change the filter was to trust a
description of how it had been made.  This is that script, and its first job
is to prove itself: --verify regenerates the 120-tap tables and diffs them
against the ones in the header.  They agree to 5e-10, which is the header's
own 9-decimal print rounding, so anything else this emits is trustworthy for
the same reason.

Design: one linear-phase Kaiser-windowed sinc prototype at the 96 kHz common
multiple of 32 and 48 kHz, normalised to unit DC gain, then decomposed into
polyphase branches -- L=3 for 32->48, L=2 for 48->32 -- each branch scaled by
L to make up for the zero-stuffing, and stored reversed so the run-time dot
product reads both coefficients and samples forward.

Usage:
    python3 tools/generate_src_tables.py --verify
    python3 tools/generate_src_tables.py --taps 60 --beta 7 --fc 12500
"""

import argparse
import math
import re
import sys


def i0(x):
    """Modified Bessel function of the first kind, order 0."""
    s, t, k = 1.0, 1.0, 1
    while True:
        t *= (x / (2.0 * k)) ** 2
        s += t
        if t < 1e-18 * s:
            return s
        k += 1


def prototype(n_taps, beta, fs, fc):
    """Kaiser-windowed sinc low-pass, linear phase, unit DC gain."""
    h = []
    mid = (n_taps - 1) / 2.0
    denom = i0(beta)
    two_fc = 2.0 * fc / fs
    for n in range(n_taps):
        x = n - mid
        sinc = two_fc if abs(x) < 1e-12 else math.sin(math.pi * two_fc * x) / (math.pi * x)
        r = (n - mid) / mid
        w = i0(beta * math.sqrt(max(0.0, 1.0 - r * r))) / denom
        h.append(sinc * w)
    total = sum(h)
    return [v / total for v in h]


def branches(h, factor):
    """Branch p is h[p], h[p+L], ... reversed and scaled by L."""
    out = []
    for p in range(factor):
        b = [h[k] * factor for k in range(p, len(h), factor)]
        b.reverse()
        out.append(b)
    return out


def response(h, fs, freqs):
    """|H(f)| in dB."""
    out = []
    for f in freqs:
        w = 2.0 * math.pi * f / fs
        re = sum(v * math.cos(-w * n) for n, v in enumerate(h))
        im = sum(v * math.sin(-w * n) for n, v in enumerate(h))
        mag = math.hypot(re, im)
        out.append((f, 20.0 * math.log10(mag) if mag > 1e-12 else -240.0))
    return out


def emit(name, table, indent="  "):
    lines = ["static const float %s[%d][%d] = {" % (name, len(table), len(table[0]))]
    for branch in table:
        lines.append(indent + "{")
        for i in range(0, len(branch), 4):
            row = branch[i:i + 4]
            lines.append(indent + "  " + " ".join("%16.9ff," % v for v in row).rstrip())
        lines.append(indent + "},")
    lines.append("};")
    return "\n".join(lines)


def read_header_tables(path):
    src = open(path).read()
    out = {}
    for name, nbr, ntap in (("kSrcUpPhase", 3, 40), ("kSrcDownPhase", 2, 60)):
        # The first occurrence is the declaration; take the values after it.
        blk = src.split(name, 1)[1]
        vals = [float(v[:-1]) for v in re.findall(r"-?\d+\.\d+f", blk)][:nbr * ntap]
        if len(vals) != nbr * ntap:
            raise SystemExit("could not read %s from %s" % (name, path))
        out[name] = [vals[i * ntap:(i + 1) * ntap] for i in range(nbr)]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--taps", type=int, default=120)
    ap.add_argument("--beta", type=float, default=8.0)
    ap.add_argument("--fc", type=float, default=14400.0)
    ap.add_argument("--fs", type=float, default=96000.0)
    ap.add_argument("--verify", action="store_true",
                    help="regenerate the shipped 120-tap tables and diff them")
    ap.add_argument("--header", default="clouds_src.h")
    args = ap.parse_args()

    if args.taps % 6:
        raise SystemExit("taps must be divisible by 6 (L=3 and L=2 branches)")

    h = prototype(args.taps, args.beta, args.fs, args.fc)
    up, down = branches(h, 3), branches(h, 2)

    if args.verify:
        shipped = read_header_tables(args.header)
        worst = 0.0
        for name, mine in (("kSrcUpPhase", up), ("kSrcDownPhase", down)):
            for a, b in zip(shipped[name], mine):
                for x, y in zip(a, b):
                    worst = max(worst, abs(x - y))
        print("max |shipped - regenerated| = %.3g" % worst)
        # The header prints 9 decimals, so anything at or below 1e-9 is the
        # printing and not the design.
        ok = worst < 1e-8
        print("VERIFY:", "the generator reproduces the shipped tables" if ok
              else "MISMATCH -- do not trust generated tables")
        return 0 if ok else 1

    freqs = [1000, 10000, 12000, 13000, 14000, 15000, 16000, 17000, 18000, 20000]
    print("/* %d taps, Kaiser beta=%g, -6 dB near %g Hz, designed at %g Hz." %
          (args.taps, args.beta, args.fc, args.fs))
    print(" * Response: " + ", ".join("%gk %.1f dB" % (f / 1000.0, db)
                                      for f, db in response(h, args.fs, freqs)))
    print(" * 16 kHz is the engine's Nyquist and so the figure that bounds both")
    print(" * the aliasing folded in and the imaging let out. */")
    print()
    print(emit("kSrcUpPhase", up))
    print()
    print(emit("kSrcDownPhase", down))
    return 0


if __name__ == "__main__":
    sys.exit(main())
