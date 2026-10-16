#!/usr/bin/env python3
"""ab_sheet.py — inspect the captures of an abtest.py run.

Every subcommand takes a run as a run id (`20261015T171524Z-ab`), a path,
or `latest` (the newest directory under runs/).

  ab_sheet.py sheet RUN COMBO TICK [TICK ...] [--crop x0,y0,x1,y1] [--out PNG]
      One row per tick: A | B | diff (red = max channel delta x 8, any
      differing pixel tinted green). Prints differing pixels and max delta
      per tick. Default output: <run>/<combo>/report/sheet_<ticks>.png.

  ab_sheet.py summary RUN [COMBO]
      Per combo: verdict counts, total differing pixels over all captured
      ticks and the worst tick.

  ab_sheet.py failsets RUN_A RUN_B [COMBO]
      Failing-tick sets of two runs of the same combo, and the ticks that
      fail in only one of them (e.g. full lighting vs --fullbright).

  ab_sheet.py bbox RUN [COMBO] [--ticks all|fail] [--threshold N]
      Per tick (default: the failing ones): the bounding box of the pixels
      whose max channel delta exceeds the threshold (default 2, the
      tolerance), and the max delta. Tells a localized difference (one
      surface) from screen-wide noise or a band (e.g. ceiling speckle).

  ab_sheet.py clusters RUN [COMBO] [--min-px N]
      Per captured tick: verdict, cluster count, and the clusters of at
      least N pixels (default 20) no allowlist entry covers (bbox, pixels,
      max delta), from results.json; a failing tick without such clusters
      prints the verdict detail (e.g. a cluster over 1M px is not recorded).

Requires numpy and Pillow. Paths are relative to this directory's runs/.
"""
import argparse
import glob
import json
import os
import re
import sys

import numpy as np
from PIL import Image

KIT = os.path.dirname(os.path.abspath(__file__))
RUNS = os.path.join(KIT, "runs")


def run_dir(run):
    if run == "latest":
        runs = sorted(d for d in os.listdir(RUNS) if os.path.isdir(os.path.join(RUNS, d)))
        if not runs:
            sys.exit("no runs under " + RUNS)
        return os.path.join(RUNS, runs[-1])
    if os.path.isdir(run):
        return run
    path = os.path.join(RUNS, run)
    if not os.path.isdir(path):
        sys.exit("no such run: " + run)
    return path


def results(rdir):
    with open(os.path.join(rdir, "results.json")) as f:
        return json.load(f)["results"]


def pick_combo(rdir, combo):
    combos = [r["combo"] for r in results(rdir)]
    if combo is None:
        if len(combos) != 1:
            sys.exit("run has several combos, name one: " + ", ".join(combos))
        return combos[0]
    hits = [c for c in combos if c == combo] or [c for c in combos if combo in c]
    if len(hits) != 1:
        sys.exit(f"combo {combo!r} matches {hits or 'nothing'} of " + ", ".join(combos))
    return hits[0]


def captures(rdir, combo):
    """tick -> (path a, path b) for every tick captured on both paths."""
    out = {}
    for pa in glob.glob(os.path.join(rdir, combo, "a", "png", "*.png")):
        m = re.match(r"(.+)_(\d+)\.png$", os.path.basename(pa))
        if not m:
            continue
        pb = os.path.join(rdir, combo, "b", "png", os.path.basename(pa))
        if os.path.isfile(pb):
            out[int(m.group(2))] = (pa, pb)
    return out


def load(path, crop=None):
    im = Image.open(path).convert("RGB")
    if crop:
        im = im.crop(crop)
    return np.asarray(im).astype(np.int16)


def delta(a, b):
    return np.abs(a - b).max(axis=2)


