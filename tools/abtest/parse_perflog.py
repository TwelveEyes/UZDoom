#!/usr/bin/env python3
"""Parse r_perflog output into p50/p95/p99/max tables (perf baseline v2).

Effort: .scratch/levelmesh-impl/issues/02-perf-baseline-v2.md.

A run's frames are windowed by demo tic: --warmup tics are skipped (counted
from the first logged frame), then --window tics are measured. For every
time field the result carries p50/p95/p99/max; for every count field
p50/p95/max. `wall` (r_perflog v2) is the real wall-clock interval between
two logged frames; `total` only brackets D_Display, so `wall` is the honest
frame time and `total` the display share of it.

Non-empty-view check (--min-geom N --max-empty F): a run passes only if the
median of wl+fl (walls + flats processed per frame) over the window is >= N
and at most a fraction F of the frames process no wall or flat at all. A
run whose camera looks at nothing measures present/2D overhead, not
rendering. Protocol values: --min-geom 5 --max-empty 0.10.

Perflog format: see the '# r_perflog vN' header (hw_clock.cpp). 'L' lines
precede the 'F' frames of their level. Older logs (v1: no wall column; before
v10: no light list columns) still parse: missing fields are left out.

Usage:
  parse_perflog.py [--warmup 350] [--window 2100] [--min-geom 0]
                   [--md out.md] [--json out.json] FILE...
Exit status: 1 if any file fails --min-geom (or has no usable frames).
"""
import argparse
import contextlib
import json
import math
import sys

TIME_FIELDS = ("wall", "total", "r", "bsp", "clip", "wr", "ws", "fr", "fs",
               "sr", "ss", "2d", "f3d", "fin", "pg", "pr", "dcms",
               # levelmesh (r_perflog v10, ticket 19): draw / total GPU ms, light
               # list CPU ms, cluster light refine GPU ms; absent in older logs
               "lmd1", "lmgpu", "lmlms", "lmlf")
COUNT_FIELDS = ("wl", "fl", "sp", "dec", "portals", "wv", "fv", "cbuf",
                "lmlt", "lmlr", "lmlo", "lmlp", "lmlix", "lmlc")


def parse_file(path):
    level = {}
    frames = []
    try:
        with open(path) as f:
            for line in f:
                parts = line.strip().split()
                if not parts:
                    continue
                if parts[0] == "F":
                    # parts[1] is the bare frame number; the rest is k=v
                    kv = dict(p.split("=", 1) for p in parts[1:] if "=" in p)
                    try:
                        rec = {"tic": float(kv["tic"])}
                        for k in TIME_FIELDS + COUNT_FIELDS:
                            if k in kv:
                                rec[k] = float(kv[k])
                    except (KeyError, ValueError):
                        continue
                    frames.append(rec)
                elif parts[0] == "L":
                    kv = dict(p.split("=", 1) for p in parts[1:])
                    with contextlib.suppress(ValueError):
                        level = {k: (v if k == "map" else float(v))
                                 for k, v in kv.items()}
        return level, frames
    except OSError as e:
        print(f"warning: {e}", file=sys.stderr)
        return None


def percentile(sorted_vals, p):
    """Nearest-rank percentile on a pre-sorted list."""
    if not sorted_vals:
        return float("nan")
    k = max(0, math.ceil(p / 100.0 * len(sorted_vals)) - 1)
    return sorted_vals[k]


def summarize(frames, warmup, window):
    if not frames:
        return None
    t0 = frames[0]["tic"]
    sel = [x for x in frames if warmup <= x["tic"] - t0 < warmup + window]
    if not sel:
        # Demo shorter than warmup+window: use everything after warmup.
        sel = [x for x in frames if x["tic"] - t0 >= warmup]
    if not sel:
        return None
    res = {"frames": len(sel), "tic0": t0,
           "tic_start": sel[0]["tic"], "tic_end": sel[-1]["tic"]}
    for k in TIME_FIELDS:
        vals = sorted(x[k] for x in sel if k in x)
        if not vals:
            continue
        res[k + "_p50"] = percentile(vals, 50)
        res[k + "_p95"] = percentile(vals, 95)
        res[k + "_p99"] = percentile(vals, 99)
        res[k + "_max"] = vals[-1]
    for k in COUNT_FIELDS:
        vals = sorted(x[k] for x in sel if k in x)
        if not vals:
            continue
        res[k + "_p50"] = percentile(vals, 50)
        res[k + "_p95"] = percentile(vals, 95)
        res[k + "_max"] = vals[-1]
    geom = sorted(x.get("wl", 0.0) + x.get("fl", 0.0) for x in sel)
    res["geom_p50"] = percentile(geom, 50)
    res["empty_frac"] = sum(1 for g in geom if g == 0) / len(geom)
    # Wall-clock coverage: how much of the real frame D_Display brackets.
    if "wall_p50" in res:
        walls = [x["wall"] for x in sel if "wall" in x]
        totals = [x["total"] for x in sel if "wall" in x and "total" in x]
        res["wall_sum_s"] = sum(walls) / 1000.0
        res["total_sum_s"] = sum(totals) / 1000.0
    return res


