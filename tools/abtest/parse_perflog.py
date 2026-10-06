#!/usr/bin/env python3
"""Parse r_perflog output into median/p95 tables (ticket 09 protocol).

Protocol: 10 s warmup (350 tics) + 60 s measurement window (2100 tics),
median + p95 of total frame, CPU render window (r = RenderView), and GPU
3D window (fin = finish/present). The full per-field table is available in
the JSON output. Stdlib only.

Perflog format: see the '# r_perflog v1' header written at the top of each
file (hw_clock.cpp). 'L' lines precede the 'F' frames of their level.

Usage:
  parse_perflog.py [--warmup 350] [--window 2100]
                   [--md out.md] [--json out.json] FILE...
"""
import argparse
import contextlib
import json
import math
import statistics
import sys

TIME_FIELDS = ("total", "r", "bsp", "clip", "wr", "ws", "fr", "fs", "sr",
               "ss", "2d", "f3d", "fin", "pg", "pr", "dcms")
COUNT_FIELDS = ("wl", "fl", "sp")


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
        res[k + "_med"] = statistics.median(vals)
        res[k + "_p95"] = percentile(vals, 95)
    for k in COUNT_FIELDS:
        vals = [x[k] for x in sel if k in x]
        res[k + "_med"] = statistics.median(vals) if vals else 0.0
    return res


def fmt(x):
    return f"{x:.3f}" if isinstance(x, float) else str(x)


def main():
    ap = argparse.ArgumentParser(
        description="Parse r_perflog files into median/p95 tables.")
    ap.add_argument("--warmup", type=int, default=350,
                    help="warmup tics to skip (default 350 = 10 s)")
    ap.add_argument("--window", type=int, default=2100,
                    help="measurement window tics (default 2100 = 60 s)")
    ap.add_argument("--md", default=None, help="append a markdown table here")
    ap.add_argument("--json", dest="json_out", default=None,
                    help="write full per-file stats here")
    ap.add_argument("files", nargs="+", help="perflog files")
    args = ap.parse_args()

    results = []
    for path in args.files:
        parsed = parse_file(path)
        if parsed is None:
            continue
        level, frames = parsed
        s = summarize(frames, args.warmup, args.window)
        if s is None:
            print(f"warning: no usable frames in {path}", file=sys.stderr)
            continue
        s["file"] = path
        s["level"] = level
        results.append(s)

    # Console table: the fields ticket 09 reads, plus context.
    cols = [("file", "file"), ("map", "map"), ("sectors", "sectors"),
            ("frames", "frames"), ("total_med", "med total"),
            ("total_p95", "p95 total"), ("r_med", "med render"),
            ("r_p95", "p95 render"), ("fin_med", "med 3d-fin"),
            ("fin_p95", "p95 3d-fin")]
    rows = []
    for s in results:
        row = {}
        for key, label in cols:
            v = s[key] if key in s else s["level"].get(key, "-")
            row[label] = fmt(v) if isinstance(v, (int, float)) else str(v)
        rows.append(row)

    width = max(len(label) for _, label in cols)
    print("  ".join(label.ljust(width) for _, label in cols))
    for row in rows:
        print("  ".join(str(row[label]).ljust(width) for _, label in cols))

    if args.json_out:
        try:
            with open(args.json_out, "w") as f:
                json.dump(results, f, indent=1)
        except OSError as e:
            raise SystemExit(f"{args.json_out}: {e}") from e

    if args.md:
        try:
            with open(args.md, "a") as f:
                f.write("\n| " + " | ".join(label for _, label in cols) +
                        " |\n")
                f.write("|" + "---|" * len(cols) + "\n")
                for row in rows:
                    f.write("| " + " | ".join(str(row[label])
                                               for _, label in cols) +
                            " |\n")
        except OSError as e:
            raise SystemExit(f"{args.md}: {e}") from e


if __name__ == "__main__":
    main()
