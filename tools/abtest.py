#!/usr/bin/env python3
"""
abtest.py — levelmesh A/B capture + pixel-diff driver (ticket 01).

Drives the engine's always-compiled A/B capture mode (r_ab_capture,
src/d_abcapture.cpp) to measure classic-vs-levelmesh pixel parity at
zero tolerance, per spec .scratch/levelmesh-rendering/spec.md §9:

  per map x mode x backend: TWO fresh-level-load launches (path A
  gl_uselevelmesh 0, path B gl_uselevelmesh 1), same-backend pairs only,
  config applied per run via a temp config (-config) + pin script
  (-exec). Comparator: per-pixel diff of each tick's PNG pair,
  connected-component clusters, any pixel outside the frozen per-map
  allowlist fails the run (ADR 0004: z-fighting only, human-reviewed,
  shrink-only).

The tool proves itself before any levelmesh work is measured:
`run --determinism` re-runs the acceptance map classic-vs-classic
(cvar 0 on both fresh launches) across {static, demo} x {gl33, vulkan}
and requires zero diff plus byte-identical PNGs between repeats.

Requirements: Python 3, Pillow. WADs in build/wads/, engine at
build/uzdoom (override with --engine). Runs MUST happen on a real-GPU
machine with a display, unsandboxed — a sandboxed shell silently falls
back to llvmpipe / GL (rejected by the reality checks).

Usage:
  abtest.py run [--determinism] [--map NAME] [--only PAT] [--mode M]
                [--backend B] [--manifest F] [--allowlists DIR]
                [--engine PATH] [--timeout S] [--runs DIR]
  abtest.py diff  DIRA DIRB [--map NAME] [--mode M] [--allowlists DIR]
  abtest.py record MAP MODE BACKEND DIRA DIRB [--allowlists DIR]
  abtest.py validate [--manifest F] [--allowlists DIR]

Exit: 0 all pass, 1 any check failed, 2 usage/setup error.
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from typing import NoReturn

try:
    from PIL import Image, ImageChops
except ImportError:
    print("error: Pillow is required (pip install pillow)", file=sys.stderr)
    sys.exit(2)

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
KIT = os.path.join(HERE, "abtest")
DEFAULT_MANIFEST = os.path.join(KIT, "ab_manifest.json")
DEFAULT_ALLOWLISTS = os.path.join(KIT, "allowlists")
DEFAULT_RUNS = os.path.join(KIT, "runs")

MODES = ("static", "demo")
BACKENDS = ("gl33", "vulkan")
BACKEND_CVAR = {"gl33": 0, "vulkan": 1}  # vid_preferbackend
BACKEND_LINE = {
    "gl33": "Selecting OpenGL backend",
    "vulkan": "Selecting Vulkan backend",
}


# --------------------------------------------------------------------------
# manifest
# --------------------------------------------------------------------------

def load_manifest(path):
    if not os.path.isfile(path):
        die(f"manifest not found: {path}")
    try:
        with open(path) as f:
            m = json.load(f)
    except (OSError, ValueError) as e:
        die(f"manifest {path}: unreadable ({e})")
    for key in ("format", "resolution", "backends", "static_ticks_default",
                "demo_ticks", "maps"):
        if key not in m:
            die(f"manifest {path}: missing key {key!r}")
    if m["format"] != "ab-manifest/1":
        die(f"manifest {path}: unknown format {m['format']!r}")
    w, h = m["resolution"]
    if not (isinstance(w, int) and isinstance(h, int) and w > 0 and h > 0):
        die(f"manifest: bad resolution {m['resolution']!r}")
    for b in BACKENDS:
        if b not in m["backends"] or not isinstance(m["backends"][b], int):
            die(f"manifest: backends must map {BACKENDS} to ints")
    dt = m["demo_ticks"]
    for key in ("step", "start", "last"):
        if not isinstance(dt.get(key), int) or dt[key] <= 0:
            die(f"manifest: demo_ticks.{key} must be a positive int")
    if dt["step"] < dt["start"]:
        die("manifest: demo_ticks.step >= demo_ticks.start")
    if not isinstance(m["static_ticks_default"], list) or not m["static_ticks_default"]:
        die("manifest: static_ticks_default must be a non-empty tick list")
    seen = set()
    for mp in m["maps"]:
        for key in ("name", "iwad", "warp", "demo"):
            if not mp.get(key):
                die(f"manifest: map missing {key}: {mp!r}")
        if mp["name"] in seen:
            die(f"manifest: duplicate map {mp['name']}")
        seen.add(mp["name"])
        st = mp.get("static_ticks")
        if st is not None and (not isinstance(st, list) or not st):
            die(f"manifest: {mp['name']} static_ticks must be null or a non-empty list")
    return m


def static_tick_list(manifest, mp):
    ticks = mp["static_ticks"] if mp["static_ticks"] is not None \
        else manifest["static_ticks_default"]
    ticks = list(dict.fromkeys(int(t) for t in ticks))
    ticks.sort()
    if any(t <= 0 for t in ticks):
        die(f"map {mp['name']}: ticks must be positive")
    return ticks


def demo_tick_list(manifest):
    dt = manifest["demo_ticks"]
    ticks = list(range(dt["start"], dt["last"] + 1, dt["step"]))
    if ticks[-1] != dt["last"]:
        ticks.append(dt["last"])
    return ticks


# --------------------------------------------------------------------------
# allowlists
# --------------------------------------------------------------------------

def load_allowlist(dirpath, mapname):
    """Return (entries, error). entries: list of dicts for this map (all modes)."""
    path = os.path.join(dirpath, f"{mapname}.json")
    if not os.path.isfile(path):
        return [], None
    try:
        with open(path) as f:
            doc = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        return None, f"{path}: unreadable ({e})"
    if doc.get("format") != "ab-allowlist/1":
        return None, f"{path}: unknown format {doc.get('format')!r}"
    if doc.get("map") != mapname:
        return None, f"{path}: map field {doc.get('map')!r} != {mapname}"
    entries = []
    for i, e in enumerate(doc.get("entries", [])):
        for key in ("mode", "tick", "cx", "cy", "radius", "reason"):
            if key not in e:
                return None, f"{path}: entry {i} missing {key!r}"
        if e["mode"] not in MODES:
            return None, f"{path}: entry {i} bad mode {e['mode']!r}"
        if e["tick"] is not None and not isinstance(e["tick"], int):
            return None, f"{path}: entry {i} tick must be int or null"
        for key in ("cx", "cy", "radius"):
            if not isinstance(e[key], (int, float)):
                return None, f"{path}: entry {i} {key} must be numeric"
        if e["radius"] <= 0:
            return None, f"{path}: entry {i} radius must be > 0"
        if not isinstance(e["reason"], str) or not e["reason"].strip():
            return None, f"{path}: entry {i} reason must be a non-empty string"
        entries.append(e)
    return entries, None


def entries_for_tick(entries, mode, tick):
    out = []
    for e in entries:
        if e["mode"] != mode:
            continue
        if e["tick"] is not None and e["tick"] != tick:
            continue
        out.append(e)
    return out


# --------------------------------------------------------------------------
# comparator
# --------------------------------------------------------------------------

def _read_bytes(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None


def extract_clusters(diff, width, height, max_cluster_pixels=1_000_000):
    """4-connected components of a 0/255 'L' image. Returns list of dicts
    with bbox, pixels (flat indices), count, centroid, radius."""
    data = bytes(diff)
    n = width * height
    visited = bytearray(n)
    clusters = []
    idx = 0
    while idx < n:
        if data[idx] and not visited[idx]:
            stack = [idx]
            visited[idx] = 1
            px = []
            x0 = y0 = 1 << 30
            x1 = y1 = -1
            while stack:
                j = stack.pop()
                px.append(j)
                x = j % width
                y = j // width
                if x < x0:
                    x0 = x
                if x > x1:
                    x1 = x
                if y < y0:
                    y0 = y
                if y > y1:
                    y1 = y
                if x > 0:
                    k = j - 1
                    if data[k] and not visited[k]:
                        visited[k] = 1
                        stack.append(k)
                if x + 1 < width:
                    k = j + 1
                    if data[k] and not visited[k]:
                        visited[k] = 1
                        stack.append(k)
                if y > 0:
                    k = j - width
                    if data[k] and not visited[k]:
                        visited[k] = 1
                        stack.append(k)
                if y + 1 < height:
                    k = j + width
                    if data[k] and not visited[k]:
                        visited[k] = 1
                        stack.append(k)
            if len(px) > max_cluster_pixels:
                raise RuntimeError(
                    f"cluster of {len(px)} px exceeds {max_cluster_pixels} px guard")
            cx = sum(p % width for p in px) / len(px)
            cy = sum(p // width for p in px) / len(px)
            r2 = max(((p % width - cx) ** 2 + (p // width - cy) ** 2) for p in px)
            clusters.append({
                "bbox": [x0, y0, x1, y1],
                "count": len(px),
                "cx": round(cx, 2),
                "cy": round(cy, 2),
                "radius": round(r2 ** 0.5, 2),
                "_pixels": px,
            })
        idx += 1
    return clusters


def cluster_covered(cl, entries, width):
    """Covered iff EVERY pixel of the cluster lies within some entry's
    circle (center + radius)."""
    pix = cl["_pixels"]
    for e in entries:
        r2 = float(e["radius"]) ** 2
        ex, ey = float(e["cx"]), float(e["cy"])
        ok = True
        for p in pix:
            dx = (p % width) - ex
            dy = (p // width) - ey
            if dx * dx + dy * dy > r2:
                ok = False
                break
        if ok:
            return True
    return False


def compare_tick(path_a, path_b, entries_for_this_tick, strict):
    """Compare one tick's PNG pair. Returns (verdict, detail, cluster_summaries).
    verdict: 'identical' | 'pass' | 'fail'."""
    ba = _read_bytes(path_a)
    bb = _read_bytes(path_b)
    if ba is None or bb is None:
        bad = [os.path.basename(p) for p, b in ((path_a, ba), (path_b, bb)) if b is None]
        return "fail", f"unreadable PNG: {', '.join(bad)}", []
    if hashlib.sha256(ba).hexdigest() == hashlib.sha256(bb).hexdigest():
        return "identical", "byte-identical", []
    try:
        im_a = Image.open(path_a)
        im_a.load()
        im_b = Image.open(path_b)
        im_b.load()
    except Exception as e:
        return "fail", f"unreadable image ({e.__class__.__name__}: {e})", []
    if im_a.size != im_b.size or im_a.mode != im_b.mode:
        return "fail", f"mismatched images: {im_a.size}/{im_a.mode} vs {im_b.size}/{im_b.mode}", []
    width, height = im_a.size
    diff = ImageChops.difference(im_a.convert("RGB"), im_b.convert("RGB")).convert("L")
    diff = diff.point(lambda v: 255 if v > 0 else 0)
    if diff.getbbox() is None:
        return "pass", "zero pixel diff", []
    try:
        clusters = extract_clusters(diff.tobytes(), width, height)  # 'L': 1 byte/px
    except RuntimeError as e:
        # A single diff region so large it cannot be enumerated (e.g. the whole
        # frame) is a clear zero-tolerance failure; record it and let the run
        # continue to the next tick/combo instead of crashing the whole matrix.
        return "fail", f"{e}; treated as fail", []
    uncovered = []
    summaries = []
    for cl in clusters:
        s = {k: v for k, v in cl.items() if k != "_pixels"}
        summaries.append(s)
        if strict or not cluster_covered(cl, entries_for_this_tick, width):
            uncovered.append(s)
    if strict and clusters:
        return "fail", f"{len(clusters)} cluster(s), {sum(c['count'] for c in clusters)} px differ (strict)", summaries
    if uncovered:
        return "fail", f"{len(uncovered)}/{len(clusters)} cluster(s) outside allowlist", summaries
    return "pass", f"{len(clusters)} cluster(s) all covered by allowlist", summaries


def compare_pair(dir_a, dir_b, mapname, mode, tick_files, entries, strict):
    """Compare all tick PNGs of one pair. Returns (ok, [(tick, verdict, detail, summaries)])."""
    rows = []
    ok = True
    for tick in tick_files:
        fn = f"{mapname}_{tick}.png"
        pa = os.path.join(dir_a, fn)
        pb = os.path.join(dir_b, fn)
        if not (os.path.isfile(pa) and os.path.isfile(pb)):
            missing = [d for d, p in ((dir_a, pa), (dir_b, pb)) if not os.path.isfile(p)]
            rows.append((tick, "fail", f"missing PNG: {', '.join(missing)}", []))
            ok = False
            continue
        es = entries_for_tick(entries, mode, tick) if not strict else []
        verdict, detail, summaries = compare_tick(pa, pb, es, strict)
        rows.append((tick, verdict, detail, summaries))
        if verdict == "fail":
            ok = False
    return ok, rows


# --------------------------------------------------------------------------
# engine launches
# --------------------------------------------------------------------------

PIN_TEMPLATE = """\
// generated by abtest.py for {combo} (path {path})
set vid_vsync 0
set vid_maxfps 0
set cl_capfps 0
set vid_fixgamma 0
set vid_contrast 1
set vid_saturation 1
set vid_preferbackend {backend}
set gl_uselevelmesh {cvar}
set language english
set uiscale 1
set screenshot_quiet 1
set r_ab_capture {ticks}:{outdir}
"""


def launch(engine, build, map_entry, mode, backend, cvar_value, ticks, outdir,
           timeout, workdir):
    """One fresh-level-load capture launch. Returns (ok, detail, rc, elapsed)."""
    try:
        os.makedirs(outdir, exist_ok=True)
        pins = os.path.join(workdir, "run.pins")
        cfg = os.path.join(workdir, "run.cfg")
        with open(cfg, "w") as f:
            f.write("// fresh per-run config (abtest.py)\n")
        with open(pins, "w") as f:
            f.write(PIN_TEMPLATE.format(
                combo=f"{map_entry['name']} {mode} {backend}",
                path=f"cvar{cvar_value}",
                backend=BACKEND_CVAR[backend],
                cvar=cvar_value,
                ticks=",".join(str(t) for t in ticks),
                outdir=outdir))
    except OSError as e:
        return False, f"cannot write run config: {e}", None, 0.0
    # -rngseed pins the static RNG seed (d_main.cpp FArg_rngseed) so the
    # per-launch random stream is identical across a pair: the Doomguy face
    # frame pick (M_Random) and the pickup's FloatBobPhase are seeded from it,
    # so without a fixed seed they differ run-to-run and pollute the diff.
    # NOTE: FARGs take a '-' prefix; a '+rngseed' would be parsed as a no-op
    # console command and the seed would stay random per launch.
    cmd = [engine,
           "-iwad", os.path.join("wads", map_entry["iwad"]),
           "-nomonsters",
           "-rngseed", "12345",
           "-config", cfg,
           "-exec", pins,
           "-width", "2560", "-height", "1440"]
    if map_entry.get("pwad"):
        cmd += ["-file", os.path.join("wads", map_entry["pwad"])]
    if mode == "static":
        # +map takes the map's lump name string (MAP01, E1M2, 20PAM); -warp
        # takes ints (e[m]) and would atoi("MAP01") to 0 -> "Could not find map MAP00".
        cmd += ["+map", map_entry["warp"]]
    else:
        # Absolute: the engine chdirs to its install dir at startup, so
        # relative paths would not resolve ("Unable to open demo").
        cmd += ["-playdemo", os.path.join(REPO_ROOT, "tools/abtest/demos/" + map_entry["demo"])]
    stdout_log = os.path.join(workdir, "stdout.log")
    t0 = time.monotonic()
    try:
        with open(stdout_log, "wb") as out:
            proc = subprocess.run(cmd, cwd=build, stdout=out, stderr=subprocess.STDOUT,
                                  timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, f"timeout after {timeout}s", None, time.monotonic() - t0
    elapsed = time.monotonic() - t0
    try:
        with open(stdout_log, errors="replace") as f:
            stdout = f.read()
    except OSError:
        stdout = ""
    # reality checks (mirrors run_matrix.sh): the right backend must be
    # selected, and no software-GL / fallback may be in play.
    if "Can't find map" in stdout:
        return False, "map not found (check manifest warp name for this WAD)", proc.returncode, elapsed
    if BACKEND_LINE[backend] not in stdout:
        return False, f"stdout lacks {BACKEND_LINE[backend]!r}", proc.returncode, elapsed
    if "llvmpipe" in stdout:
        return False, "llvmpipe in stdout (software GL — run unsandboxed)", proc.returncode, elapsed
    if backend == "vulkan" and "Initialization of Vulkan failed" in stdout:
        return False, "vulkan init failed (fell back to GL)", proc.returncode, elapsed
    if proc.returncode != 0:
        extra = ""
        if "ABCAPTURE incomplete" in stdout:
            extra = " (ABCAPTURE incomplete: level ended with ticks unmet)"
        return False, f"exit {proc.returncode}{extra}", proc.returncode, elapsed
    if "ABCAPTURE done" not in stdout:
        return False, "stdout lacks 'ABCAPTURE done'", proc.returncode, elapsed
    return True, f"exit 0, {elapsed:.1f}s", proc.returncode, elapsed


def verify_pngs(outdir, mapname, ticks):
    missing = [t for t in ticks if not os.path.isfile(os.path.join(outdir, f"{mapname}_{t}.png"))]
    return missing


# --------------------------------------------------------------------------
# subcommands
# --------------------------------------------------------------------------

def die(msg) -> NoReturn:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(2)


def select_combos(manifest, args):
    """Yield (map_entry, mode, backend) per run request."""
    maps = manifest["maps"]
    if args.map:
        maps = [m for m in maps if m["name"] == args.map]
        if not maps:
            die(f"no map named {args.map!r} in manifest")
    for mp in maps:
        for mode in (MODES if not args.mode else [args.mode]):
            for backend in (BACKENDS if not args.backend else [args.backend]):
                if args.only and args.only not in mp["name"]:
                    continue
                yield mp, mode, backend


def run_one_pair(runs, manifest, allowlists, args, mp, mode, backend,
                 cvar_a, cvar_b, strict, label):
    combo = f"{mp['name']}_{mode}_{backend}"
    ticks = static_tick_list(manifest, mp) if mode == "static" else demo_tick_list(manifest)
    tick_files = ticks
    dir_a = os.path.join(runs, combo, "a")
    dir_b = os.path.join(runs, combo, "b")
    png_a = os.path.join(dir_a, "png")
    png_b = os.path.join(dir_b, "png")
    for d in (dir_a, dir_b):
        try:
            shutil.rmtree(d, ignore_errors=True)
            os.makedirs(d, exist_ok=True)
        except OSError as e:
            print(f"   cannot prepare {d}: {e}")
            return False, [{"check": "setup", "ok": False, "detail": str(e)}]
    print(f"== {label}: {combo}  path A cvar={cvar_a}  path B cvar={cvar_b}  "
          f"({len(ticks)} ticks)")
    entries, err = ([], None)
    if not strict:
        entries, err = load_allowlist(allowlists, mp["name"])
        if err:
            print(f"   allowlist: FAIL {err}")
            return False, [{"check": "allowlist", "ok": False, "detail": err}]
    ok, detail, rc, elapsed = launch(args.engine, args.build, mp, mode, backend,
                                     cvar_a, ticks, png_a, args.timeout, dir_a)
    print(f"   A: {detail}")
    if ok:
        missing = verify_pngs(png_a, mp["warp"], tick_files)
        if missing:
            ok = False
            detail = f"missing {len(missing)} PNGs: {missing[:5]}{'…' if len(missing) > 5 else ''}"
            print(f"   A: FAIL {detail}")
    row_a = {"path": "A", "ok": ok, "detail": detail, "rc": rc, "wall_s": round(elapsed, 1)}
    ok2, detail2, rc2, elapsed2 = launch(args.engine, args.build, mp, mode, backend,
                                         cvar_b, ticks, png_b, args.timeout, dir_b)
    print(f"   B: {detail2}")
    if ok2:
        missing = verify_pngs(png_b, mp["warp"], tick_files)
        if missing:
            ok2 = False
            detail2 = f"missing {len(missing)} PNGs: {missing[:5]}{'…' if len(missing) > 5 else ''}"
            print(f"   B: FAIL {detail2}")
    row_b = {"path": "B", "ok": ok2, "detail": detail2, "rc": rc2, "wall_s": round(elapsed2, 1)}
    checks = [row_a, row_b]
    if not (ok and ok2):
        return False, checks
    compare_ok, rows = compare_pair(png_a, png_b, mp["warp"], mode, tick_files, entries, strict)
    n_ident = sum(1 for r in rows if r[1] == "identical")
    n_pass = sum(1 for r in rows if r[1] == "pass")
    n_fail = sum(1 for r in rows if r[1] == "fail")
    summary = (f"ticks: {n_ident} byte-identical, {n_pass} zero-diff, "
               f"{n_fail} fail (of {len(rows)})")
    print(f"   compare: {summary}")
    for tick, verdict, det, sums in rows:
        if verdict == "fail":
            print(f"     FAIL tick {tick}: {det}")
            for s in sums:
                print(f"       cluster bbox={s['bbox']} px={s['count']} "
                      f"center=({s['cx']},{s['cy']}) r={s['radius']}")
    checks.append({"check": "compare", "ok": compare_ok, "detail": summary,
                   "rows": [(t, v, d) for t, v, d, _ in rows]})
    return compare_ok, checks


def cmd_run(args):
    manifest = load_manifest(args.manifest)
    if not os.path.isfile(args.engine):
        die(f"engine not found: {args.engine} (build first, or pass --engine)")
    w, h = manifest["resolution"]
    if (w, h) != (2560, 1440):
        print(f"note: manifest resolution is {w}x{h}, but the engine is launched at 2560x1440")
    runs = args.runs or DEFAULT_RUNS
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    runs = os.path.join(runs, f"{stamp}-{'det' if args.determinism else 'ab'}")
    try:
        os.makedirs(runs, exist_ok=True)
    except OSError as e:
        die(f"cannot create runs dir {runs}: {e}")
    print(f"run root: {runs}")

    if args.determinism:
        mapname = args.map or manifest.get("determinism_map", "DOOM2_MAP01")
        combos = [c for c in select_combos(manifest, args) if c[0]["name"] == mapname]
        if not combos:
            die(f"map {mapname!r} not selectable")
        results = []
        for mp, mode, backend in combos:
            ok, checks = run_one_pair(runs, manifest, args.allowlists, args, mp, mode,
                                      backend, 0, 0, strict=True,
                                      label="determinism (classic vs classic)")
            results.append({"combo": f"{mp['name']}_{mode}_{backend}", "ok": ok,
                            "checks": checks})
            print(f"== {mp['name']}_{mode}_{backend}: {'PASS' if ok else 'FAIL'}\n")
    else:
        results = []
        for mp, mode, backend in select_combos(manifest, args):
            ok, checks = run_one_pair(runs, manifest, args.allowlists, args, mp, mode,
                                      backend, 0, 1, strict=False, label="A/B")
            results.append({"combo": f"{mp['name']}_{mode}_{backend}", "ok": ok,
                            "checks": checks})
            print(f"== {mp['name']}_{mode}_{backend}: {'PASS' if ok else 'FAIL'}\n")

    try:
        with open(os.path.join(runs, "results.json"), "w") as f:
            json.dump({"manifest": args.manifest, "timestamp": stamp, "results": results},
                      f, indent=2)
    except OSError as e:
        print(f"warning: cannot write results.json: {e}", file=sys.stderr)
    n_ok = sum(1 for r in results if r["ok"])
    print(f"----\n{len(results)} combos: {n_ok} pass, {len(results) - n_ok} fail "
          f"(results: {os.path.join(runs, 'results.json')})")
    sys.exit(0 if n_ok == len(results) and results else 1)


def resolve_dirs(a, b):
    """Accept either pair dirs (containing the PNGs) or dirs holding png/."""
    def fix(d):
        d = os.path.abspath(d)
        if os.path.isdir(os.path.join(d, "png")):
            return os.path.join(d, "png")
        return d
    return fix(a), fix(b)


def cmd_diff(args):
    dir_a, dir_b = resolve_dirs(args.dir_a, args.dir_b)
    if not args.map:
        die("--map is required (PNG names carry the level map name)")
    manifest = load_manifest(args.manifest)
    mp = next((m for m in manifest["maps"] if m["name"] == args.map), None)
    if not mp:
        die(f"no map named {args.map!r} in manifest")
    mode = args.mode or "static"
    ticks = static_tick_list(manifest, mp) if mode == "static" else demo_tick_list(manifest)
    entries, err = load_allowlist(args.allowlists, args.map)
    if err:
        die(err)
    strict = args.strict
    ok, rows = compare_pair(dir_a, dir_b, mp["warp"], mode, ticks, entries, strict)
    for tick, verdict, detail, sums in rows:
        if verdict != "identical":
            print(f"tick {tick}: {verdict} — {detail}")
            for s in sums:
                print(f"   cluster bbox={s['bbox']} px={s['count']} "
                      f"center=({s['cx']},{s['cy']}) r={s['radius']}")
    print(f"{'PASS' if ok else 'FAIL'}: {args.map} {mode} ({dir_a} vs {dir_b})")
    sys.exit(0 if ok else 1)


def cmd_record(args):
    dir_a, dir_b = resolve_dirs(args.dir_a, args.dir_b)
    manifest = load_manifest(args.manifest)
    mp = next((m for m in manifest["maps"] if m["name"] == args.map), None)
    if not mp:
        die(f"no map named {args.map!r} in manifest")
    ticks = static_tick_list(manifest, mp) if args.mode == "static" else demo_tick_list(manifest)
    _, rows = compare_pair(dir_a, dir_b, mp["warp"], args.mode, ticks, [], strict=False)
    pending = os.path.join(args.allowlists, "pending", f"{args.map}.json")
    new_entries = []
    for tick, verdict, _detail, sums in rows:
        if verdict == "identical" or not sums:
            continue
        for s in sums:
            new_entries.append({
                "mode": args.mode,
                "tick": tick,
                "cx": s["cx"],
                "cy": s["cy"],
                "radius": s["radius"],
                "reason": "PENDING human review — z-fighting? (ADR 0004: z-fighting only; "
                          "fix real bugs, then promote to allowlists/<map>.json)",
            })
    if not new_entries:
        print("no diff clusters found — nothing to record")
        sys.exit(0)
    try:
        os.makedirs(os.path.dirname(pending), exist_ok=True)
    except OSError as e:
        die(f"cannot create pending dir: {e}")
    existing = []
    if os.path.isfile(pending):
        try:
            with open(pending) as f:
                existing = json.load(f).get("entries", [])
        except (OSError, ValueError) as e:
            die(f"{pending}: unreadable ({e})")
    # replace prior pending entries for the same (mode, tick); keep others
    keys = {(e["mode"], e["tick"]) for e in new_entries}
    existing = [e for e in existing if (e["mode"], e["tick"]) not in keys]
    doc = {"format": "ab-allowlist/1", "map": args.map, "frozen": False,
           "note": "STAGING — review every entry (z-fighting only, ADR 0004), write a "
                  "concrete reason, then promote to allowlists/<map>.json (frozen). "
                  "Never commit this file.",
           "entries": existing + new_entries}
    try:
        with open(pending, "w") as f:
            json.dump(doc, f, indent=2)
    except OSError as e:
        die(f"cannot write {pending}: {e}")
    print(f"recorded {len(new_entries)} cluster(s) to {pending}")
    print("review each entry, then promote the true z-fights to "
          f"{args.allowlists}/{args.map}.json")


def cmd_validate(args):
    manifest = load_manifest(args.manifest)
    problems = []
    w, h = manifest["resolution"]
    if (w, h) != (2560, 1440):
        problems.append(f"resolution is {w}x{h}, expected 2560x1440 (pinned)")
    n_maps = len(manifest["maps"])
    if n_maps != 26:
        problems.append(f"expected 26 maps, found {n_maps}")
    # demo files
    for mp in manifest["maps"]:
        p = os.path.join(KIT, "demos", mp["demo"])
        if not os.path.isfile(p):
            problems.append(f"missing demo: {p}")
    # tick lists
    dt = demo_tick_list(manifest)
    if len(dt) < 2:
        problems.append("demo tick list too small")
    for mp in manifest["maps"]:
        st = static_tick_list(manifest, mp)
        if not st:
            problems.append(f"{mp['name']}: empty static tick list")
    # allowlists
    try:
        listing = sorted(os.listdir(args.allowlists))
    except OSError as e:
        die(f"cannot list allowlists dir {args.allowlists}: {e}")
    for name in listing:
        if not name.endswith(".json") or name.startswith("pending"):
            continue
        entries, err = load_allowlist(args.allowlists, name[:-5])
        if err:
            problems.append(err)
            continue
        known = {m["name"] for m in manifest["maps"]}
        if name[:-5] not in known:
            problems.append(f"allowlist for unknown map: {name}")
        for i, e in enumerate(entries or []):
            if "z-fight" not in e["reason"].lower() and \
               "z fight" not in e["reason"].lower() and \
               "pending" not in e["reason"].lower():
                problems.append(f"{name}: entry {i} reason does not state z-fighting: "
                                f"{e['reason']!r}")
    # pillow sanity
    Image.new("L", (2, 2))
    if problems:
        print(f"FAIL: {len(problems)} problem(s):")
        for p in problems:
            print(f"  - {p}")
        sys.exit(1)
    print(f"PASS: manifest OK ({n_maps} maps, {len(dt)} demo ticks), "
          f"allowlists OK, Pillow OK")
    sys.exit(0)


def main():
    ap = argparse.ArgumentParser(description="levelmesh A/B capture + pixel-diff driver (ticket 01)",
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", help="path to uzdoom (default: build/uzdoom)")
    ap.add_argument("--build", default=os.path.join(HERE, "..", "build"),
                    help="build dir (default: <repo>/build)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    pr = sub.add_parser("run", help="launch capture pairs and compare")
    pr.add_argument("--determinism", action="store_true",
                    help="classic-vs-classic determinism run (cvar 0 both paths, "
                         "strict zero diff + byte-identical)")
    pr.add_argument("--map", help="run only this manifest map name")
    pr.add_argument("--only", help="run only maps whose name contains this")
    pr.add_argument("--mode", choices=MODES)
    pr.add_argument("--backend", choices=BACKENDS)
    pr.add_argument("--manifest", default=DEFAULT_MANIFEST)
    pr.add_argument("--allowlists", default=DEFAULT_ALLOWLISTS)
    pr.add_argument("--timeout", type=int, default=600, help="per-launch timeout (s)")
    pr.add_argument("--runs", help="runs root (default: tools/abtest/runs)")
    pr.set_defaults(fn=cmd_run)

    pd = sub.add_parser("diff", help="compare two existing capture dirs")
    pd.add_argument("dir_a")
    pd.add_argument("dir_b")
    pd.add_argument("--map", required=True)
    pd.add_argument("--mode", choices=MODES)
    pd.add_argument("--manifest", default=DEFAULT_MANIFEST)
    pd.add_argument("--allowlists", default=DEFAULT_ALLOWLISTS)
    pd.add_argument("--strict", action="store_true",
                    help="any pixel diff fails (no allowlist consulted)")
    pd.set_defaults(fn=cmd_diff)

    prec = sub.add_parser("record", help="stage diff clusters as pending allowlist entries")
    prec.add_argument("map")
    prec.add_argument("mode", choices=MODES)
    prec.add_argument("backend", choices=BACKENDS)
    prec.add_argument("dir_a")
    prec.add_argument("dir_b")
    prec.add_argument("--manifest", default=DEFAULT_MANIFEST)
    prec.add_argument("--allowlists", default=DEFAULT_ALLOWLISTS)
    prec.set_defaults(fn=cmd_record)

    pv = sub.add_parser("validate", help="validate manifest + allowlists")
    pv.add_argument("--manifest", default=DEFAULT_MANIFEST)
    pv.add_argument("--allowlists", default=DEFAULT_ALLOWLISTS)
    pv.set_defaults(fn=cmd_validate)

    args = ap.parse_args()
    args.build = os.path.abspath(args.build)
    if getattr(args, "engine", None) is None:
        args.engine = os.path.join(args.build, "uzdoom")
    args.fn(args)


if __name__ == "__main__":
    main()