def fmt(x):
    return f"{x:.3f}" if isinstance(x, float) else str(x)


COLS = [("file", "file"), ("map", "map"), ("frames", "frames"),
        ("geom_p50", "wl+fl p50"), ("empty_frac", "empty"), ("wall_p50", "wall p50"),
        ("wall_p95", "wall p95"), ("wall_p99", "wall p99"),
        ("total_p50", "total p50"), ("total_p95", "total p95"),
        ("r_p50", "r p50"), ("r_p95", "r p95"), ("r_p99", "r p99"),
        ("fin_p50", "fin p50"), ("fin_p95", "fin p95")]


def main():
    ap = argparse.ArgumentParser(
        description="Parse r_perflog files into p50/p95/p99/max tables.")
    ap.add_argument("--warmup", type=int, default=350,
                    help="warmup tics to skip (default 350 = 10 s)")
    ap.add_argument("--window", type=int, default=2100,
                    help="measurement window tics (default 2100 = 60 s)")
    ap.add_argument("--min-geom", type=float, default=0.0,
                    help="fail a run whose median wl+fl is below this")
    ap.add_argument("--max-empty", type=float, default=1.0,
                    help="fail a run whose fraction of frames with wl+fl == 0 "
                         "exceeds this (camera outside the map / in the void)")
    ap.add_argument("--md", default=None, help="append a markdown table here")
    ap.add_argument("--json", dest="json_out", default=None,
                    help="write full per-file stats here")
    ap.add_argument("files", nargs="+", help="perflog files")
    args = ap.parse_args()

    results = []
    failed = 0
    for path in args.files:
        parsed = parse_file(path)
        if parsed is None:
            failed += 1
            continue
        level, frames = parsed
        s = summarize(frames, args.warmup, args.window)
        if s is None:
            print(f"warning: no usable frames in {path}", file=sys.stderr)
            failed += 1
            continue
        s["file"] = path
        s["level"] = level
        s["nonempty"] = (s["geom_p50"] >= args.min_geom
                         and s["empty_frac"] <= args.max_empty)
        if not s["nonempty"]:
            print(f"FAIL non-empty check: {path}: median wl+fl "
                  f"{s['geom_p50']:.0f} (min {args.min_geom:.0f}), empty frames "
                  f"{s['empty_frac']:.1%} (max {args.max_empty:.0%})", file=sys.stderr)
            failed += 1
        results.append(s)

    rows = []
    for s in results:
        row = {}
        for key, label in COLS:
            v = s[key] if key in s else s["level"].get(key, "-")
            row[label] = fmt(v) if isinstance(v, (int, float)) else str(v)
        rows.append(row)

    width = max(len(label) for _, label in COLS)
    print("  ".join(label.ljust(width) for _, label in COLS))
    for row in rows:
        print("  ".join(str(row[label]).ljust(width) for _, label in COLS))

    if args.json_out:
        try:
            with open(args.json_out, "w") as f:
                json.dump(results, f, indent=1)
        except OSError as e:
            raise SystemExit(f"{args.json_out}: {e}") from e

    if args.md:
        try:
            with open(args.md, "a") as f:
                f.write("\n| " + " | ".join(label for _, label in COLS) + " |\n")
                f.write("|" + "---|" * len(COLS) + "\n")
                for row in rows:
                    f.write("| " + " | ".join(str(row[label]) for _, label in COLS)
                            + " |\n")
        except OSError as e:
            raise SystemExit(f"{args.md}: {e}") from e

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
