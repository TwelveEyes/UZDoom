#!/usr/bin/env python3
"""select_vantages.py — derive fixed stress vantages from the v2 tour runs.

Perf baseline v2 (.scratch/levelmesh-impl/issues/02-perf-baseline-v2.md).
For each target map, the v2 tour perflog (logs/v2_<MAP>_vulkan.perflog) is
scanned per demo tic; the tic whose median metric is highest wins, skipping
the first SKIP tics after every warp (texture upload / settling transients)
and keeping winners on distinct stops. The metric is the classic render CPU
window `r` (or wl+fl for maps too small for `r` to mean anything).

A tour view at tic t is (stop position, start angle + t * yaw_fp), so the
vantage demo warps to the stop and turns by t * yaw_fp on its first tic
(make_demo.py --path vantage). Writes demos/vantage/<id>.lmp and
vantages.json (the manifest record).

Usage: select_vantages.py [--tics 1050]
"""
import argparse
import json
import os
import statistics
import subprocess
import sys

KIT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, KIT)
import make_demo as md  # noqa: E402

HOLD = 100
SKIP = 10
WIN = 4  # half-width of the sliding median window (9 tics)
YAW_FP = int(round(md.ANGLE_FPP / HOLD))

# (demo name, map, wad under wads/, metric, count, why)
TARGETS = [
    ("MYHOUSE_MAP01", "MAP01", "myhouse.pk3", "r", 2, "huge map vista (166k lines)"),
    ("MYHOUSE_20PAM", "20PAM", "myhouse.pk3", "r", 1, "line portals + sector_link"),
    ("PLANISF_MAP01", "MAP01", "planisf2.wad", "r", 2, "large open vista (37k lines)"),
    ("SOS_MAP32", "MAP32", "SOS_Boom.wad", "r", 2, "slaughter-scale vista (62k lines)"),
    ("PIRATES_MAP49", "MAP49", "Pirates!.wad", "r", 1, "3D-floor dense"),
    ("PIRATES_MAP54", "MAP54", "Pirates!.wad", "r", 1, "3D-floor dense"),
    ("DOOM2_MAP27", "MAP27", "DOOM2.WAD", "geom", 1, "largest open area"),
    ("DOOM2_MAP31", "MAP31", "DOOM2.WAD", "geom", 1, "largest open area"),
]


def per_tic(path, metric):
    vals = {}
    with open(path) as f:
        for line in f:
            if not line.startswith("F "):
                continue
            kv = dict(p.split("=", 1) for p in line.split()[2:] if "=" in p)
            t = int(kv["tic"])
            v = float(kv["r"]) if metric == "r" else float(kv["wl"]) + float(kv["fl"])
            vals.setdefault(t, []).append(v)
    return {t: statistics.median(v) for t, v in vals.items()}


def main():
    ap = argparse.ArgumentParser(description="derive stress vantages")
    ap.add_argument("--tics", type=int, default=1050, help="vantage demo length")
    a = ap.parse_args()
    outdir = os.path.join(KIT, "demos", "vantage")
    os.makedirs(outdir, exist_ok=True)
    records = []
    for demo, mapname, wad, metric, count, why in TARGETS:
        plog = os.path.join(KIT, "logs", f"v2_{demo}_vulkan.perflog")
        if not os.path.isfile(plog):
            sys.exit(f"missing {plog}: run the v2 tour set first")
        lines, secs = md.load_level(os.path.join(KIT, "wads", wad), mapname)
        stops, _ = md.tour_stops(lines, secs, (2600 + HOLD - 1) // HOLD)
        tics = per_tic(plog, metric)
        # Sustained view cost, not hitches: rank by the median of the per-tic
        # values over a centred window that stays inside one stop (texture
        # uploads on first sight, GC and level-start spikes last 1-2 tics).
        ranked = []
        for t in tics:
            lo, hi = t - WIN, t + WIN
            if lo % HOLD < SKIP or lo // HOLD != hi // HOLD:
                continue
            win = [tics[u] for u in range(lo, hi + 1) if u in tics]
            if len(win) == 2 * WIN + 1:
                ranked.append((statistics.median(win), t))
        ranked.sort()
        used = set()
        n = 0
        for v, t in reversed(ranked):
            s = t // HOLD
            if s in used or s >= len(stops):
                continue
            used.add(s)
            n += 1
            vid = f"{demo}_V{n}"
            x, y = stops[s]
            turn = t * YAW_FP
            out = os.path.join(outdir, f"{vid}.lmp")
            subprocess.run([sys.executable, os.path.join(KIT, "make_demo.py"),
                            "--path", "vantage", "--map", mapname,
                            "--iwad", os.path.join(KIT, "wads", wad),
                            "--at", f"{x:.0f},{y:.0f}", "--turn", str(turn),
                            "--tics", str(a.tics), "--out", out],
                           check=True, stdout=subprocess.DEVNULL)
            records.append({"id": vid, "demo": demo, "map": mapname, "wad": wad,
                            "x": round(x), "y": round(y), "turn": turn,
                            "tour_tic": t, "metric": metric, "tour_value": v,
                            "why": why})
            print(f"{vid}: ({x:.0f},{y:.0f}) turn {turn} <- tour tic {t} "
                  f"{metric}={v:.3f}  [{why}]")
            if n >= count:
                break
    with open(os.path.join(KIT, "vantages.json"), "w") as f:
        json.dump({"tics": a.tics, "hold": HOLD, "skip": SKIP, "win": WIN,
                   "yaw_fp": YAW_FP,
                   "vantages": records}, f, indent=1)
    print(f"{len(records)} vantages across {len({r['demo'] for r in records})} maps")


if __name__ == "__main__":
    main()
