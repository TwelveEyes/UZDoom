#!/usr/bin/env python3
"""lm_state_stats.py — levelmesh state-stream cost per perflog
(.scratch/levelmesh-impl ticket 09).

Reads the r_perflog v4 state columns (lmsc candidates, lmss sectors
streamed, lmsk cluster z refreshes, lmsb bytes uploaded, lmsms CPU ms) and
`r` from every 'F' frame at or after --warmup tics, and prints one line per
file: p50 / p95 / max.

  lm_state_stats.py [--warmup 350] FILE_OR_GLOB...
      e.g. lm_state_stats.py 'logs/lm09_*vulkan.perflog'
"""

import argparse
import glob


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p))] if v else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--warmup", type=float, default=350)
    args = ap.parse_args()
    files = sorted({f for pattern in args.files for f in (glob.glob(pattern) or [pattern])})
    for f in files:
        rows = []
        with open(f, errors="replace") as fh:
            for line in fh:
                if not line.startswith("F "):
                    continue
                kv = dict(p.split("=", 1) for p in line.split()[2:] if "=" in p)
                if "lmsc" not in kv or float(kv.get("tic", 0)) < args.warmup:
                    continue
                rows.append(kv)
        name = f.split("/")[-1][:32]
        if not rows:
            print(f"{name:32s} no levelmesh state rows")
            continue

        def g(k):
            return [float(r[k]) for r in rows]

        print(f"{name:32s} n={len(rows):6d} cand p50={pct(g('lmsc'), .5):6.0f} max={max(g('lmsc')):6.0f}"
              f" | sec p50={pct(g('lmss'), .5):4.0f} max={max(g('lmss')):5.0f} | clu max={max(g('lmsk')):5.0f}"
              f" | bytes p50={pct(g('lmsb'), .5):6.0f} p95={pct(g('lmsb'), .95):7.0f} max={max(g('lmsb')):7.0f}"
              f" | ms p50={pct(g('lmsms'), .5):.4f} p95={pct(g('lmsms'), .95):.4f} max={max(g('lmsms')):.3f}"
              f" | r p50={pct(g('r'), .5):.3f}")


if __name__ == "__main__":
    main()
