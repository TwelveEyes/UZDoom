#!/usr/bin/env python3
"""Ticket 05 spike (throwaway): tabulate the spike05 perflog arms.

Reads logs/<tag>_<vantage>_vulkan.perflog for every arm and prints markdown
tables (p50/p95/p99 over the vantage window: 105 warmup + 945 tics).
"""
import glob
import json
import math
import os
import re
import sys

KIT = os.path.dirname(os.path.abspath(__file__))
LOGS = os.path.join(KIT, "logs")
WARMUP, WINDOW = 105, 945

FIELDS = ["wall", "total", "r", "fin", "ss", "sr", "gsc", "gc1", "gd1", "ghz", "gc2", "gd2",
          "cl", "c1", "c2", "cf", "tri", "mvp", "mvd", "mvc", "wl", "fl"]


def pct(vals, p):
    if not vals:
        return float("nan")
    k = max(0, math.ceil(p / 100.0 * len(vals)) - 1)
    return vals[k]


def load(path):
    frames = []
    with open(path, errors="replace") as f:
        for line in f:
            if not line.startswith("F "):
                continue
            kv = dict(p.split("=", 1) for p in line.split()[2:] if "=" in p)
            try:
                rec = {"tic": int(kv["tic"])}
                for k in FIELDS:
                    if k in kv:
                        rec[k] = float(kv[k])
                frames.append(rec)
            except (KeyError, ValueError):
                pass
    if not frames:
        return None
    t0 = frames[0]["tic"]
    sel = [x for x in frames if WARMUP <= x["tic"] - t0 < WARMUP + WINDOW]
    res = {"frames": len(sel)}
    for k in FIELDS:
        vals = sorted(x[k] for x in sel if k in x)
        if vals:
            res[k] = {"p50": pct(vals, 50), "p95": pct(vals, 95), "p99": pct(vals, 99), "max": vals[-1]}
    return res