def cmd_sheet(args):
    rdir = run_dir(args.run)
    combo = pick_combo(rdir, args.combo)
    caps = captures(rdir, combo)
    crop = tuple(int(v) for v in args.crop.split(",")) if args.crop else None
    rows = []
    for t in args.ticks:
        if t not in caps:
            sys.exit(f"tick {t} not captured on both paths (have {sorted(caps)[:8]}...)")
        a, b = (load(p, crop) for p in caps[t])
        d = delta(a, b)
        dv = np.zeros_like(a)
        dv[..., 0] = np.clip(d * 8, 0, 255)
        dv[..., 1] = (d > 0) * 60
        rows.append(np.concatenate([a, b, dv], axis=1))
        print(f"tick {t}: differing px {(d > 0).sum()}, max delta {d.max()}")
    img = Image.fromarray(np.concatenate(rows, axis=0).astype(np.uint8))
    if img.width > args.width:
        img = img.resize((args.width, img.height * args.width // img.width))
    out = args.out or os.path.join(rdir, combo, "report", "sheet_" + "_".join(map(str, args.ticks)) + ".png")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    img.save(out)
    print("wrote", out)


def cmd_summary(args):
    rdir = run_dir(args.run)
    for r in results(rdir):
        if args.combo and args.combo not in r["combo"]:
            continue
        counts = {}
        for row in r.get("rows", []):
            counts[row["verdict"]] = counts.get(row["verdict"], 0) + 1
        total, worst = 0, (0, None)
        for t, (pa, pb) in sorted(captures(rdir, r["combo"]).items()):
            n = int((delta(load(pa), load(pb)) > 0).sum())
            total += n
            if n > worst[0]:
                worst = (n, t)
        verdicts = ", ".join(f"{k} {v}" for k, v in sorted(counts.items()))
        print(f"{r['combo']}: {verdicts}; differing px total {total}, worst tick {worst[1]} ({worst[0]} px)")


def fail_ticks(rdir, combo):
    r = [x for x in results(rdir) if x["combo"] == combo][0]
    return {row["tick"] for row in r.get("rows", []) if row["verdict"] == "fail"}


def cmd_failsets(args):
    ra, rb = run_dir(args.run_a), run_dir(args.run_b)
    combo = pick_combo(ra, args.combo)
    fa, fb = fail_ticks(ra, combo), fail_ticks(rb, pick_combo(rb, combo))
    print(f"{combo}: {os.path.basename(ra)} fails {len(fa)}, {os.path.basename(rb)} fails {len(fb)}")
    print("  only in", os.path.basename(ra), sorted(fa - fb))
    print("  only in", os.path.basename(rb), sorted(fb - fa))


def cmd_bbox(args):
    rdir = run_dir(args.run)
    combo = pick_combo(rdir, args.combo)
    caps = captures(rdir, combo)
    ticks = sorted(caps) if args.ticks == "all" else sorted(fail_ticks(rdir, combo) & set(caps))
    for t in ticks:
        d = delta(load(caps[t][0]), load(caps[t][1]))
        ys, xs = np.nonzero(d > args.threshold)
        box = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1) if len(xs) else None
        print(f"tick {t}: bbox {box}, px {len(xs)}, max delta {int(d.max())}")


def cmd_clusters(args):
    rdir = run_dir(args.run)
    combo = pick_combo(rdir, args.combo)
    r = [x for x in results(rdir) if x["combo"] == combo][0]
    for row in r.get("rows", []):
        clusters = row.get("clusters", [])
        big = [(k["bbox"], k["count"], k["max_delta"]) for k in clusters
               if k["count"] >= args.min_px and k.get("covered_by") is None]
        print(row["tick"], row["verdict"], len(clusters), big[:6], "" if big or row["verdict"] != "fail" else row.get("detail", ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("sheet")
    p.add_argument("run")
    p.add_argument("combo")
    p.add_argument("ticks", nargs="+", type=int)
    p.add_argument("--crop")
    p.add_argument("--out")
    p.add_argument("--width", type=int, default=1800)
    p.set_defaults(func=cmd_sheet)
    p = sub.add_parser("summary")
    p.add_argument("run")
    p.add_argument("combo", nargs="?")
    p.set_defaults(func=cmd_summary)
    p = sub.add_parser("failsets")
    p.add_argument("run_a")
    p.add_argument("run_b")
    p.add_argument("combo", nargs="?")
    p.set_defaults(func=cmd_failsets)
    p = sub.add_parser("bbox")
    p.add_argument("run")
    p.add_argument("combo", nargs="?")
    p.add_argument("--ticks", choices=("fail", "all"), default="fail")
    p.add_argument("--threshold", type=int, default=2)
    p.set_defaults(func=cmd_bbox)
    p = sub.add_parser("clusters")
    p.add_argument("run")
    p.add_argument("combo", nargs="?")
    p.add_argument("--min-px", type=int, default=20)
    p.set_defaults(func=cmd_clusters)
    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