def main():
    out = {}
    for path in sorted(glob.glob(os.path.join(LOGS, "s05*_vulkan.perflog"))):
        name = os.path.basename(path)[: -len("_vulkan.perflog")]
        m = re.match(r"(s05[a-z0-9]*_[a-z0-9]+(?:_b\d+)?)_(.+)", name)
        if not m:
            continue
        arm, vantage = m.group(1), m.group(2)
        r = load(path)
        if r:
            out.setdefault(vantage, {})[arm] = r
    json.dump(out, open(os.path.join(KIT, "results", "spike05.json"), "w"), indent=1, sort_keys=True)

    def g(v, arm, k, p="p50"):
        try:
            return out[v][arm][k][p]
        except KeyError:
            return float("nan")

    arms = ["s05_classic", "s05_hiz_b64", "s05_hiz_b128", "s05_hiz_b256", "s05_hiz_b512", "s05_fr_b64", "s05_fr_b256", "s05_nocull"]
    vants = sorted(v for v in out if any(a in out[v] for a in arms))

    print("### wall p50 / p95 (ms) per arm\n")
    print("| vantage | " + " | ".join(a[4:] for a in arms) + " |")
    print("|---" * (len(arms) + 1) + "|")
    for v in vants:
        cells = [f"{g(v, a, 'wall'):.3f} / {g(v, a, 'wall', 'p95'):.3f}" for a in arms]
        print(f"| {v} | " + " | ".join(cells) + " |")

    print("\n### CPU render window r p50 / p95 (ms)\n")
    print("| vantage | " + " | ".join(a[4:] for a in arms) + " |")
    print("|---" * (len(arms) + 1) + "|")
    for v in vants:
        cells = [f"{g(v, a, 'r'):.3f} / {g(v, a, 'r', 'p95'):.3f}" for a in arms]
        print(f"| {v} | " + " | ".join(cells) + " |")

    print("\n### GPU scene span gsc p50 / p95 (ms)\n")
    print("| vantage | " + " | ".join(a[4:] for a in arms) + " |")
    print("|---" * (len(arms) + 1) + "|")
    for v in vants:
        cells = [f"{g(v, a, 'gsc'):.3f} / {g(v, a, 'gsc', 'p95'):.3f}" for a in arms]
        print(f"| {v} | " + " | ".join(cells) + " |")

    for a in [x for x in arms if "hiz" in x]:
        print(f"\n### {a[4:]}: GPU phases p50 (ms), counts p50\n")
        print("| vantage | cull1 | draw1 | Hi-Z | cull2 | draw2 | total | clusters | drawn p1 | drawn p2 | frustum | tris |")
        print("|---|---|---|---|---|---|---|---|---|---|---|---|")
        for v in vants:
            print(f"| {v} | {g(v, a, 'gc1'):.3f} | {g(v, a, 'gd1'):.3f} | {g(v, a, 'ghz'):.3f} | {g(v, a, 'gc2'):.3f} | "
                  f"{g(v, a, 'gd2'):.3f} | {g(v, a, 'gsc'):.3f} | {g(v, a, 'cl'):.0f} | {g(v, a, 'c1'):.0f} | "
                  f"{g(v, a, 'c2'):.0f} | {g(v, a, 'cf'):.0f} | {g(v, a, 'tri'):.0f} |")

    for a in [x for x in arms if "fr_" in x or "nocull" in x]:
        print(f"\n### {a[4:]}: GPU phases p50 (ms), counts p50\n")
        print("| vantage | cull | draw | total | clusters | drawn | tris |")
        print("|---|---|---|---|---|---|---|")
        for v in vants:
            print(f"| {v} | {g(v, a, 'gc2'):.3f} | {g(v, a, 'gd2'):.3f} | {g(v, a, 'gsc'):.3f} | {g(v, a, 'cl'):.0f} | "
                  f"{g(v, a, 'c2'):.0f} | {g(v, a, 'tri'):.0f} |")

    fs = ["s05fs_hiz_b256", "s05fs_fr_b256", "s05fs_nocull"]
    fsv = sorted(v for v in out if any(a in out[v] for a in fs))
    if fsv:
        print("\n### Synthetic shading cost (r_spike_fsload 256): GPU scene span and wall, p50 (ms)\n")
        print("| vantage | " + " | ".join(f"{a[6:]} gsc / wall" for a in fs) + " |")
        print("|---" * (len(fs) + 1) + "|")
        for v in fsv:
            print(f"| {v} | " + " | ".join(f"{g(v, a, 'gsc'):.3f} / {g(v, a, 'wall'):.3f}" for a in fs) + " |")

    print("\n### Bars (hiz_b256 vs classic)\n")
    print("| vantage | classic wall p95 | hiz wall p95 | ratio | B2 (<=50%) | r p95 (B3 <= 1.0) |")
    print("|---|---|---|---|---|---|")
    for v in vants:
        c, h = g(v, "s05_classic", "wall", "p95"), g(v, "s05_hiz_b256", "wall", "p95")
        print(f"| {v} | {c:.3f} | {h:.3f} | {h / c * 100:.1f}% | {'pass' if h <= 0.5 * c else 'n/a or fail'} | {g(v, 's05_hiz_b256', 'r', 'p95'):.3f} |")

    mov = sorted(set(a for v in out for a in out[v] if a.startswith("s05mov")))
    if mov:
        print("\n### Moving-sector vantages\n")
        print("| vantage | arm | wall p50 / p95 | r p50 / p95 | moving planes p50 | dirty clusters p50 / max | mvc p50 / p95 / max (ms) |")
        print("|---|---|---|---|---|---|---|")
        for v in sorted(out):
            for a in mov:
                if a in out[v]:
                    print(f"| {v} | {a} | {g(v, a, 'wall'):.3f} / {g(v, a, 'wall', 'p95'):.3f} | "
                          f"{g(v, a, 'r'):.3f} / {g(v, a, 'r', 'p95'):.3f} | {g(v, a, 'mvp'):.0f} | "
                          f"{g(v, a, 'mvd'):.0f} / {g(v, a, 'mvd', 'max'):.0f} | "
                          f"{g(v, a, 'mvc'):.3f} / {g(v, a, 'mvc', 'p95'):.3f} / {g(v, a, 'mvc', 'max'):.3f} |")


if __name__ == "__main__":
    main()
