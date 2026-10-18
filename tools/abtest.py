#!/usr/bin/env python3
"""
abtest.py — levelmesh A/B capture + pixel-diff driver (v2).

Effort: .scratch/levelmesh-impl/map.md (Acceptance policy);
policy: docs/adr/0002-delta-class-acceptance.md; ticket 03.

Drives the engine's always-compiled A/B capture mode (r_ab_capture,
src/d_abcapture.cpp: synthetic clock, one sim tic per rendered frame) and
compares the composed frames of two fresh-level-load launches per
map x mode x backend: path A gl_uselevelmesh 0 (classic) vs path B
gl_uselevelmesh 1 (levelmesh). The levelmesh is Vulkan-only, so the
default backend is Vulkan (manifest "ab_backends"); --backend gl33 is kept
for classic-only checks.

Comparator (per tick PNG pair):
  - a pixel differs if its max per-channel |A-B| exceeds the manifest
    tolerance.channel;
  - differing pixels form 4-connected clusters; clusters smaller than
    tolerance.min_cluster are within tolerance;
  - every surviving cluster must be covered by an allowlist entry of a
    delta class (DC1..DC5, ADR 0002), else the tick fails. Failing
    clusters are reported with bbox, pixel count, max delta and a crop +
    4x-amplified diff image in <run>/<combo>/report/.

Game state (--gamestate): the engine also dumps <map>_<tick>.gstate
(per-subsector SSECMF_DRAWN, per-line ML_MAPPED); the two paths are
set-diffed per tick. Differences fail unless covered by a DC5 "gstate"
allowlist entry.

Allowlists (allowlists/<map>.json, format ab-allowlist/2) are human
curated: `record` only writes candidates to allowlists/pending/<map>.json
with class UNREVIEWED; a human sets class + reason in the pending file and
runs `promote --reviewer NAME`, which moves only reviewed entries.

Subcommands:
  run        [--determinism] [--map N] [--only P] [--mode M] [--backend B]
             [--gamestate] [--fullbright] [--runs DIR] [--timeout S]
             [--set-a 'CVAR VALUE']... [--set-b 'CVAR VALUE']...
  calibrate  [--map N] [--mode M] [--repeats K]   classic-vs-classic stats
  diff       DIRA DIRB --map N [--mode M] [--strict]
  statediff  DIRA DIRB --map N [--mode M]
  record     MAP MODE DIRA DIRB          stage candidate entries
  promote    MAP --reviewer NAME         move reviewed candidates
  validate                               manifest + allowlists

Requirements: Python 3, Pillow. WADs in build/wads/, engine at
build/uzdoom (--engine). Run unsandboxed on a real GPU with a display.
Exit: 0 all pass, 1 any check failed, 2 usage/setup error.
"""

import argparse
import datetime
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
CLASSES = ("DC1", "DC2", "DC3", "DC4", "DC5")
CLASS_NAMES = {
    "DC1": "z-fight",
    "DC2": "float-z rounding",
    "DC3": "feedback latency",
    "DC4": "dynamic-light selection",
    "DC5": "visibility precision (game state)",
}
UNREVIEWED = "UNREVIEWED"


def die(msg) -> NoReturn:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(2)


# --------------------------------------------------------------------------
# manifest
# --------------------------------------------------------------------------

def check_demo_ticks(dt, where):
    for key in ("step", "start", "last"):
        if not isinstance(dt.get(key), int) or dt[key] <= 0:
            die(f"manifest: {where}.{key} must be a positive int")
    for r in dt.get("dense", []):
        if not (isinstance(r, list) and len(r) == 2 and all(isinstance(v, int) and v > 0 for v in r) and r[0] <= r[1]):
            die(f"manifest: {where}.dense entries must be [first, last] positive tic ranges")


def load_manifest(path):
    if not os.path.isfile(path):
        die(f"manifest not found: {path}")
    try:
        with open(path) as f:
            m = json.load(f)
    except (OSError, ValueError) as e:
        die(f"manifest {path}: unreadable ({e})")
    if m.get("format") != "ab-manifest/2":
        die(f"manifest {path}: format must be ab-manifest/2, got {m.get('format')!r}")
    for key in ("resolution", "backends", "ab_backends", "demo_dir", "tolerance",
                "static_ticks_default", "demo_ticks", "maps"):
        if key not in m:
            die(f"manifest {path}: missing key {key!r}")
    w, h = m["resolution"]
    if not (isinstance(w, int) and isinstance(h, int) and w > 0 and h > 0):
        die(f"manifest: bad resolution {m['resolution']!r}")
    for b in BACKENDS:
        if not isinstance(m["backends"].get(b), int):
            die(f"manifest: backends must map {BACKENDS} to ints")
    if not m["ab_backends"] or any(b not in BACKENDS for b in m["ab_backends"]):
        die(f"manifest: ab_backends must be a non-empty subset of {BACKENDS}")
    tol = m["tolerance"]
    for key in ("channel", "min_cluster"):
        if not isinstance(tol.get(key), int) or tol[key] < 0:
            die(f"manifest: tolerance.{key} must be a non-negative int")
    check_demo_ticks(m["demo_ticks"], "demo_ticks")
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
        if "demo_ticks" in mp:
            check_demo_ticks(mp["demo_ticks"], f"{mp['name']} demo_ticks")
        st = mp.get("static_ticks")
        if st is not None and (not isinstance(st, list) or not st):
            die(f"manifest: {mp['name']} static_ticks must be null or a non-empty list")
    return m


def static_tick_list(manifest, mp):
    ticks = mp["static_ticks"] if mp.get("static_ticks") is not None \
        else manifest["static_ticks_default"]
    ticks = sorted(dict.fromkeys(int(t) for t in ticks))
    if any(t <= 0 for t in ticks):
        die(f"map {mp['name']}: ticks must be positive")
    return ticks


def demo_tick_list(manifest, mp):
    """demo_ticks (start / step / last), a map's own demo_ticks replacing the
    manifest's; optional "dense": [[first, last], ...] adds every tic of
    those ranges (latency probes)."""
    dt = mp.get("demo_ticks") or manifest["demo_ticks"]
    ticks = set(range(dt["start"], dt["last"] + 1, dt["step"]))
    ticks.add(dt["last"])
    for first, last in dt.get("dense", []):
        ticks.update(range(first, min(last, dt["last"]) + 1))
    return sorted(ticks)


def tick_list(manifest, mp, mode):
    return static_tick_list(manifest, mp) if mode == "static" else demo_tick_list(manifest, mp)


def find_map(manifest, name):
    mp = next((m for m in manifest["maps"] if m["name"] == name), None)
    if not mp:
        die(f"no map named {name!r} in manifest")
    return mp


# --------------------------------------------------------------------------
# allowlists (ab-allowlist/2)
# --------------------------------------------------------------------------

def _check_entry(e, i, where, pending):
    """Return an error string or None."""
    kind = e.get("kind", "pixels")
    if kind not in ("pixels", "gstate"):
        return f"{where}: entry {i} bad kind {kind!r}"
    if e.get("mode") not in MODES:
        return f"{where}: entry {i} bad mode {e.get('mode')!r}"
    if e.get("tick") is not None and not isinstance(e.get("tick"), int):
        return f"{where}: entry {i} tick must be int or null"
    cls = e.get("class")
    if cls not in CLASSES and not (pending and cls == UNREVIEWED):
        return f"{where}: entry {i} unknown class {cls!r} (allowed: {', '.join(CLASSES)})"
    if kind == "gstate":
        if cls not in (UNREVIEWED, "DC5"):
            return f"{where}: entry {i} gstate entries must be class DC5"
        for key in ("subsectors", "lines"):
            v = e.get(key)
            if not isinstance(v, list) or any(not isinstance(x, int) for x in v):
                return f"{where}: entry {i} {key} must be a list of ints"
    else:
        rect = e.get("rect")
        if not (isinstance(rect, list) and len(rect) == 4 and all(isinstance(v, int) for v in rect)
                and rect[0] <= rect[2] and rect[1] <= rect[3]):
            return f"{where}: entry {i} rect must be [x0, y0, x1, y1] ints"
        for key in ("cx", "cy", "radius"):
            if not isinstance(e.get(key), (int, float)):
                return f"{where}: entry {i} {key} must be numeric"
    if not isinstance(e.get("reason"), str) or not e["reason"].strip():
        return f"{where}: entry {i} reason must be a non-empty string"
    if not pending:
        if e["reason"].upper().startswith("PENDING"):
            return f"{where}: entry {i} reason is still PENDING"
        for key in ("reviewer", "date"):
            if not isinstance(e.get(key), str) or not e[key].strip():
                return f"{where}: entry {i} missing {key}"
    return None


def load_allowlist(dirpath, mapname, pending=False):
    """Return (entries, error)."""
    path = os.path.join(dirpath, "pending" if pending else "", f"{mapname}.json")
    if not os.path.isfile(path):
        return [], None
    try:
        with open(path) as f:
            doc = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        return None, f"{path}: unreadable ({e})"
    if doc.get("format") != "ab-allowlist/2":
        return None, f"{path}: format must be ab-allowlist/2, got {doc.get('format')!r}"
    if doc.get("map") != mapname:
        return None, f"{path}: map field {doc.get('map')!r} != {mapname}"
    entries = doc.get("entries", [])
    for i, e in enumerate(entries):
        err = _check_entry(e, i, path, pending)
        if err:
            return None, err
    return entries, None


def entries_for(entries, mode, tick, kind):
    return [e for e in entries
            if e.get("kind", "pixels") == kind and e["mode"] == mode
            and (e["tick"] is None or e["tick"] == tick)]


# --------------------------------------------------------------------------
# comparator
# --------------------------------------------------------------------------

def extract_clusters(mask, width, height, max_cluster_pixels=1_000_000):
    """4-connected components of a 0/255 'L' mask. Returns list of dicts with
    bbox, count, centroid, radius and the flat pixel indices."""
    data = mask
    n = width * height
    visited = bytearray(n)
    clusters = []
    idx = data.find(b"\xff") if isinstance(data, bytes) else 0
    if idx < 0:
        return clusters
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
                x0 = min(x0, x)
                x1 = max(x1, x)
                y0 = min(y0, y)
                y1 = max(y1, y)
                for k, ok in ((j - 1, x > 0), (j + 1, x + 1 < width),
                              (j - width, y > 0), (j + width, y + 1 < height)):
                    if ok and data[k] and not visited[k]:
                        visited[k] = 1
                        stack.append(k)
                if len(px) > max_cluster_pixels:
                    raise RuntimeError(f"cluster exceeds {max_cluster_pixels} px")
            cx = sum(p % width for p in px) / len(px)
            cy = sum(p // width for p in px) / len(px)
            r2 = max(((p % width - cx) ** 2 + (p // width - cy) ** 2) for p in px)
            clusters.append({"bbox": [x0, y0, x1, y1], "count": len(px),
                             "cx": round(cx, 2), "cy": round(cy, 2),
                             "radius": round(r2 ** 0.5, 2), "_pixels": px})
        idx += 1
    return clusters


def cluster_covered(cl, entries, width):
    """Covered iff the cluster bbox lies inside an entry's rect, or every
    pixel lies inside an entry's circle."""
    x0, y0, x1, y1 = cl["bbox"]
    for e in entries:
        r = e["rect"]
        if r[0] <= x0 and r[1] <= y0 and x1 <= r[2] and y1 <= r[3]:
            return e
        r2 = float(e["radius"]) ** 2
        ex, ey = float(e["cx"]), float(e["cy"])
        if all(((p % width) - ex) ** 2 + ((p // width) - ey) ** 2 <= r2 for p in cl["_pixels"]):
            return e
    return None


def delta_image(path_a, path_b):
    """Return (im_a, im_b, maxdelta 'L' image) or raise."""
    im_a = Image.open(path_a).convert("RGB")
    im_b = Image.open(path_b).convert("RGB")
    if im_a.size != im_b.size:
        raise ValueError(f"size mismatch {im_a.size} vs {im_b.size}")
    r, g, b = ImageChops.difference(im_a, im_b).split()
    return im_a, im_b, ImageChops.lighter(ImageChops.lighter(r, g), b)


def write_report(report_dir, tick, idx, cl, im_a, im_b, delta):
    """Crop of A | B | 4x-amplified diff around a cluster."""
    os.makedirs(report_dir, exist_ok=True)
    x0, y0, x1, y1 = cl["bbox"]
    pad = 24
    box = (max(0, x0 - pad), max(0, y0 - pad),
           min(im_a.width, x1 + pad + 1), min(im_a.height, y1 + pad + 1))
    ca, cb = im_a.crop(box), im_b.crop(box)
    cd = delta.crop(box).point(lambda v: min(255, v * 4)).convert("RGB")
    w, h = ca.size
    sheet = Image.new("RGB", (w * 3, h))
    for i, im in enumerate((ca, cb, cd)):
        sheet.paste(im, (i * w, 0))
    name = f"tick{tick}_c{idx}.png"
    sheet.save(os.path.join(report_dir, name))
    return name


def compare_tick(path_a, path_b, entries, tol, report_dir, tick):
    """Returns (verdict, detail, cluster summaries).
    verdict: identical | pass | fail. tol = (channel, min_cluster)."""
    try:
        with open(path_a, "rb") as fa, open(path_b, "rb") as fb:
            ba, bb = fa.read(), fb.read()
    except OSError as e:
        return "fail", f"unreadable PNG ({e})", []
    if hashlib.sha256(ba).digest() == hashlib.sha256(bb).digest():
        return "identical", "byte-identical", []
    try:
        im_a, im_b, delta = delta_image(path_a, path_b)
    except Exception as e:
        return "fail", f"unreadable image ({e.__class__.__name__}: {e})", []
    channel, min_cluster = tol
    if delta.getextrema()[1] <= channel:
        return "pass", f"max delta {delta.getextrema()[1]} <= tolerance {channel}", []
    mask = delta.point(lambda v: 255 if v > channel else 0)
    width, height = im_a.size
    try:
        clusters = extract_clusters(mask.tobytes(), width, height)
    except RuntimeError as e:
        return "fail", f"{e}; treated as fail", []
    sums = []
    failed = 0
    small = 0
    for i, cl in enumerate(clusters):
        if cl["count"] < min_cluster:
            small += 1
            continue
        cover = cluster_covered(cl, entries, width)
        x0, y0, x1, y1 = cl["bbox"]
        maxd = delta.crop((x0, y0, x1 + 1, y1 + 1)).getextrema()[1]
        s = {k: v for k, v in cl.items() if k != "_pixels"}
        s["max_delta"] = maxd
        s["covered_by"] = cover["class"] if cover else None
        if not cover:
            failed += 1
            if report_dir:
                s["image"] = write_report(report_dir, tick, i, cl, im_a, im_b, delta)
        sums.append(s)
    if failed:
        return "fail", f"{failed} cluster(s) outside allowlist ({len(sums)} over tolerance, {small} below min size)", sums
    return "pass", f"{len(sums)} cluster(s) covered, {small} below min size", sums


def read_gstate(path):
    try:
        with open(path) as f:
            lines = f.read().split("\n")
    except OSError:
        return None
    out = {}
    for ln in lines:
        if ln.startswith("S ") or ln.startswith("L "):
            hexs = ln[2:].strip()
            bits = set()
            for bi in range(0, len(hexs), 2):
                b = int(hexs[bi:bi + 2], 16)
                for k in range(8):
                    if b & (1 << k):
                        bits.add(bi // 2 * 8 + k)
            out[ln[0]] = bits
    return out if "S" in out and "L" in out else None


def compare_gstate(path_a, path_b, entries):
    """Set-diff two .gstate dumps. Returns (verdict, detail, summary)."""
    ga, gb = read_gstate(path_a), read_gstate(path_b)
    if ga is None or gb is None:
        return "fail", "missing/unreadable .gstate", {}
    ds = ga["S"] ^ gb["S"]
    dl = ga["L"] ^ gb["L"]
    cov_s = set().union(*(set(e["subsectors"]) for e in entries)) if entries else set()
    cov_l = set().union(*(set(e["lines"]) for e in entries)) if entries else set()
    us, ul = ds - cov_s, dl - cov_l
    summary = {"drawn_a": len(ga["S"]), "drawn_b": len(gb["S"]),
               "mapped_a": len(ga["L"]), "mapped_b": len(gb["L"]),
               "subsectors_only_a": sorted(ga["S"] - gb["S"]),
               "subsectors_only_b": sorted(gb["S"] - ga["S"]),
               "lines_only_a": sorted(ga["L"] - gb["L"]),
               "lines_only_b": sorted(gb["L"] - ga["L"])}
    if not ds and not dl:
        return "identical", f"{len(ga['S'])} subsectors / {len(ga['L'])} lines, identical", summary
    if us or ul:
        return "fail", f"{len(us)} subsector / {len(ul)} line diff(s) unclassified", summary
    return "pass", f"{len(ds)} subsector / {len(dl)} line diff(s), all DC5", summary


def compare_pair(dir_a, dir_b, mapname, mode, ticks, entries, tol, report_dir, gamestate):
    rows = []
    ok = True
    for tick in ticks:
        pa = os.path.join(dir_a, f"{mapname}_{tick}.png")
        pb = os.path.join(dir_b, f"{mapname}_{tick}.png")
        if not (os.path.isfile(pa) and os.path.isfile(pb)):
            rows.append((tick, "fail", "missing PNG", []))
            ok = False
            continue
        v, d, s = compare_tick(pa, pb, entries_for(entries, mode, tick, "pixels"), tol,
                               report_dir, tick)
        if gamestate:
            gv, gd, gs = compare_gstate(os.path.join(dir_a, f"{mapname}_{tick}.gstate"),
                                        os.path.join(dir_b, f"{mapname}_{tick}.gstate"),
                                        entries_for(entries, mode, tick, "gstate"))
            d += f"; gstate: {gd}"
            s = list(s) + [{"gstate": gs}]
            if gv == "fail":
                v = "fail"
            elif gv == "pass" and v == "identical":
                v = "pass"
        rows.append((tick, v, d, s))
        if v == "fail":
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
// Dynamic lights pinned off on BOTH paths: levelmesh dynlights are a
// separate clustered forward+ design (.scratch/levelmesh-impl/issues/
// 19-dynamic-lights.md); remove this pin when 19 lands.
set r_dynlights 0
set r_ab_fullbright {fullbright}
set r_ab_gamestate {gamestate}
// Notify lines off: the tour demos' tic-0 cheat messages tick through
// C_Ticker, which also runs for the real-time tics of level/demo startup
// before capture pacing applies, so their visibility at tic 30-90 varied
// run to run. They are not renderer output. (Midprints are tied to game
// tics and stay on.)
set show_messages 0
set language english
set uiscale 1
set screenshot_quiet 1
set r_ab_capture {ticks}:{outdir}
"""


def launch(args, manifest, mp, mode, backend, cvar_value, ticks, outdir, workdir, path_tag=""):
    """One fresh-level-load capture launch. Returns (ok, detail, elapsed)."""
    try:
        # M_ScreenShot does not create missing directories for an explicit
        # file name: the capture dir must exist before the engine starts.
        os.makedirs(outdir, exist_ok=True)
        pins = os.path.join(workdir, "run.pins")
        cfg = os.path.join(workdir, "run.cfg")
        with open(cfg, "w") as f:
            f.write("// fresh per-run config (abtest.py)\n")
        with open(pins, "w") as f:
            f.write(PIN_TEMPLATE.format(
                combo=f"{mp['name']} {mode} {backend}", path=f"cvar{cvar_value}",
                backend=BACKEND_CVAR[backend], cvar=cvar_value,
                fullbright=int(args.fullbright), gamestate=int(args.gamestate),
                ticks=",".join(str(t) for t in ticks), outdir=outdir))
            # Per-path pins (--set-a / --set-b) come last, so they override
            # the template, gl_uselevelmesh included.
            extra = (getattr(args, f"set_{path_tag}", None) or []) if path_tag in ("a", "b") else []
            for pin in extra:
                f.write(f"set {pin}\n")
    except OSError as e:
        return False, f"cannot write run config: {e}", 0.0
    w, h = manifest["resolution"]
    # -rngseed pins the static RNG seed (FARGs take '-'; '+rngseed' is a no-op)
    # so per-launch random streams (face frames, FloatBobPhase) match.
    # A manifest with "monsters": true keeps them (the gather kit's dormant monsters).
    cmd = [args.engine, "-iwad", os.path.join("wads", mp["iwad"])]
    if not manifest.get("monsters"):
        cmd.append("-nomonsters")
    cmd += ["-rngseed", "12345", "-config", cfg, "-exec", pins,
            "-width", str(w), "-height", str(h)]
    if mp.get("pwad"):
        cmd += ["-file", os.path.join("wads", mp["pwad"])]
    if mode == "static":
        # +map takes the lump name (MAP01, E1M2, 20PAM); -warp wants ints.
        cmd += ["+map", mp["warp"]]
    else:
        # Absolute: the engine chdirs to its install dir at startup.
        cmd += ["-playdemo", os.path.join(KIT, manifest["demo_dir"], mp["demo"])]
    stdout_log = os.path.join(workdir, "stdout.log")
    t0 = time.monotonic()
    try:
        with open(stdout_log, "wb") as out:
            proc = subprocess.run(cmd, cwd=args.build, stdout=out, stderr=subprocess.STDOUT,
                                  timeout=args.timeout)
    except subprocess.TimeoutExpired:
        return False, f"timeout after {args.timeout}s", time.monotonic() - t0
    elapsed = time.monotonic() - t0
    try:
        with open(stdout_log, errors="replace") as f:
            stdout = f.read()
    except OSError:
        stdout = ""
    if "Can't find map" in stdout:
        return False, "map not found (check manifest warp name)", elapsed
    if BACKEND_LINE[backend] not in stdout:
        return False, f"stdout lacks {BACKEND_LINE[backend]!r}", elapsed
    if "llvmpipe" in stdout:
        return False, "llvmpipe in stdout (software GL — run unsandboxed)", elapsed
    if backend == "vulkan" and "Initialization of Vulkan failed" in stdout:
        return False, "vulkan init failed (fell back to GL)", elapsed
    # A levelmesh that fails to build its shaders or scatter falls back to the
    # classic path, which would make a classic-vs-levelmesh pair trivially pass.
    for line in stdout.splitlines():
        if line.startswith("Levelmesh: ") and "failed" in line:
            return False, f"levelmesh fallback: {line[:160]}", elapsed
    if proc.returncode != 0:
        extra = " (ABCAPTURE incomplete)" if "ABCAPTURE incomplete" in stdout else ""
        return False, f"exit {proc.returncode}{extra}", elapsed
    if "ABCAPTURE done" not in stdout:
        return False, "stdout lacks 'ABCAPTURE done'", elapsed
    missing = [t for t in ticks
               if not os.path.isfile(os.path.join(outdir, f"{mp['warp']}_{t}.png"))]
    if missing:
        return False, f"missing {len(missing)} PNG(s): {missing[:5]}", elapsed
    return True, f"exit 0, {elapsed:.1f}s", elapsed


def capture_side(args, manifest, root, mp, mode, backend, cvar, ticks, tag):
    work = os.path.join(root, tag)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    png = os.path.join(work, "png")
    ok, detail, _ = launch(args, manifest, mp, mode, backend, cvar, ticks, png, work, tag)
    print(f"   {tag}: {detail}")
    return ok, png


# --------------------------------------------------------------------------
# subcommands
# --------------------------------------------------------------------------

def new_run_root(args, suffix):
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    # absolute: the engine chdirs to its install dir at startup
    root = os.path.abspath(os.path.join(args.runs or DEFAULT_RUNS, f"{stamp}-{suffix}"))
    os.makedirs(root, exist_ok=True)
    print(f"run root: {root}")
    return root, stamp


def select_combos(manifest, args, determinism):
    maps = manifest["maps"]
    if args.map:
        maps = [find_map(manifest, args.map)]
    elif determinism:
        maps = [find_map(manifest, manifest.get("determinism_map", "DOOM2_MAP01"))]
    backends = [args.backend] if args.backend else manifest["ab_backends"]
    for mp in maps:
        if args.only and args.only not in mp["name"]:
            continue
        for mode in ([args.mode] if args.mode else MODES):
            for backend in backends:
                yield mp, mode, backend


def print_rows(rows):
    for tick, verdict, det, sums in rows:
        if verdict != "fail":
            continue
        print(f"     FAIL tick {tick}: {det}")
        for s in sums:
            if "gstate" in s:
                g = s["gstate"]
                for key in ("subsectors_only_a", "subsectors_only_b", "lines_only_a", "lines_only_b"):
                    if g.get(key):
                        print(f"       gstate {key}: {g[key][:20]}{' …' if len(g[key]) > 20 else ''}")
            elif s.get("covered_by") is None:
                print(f"       cluster bbox={s['bbox']} px={s['count']} max_delta={s['max_delta']} "
                      f"center=({s['cx']},{s['cy']}) r={s['radius']} img={s.get('image')}")


def cmd_run(args):
    manifest = load_manifest(args.manifest)
    if not os.path.isfile(args.engine):
        die(f"engine not found: {args.engine}")
    root, stamp = new_run_root(args, "det" if args.determinism else "ab")
    tol_m = manifest["tolerance"]
    results = []
    for mp, mode, backend in select_combos(manifest, args, args.determinism):
        combo = f"{mp['name']}_{mode}_{backend}"
        croot = os.path.join(root, combo)
        ticks = tick_list(manifest, mp, mode)
        cvar_b = 0 if args.determinism else 1
        label = "determinism (classic vs classic)" if args.determinism else "A/B"
        print(f"== {label}: {combo}  A cvar=0  B cvar={cvar_b}  ({len(ticks)} ticks)")
        for tag in ("a", "b"):
            pins = getattr(args, f"set_{tag}") or []
            if pins:
                print(f"   {tag} pins: {'; '.join(pins)}")
        entries = []
        if not args.determinism:
            entries, err = load_allowlist(args.allowlists, mp["name"])
            if err:
                print(f"   allowlist: FAIL {err}")
                results.append({"combo": combo, "ok": False, "detail": err})
                continue
        ok_a, png_a = capture_side(args, manifest, croot, mp, mode, backend, 0, ticks, "a")
        ok_b, png_b = capture_side(args, manifest, croot, mp, mode, backend, cvar_b, ticks, "b")
        if not (ok_a and ok_b):
            results.append({"combo": combo, "ok": False, "detail": "launch failed"})
            print(f"== {combo}: FAIL\n")
            continue
        # Determinism is strict: no tolerance, every pixel counts.
        tol = (0, 1) if args.determinism else (tol_m["channel"], tol_m["min_cluster"])
        ok, rows = compare_pair(png_a, png_b, mp["warp"], mode, ticks, entries, tol,
                                os.path.join(croot, "report"), args.gamestate)
        counts = {v: sum(1 for r in rows if r[1] == v) for v in ("identical", "pass", "fail")}
        print(f"   compare: {counts['identical']} identical, {counts['pass']} pass, "
              f"{counts['fail']} fail (of {len(rows)})")
        print_rows(rows)
        results.append({"combo": combo, "ok": ok,
                        "rows": [{"tick": t, "verdict": v, "detail": d, "clusters": s}
                                 for t, v, d, s in rows]})
        print(f"== {combo}: {'PASS' if ok else 'FAIL'}\n")
    with open(os.path.join(root, "results.json"), "w") as f:
        json.dump({"manifest": args.manifest, "timestamp": stamp, "tolerance": tol_m,
                   "gamestate": args.gamestate, "fullbright": args.fullbright,
                   "set_a": args.set_a or [], "set_b": args.set_b or [],
                   "results": results}, f, indent=1)
    n_ok = sum(1 for r in results if r["ok"])
    print(f"----\n{len(results)} combos: {n_ok} pass, {len(results) - n_ok} fail "
          f"(results: {os.path.join(root, 'results.json')})")
    sys.exit(0 if results and n_ok == len(results) else 1)


def cmd_calibrate(args):
    """Classic-vs-classic statistics for choosing the tolerance:
    (1) Vulkan vs Vulkan across `repeats` separate launches (driver
    restarts), (2) GL33 vs Vulkan. Reports, per comparison, the per-pixel
    max-channel-delta histogram and the cluster sizes at each candidate
    channel tolerance."""
    manifest = load_manifest(args.manifest)
    mp = find_map(manifest, args.map or manifest.get("determinism_map", "DOOM2_MAP01"))
    mode = args.mode or "static"
    ticks = tick_list(manifest, mp, mode)
    root, stamp = new_run_root(args, "cal")
    caps = []
    for i in range(args.repeats):
        ok, png = capture_side(args, manifest, root, mp, mode, "vulkan", 0, ticks, f"vk{i}")
        if not ok:
            die("vulkan capture failed")
        caps.append(png)
    ok, gl = capture_side(args, manifest, root, mp, mode, "gl33", 0, ticks, "gl")
    if not ok:
        die("gl33 capture failed")
    pairs = [(f"vulkan#0 vs vulkan#{i}", caps[0], caps[i]) for i in range(1, len(caps))]
    pairs.append(("gl33 vs vulkan#0", gl, caps[0]))
    report = {"map": mp["name"], "mode": mode, "ticks": ticks, "comparisons": []}
    for name, da, db in pairs:
        hist = [0] * 256
        ident = 0
        cl_sizes = {c: [] for c in (0, 2, 4, 8, 16, 32)}
        for t in ticks:
            pa = os.path.join(da, f"{mp['warp']}_{t}.png")
            pb = os.path.join(db, f"{mp['warp']}_{t}.png")
            with open(pa, "rb") as fa, open(pb, "rb") as fb:
                if fa.read() == fb.read():
                    ident += 1
                    continue
            im_a, _, delta = delta_image(pa, pb)
            for v, c in enumerate(delta.histogram()):
                hist[v] += c
            for c in cl_sizes:
                if delta.getextrema()[1] <= c:
                    continue
                mask = delta.point(lambda v, c=c: 255 if v > c else 0)
                try:
                    cls = extract_clusters(mask.tobytes(), im_a.width, im_a.height, 200_000)
                    cl_sizes[c] += [x["count"] for x in cls]
                except RuntimeError:
                    cl_sizes[c].append(-1)  # whole-frame region
        nz = sum(hist[1:])
        maxd = max((v for v in range(256) if hist[v]), default=0)
        entry = {"pair": name, "identical_ticks": ident, "diff_pixels": nz, "max_delta": maxd,
                 "delta_hist": {str(v): hist[v] for v in range(1, 256) if hist[v]},
                 "clusters_by_channel_tol": {
                     str(c): {"count": len(s), "max_size": max(s) if s else 0,
                              "whole_frame": s.count(-1)} for c, s in cl_sizes.items()}}
        report["comparisons"].append(entry)
        print(f"{name}: {ident}/{len(ticks)} ticks byte-identical, {nz} differing px, "
              f"max delta {maxd}")
        for c, s in entry["clusters_by_channel_tol"].items():
            print(f"   channel tol {c:>2}: {s['count']} cluster(s), max {s['max_size']} px"
                  f"{', ' + str(s['whole_frame']) + ' whole-frame' if s['whole_frame'] else ''}")
    out = os.path.join(root, "calibration.json")
    with open(out, "w") as f:
        json.dump(report, f, indent=1)
    print(f"written {out}")


def resolve_dirs(a, b):
    def fix(d):
        d = os.path.abspath(d)
        return os.path.join(d, "png") if os.path.isdir(os.path.join(d, "png")) else d
    return fix(a), fix(b)


def cmd_diff(args):
    manifest = load_manifest(args.manifest)
    mp = find_map(manifest, args.map)
    mode = args.mode or "static"
    dir_a, dir_b = resolve_dirs(args.dir_a, args.dir_b)
    entries, err = load_allowlist(args.allowlists, args.map)
    if err:
        die(err)
    tol = (0, 1) if args.strict else (manifest["tolerance"]["channel"],
                                      manifest["tolerance"]["min_cluster"])
    report = os.path.join(os.path.dirname(dir_a), "report")
    ok, rows = compare_pair(dir_a, dir_b, mp["warp"], mode, tick_list(manifest, mp, mode),
                            [] if args.strict else entries, tol, report, args.gamestate)
    for tick, verdict, detail, _ in rows:
        if verdict != "identical":
            print(f"tick {tick}: {verdict} — {detail}")
    print_rows(rows)
    print(f"{'PASS' if ok else 'FAIL'}: {args.map} {mode}")
    sys.exit(0 if ok else 1)


def cmd_statediff(args):
    manifest = load_manifest(args.manifest)
    mp = find_map(manifest, args.map)
    mode = args.mode or "static"
    dir_a, dir_b = resolve_dirs(args.dir_a, args.dir_b)
    entries, err = load_allowlist(args.allowlists, args.map)
    if err:
        die(err)
    ok = True
    for tick in tick_list(manifest, mp, mode):
        v, d, s = compare_gstate(os.path.join(dir_a, f"{mp['warp']}_{tick}.gstate"),
                                 os.path.join(dir_b, f"{mp['warp']}_{tick}.gstate"),
                                 entries_for(entries, mode, tick, "gstate"))
        print(f"tick {tick}: {v} — {d}")
        if v == "fail":
            ok = False
            print_rows([(tick, v, d, [{"gstate": s}])])
    print(f"{'PASS' if ok else 'FAIL'}: {args.map} {mode} game state")
    sys.exit(0 if ok else 1)


def cmd_record(args):
    """Stage every failing cluster / game-state diff as an UNREVIEWED
    candidate in allowlists/pending/<map>.json. Never touches the allowlist."""
    manifest = load_manifest(args.manifest)
    mp = find_map(manifest, args.map)
    dir_a, dir_b = resolve_dirs(args.dir_a, args.dir_b)
    entries, err = load_allowlist(args.allowlists, args.map)
    if err:
        die(err)
    tol = (manifest["tolerance"]["channel"], manifest["tolerance"]["min_cluster"])
    _, rows = compare_pair(dir_a, dir_b, mp["warp"], args.mode,
                           tick_list(manifest, mp, args.mode), entries, tol, None,
                           args.gamestate)
    new = []
    for tick, verdict, _detail, sums in rows:
        if verdict != "fail":
            continue
        for s in sums:
            if "gstate" in s:
                g = s["gstate"]
                subs = g.get("subsectors_only_a", []) + g.get("subsectors_only_b", [])
                lns = g.get("lines_only_a", []) + g.get("lines_only_b", [])
                covered = entries_for(entries, args.mode, tick, "gstate")
                cov_s = {x for e in covered for x in e["subsectors"]}
                cov_l = {x for e in covered for x in e["lines"]}
                subs = [x for x in subs if x not in cov_s]
                lns = [x for x in lns if x not in cov_l]
                if not subs and not lns:
                    continue
                new.append({"kind": "gstate", "mode": args.mode, "tick": tick,
                            "class": UNREVIEWED, "subsectors": sorted(subs),
                            "lines": sorted(lns), "reason": "PENDING human review"})
            elif s.get("covered_by") is None:
                new.append({"kind": "pixels", "mode": args.mode, "tick": tick,
                            "class": UNREVIEWED, "rect": s["bbox"], "cx": s["cx"],
                            "cy": s["cy"], "radius": s["radius"],
                            "pixels": s["count"], "max_delta": s["max_delta"],
                            "reason": "PENDING human review"})
    if not new:
        print("nothing to record: no failing clusters / game-state diffs")
        return
    pending = os.path.join(args.allowlists, "pending", f"{args.map}.json")
    os.makedirs(os.path.dirname(pending), exist_ok=True)
    old, err = load_allowlist(args.allowlists, args.map, pending=True)
    if err:
        die(err)
    keys = {(e["mode"], e["tick"]) for e in new}
    old = [e for e in old if (e["mode"], e["tick"]) not in keys]
    with open(pending, "w") as f:
        json.dump({"format": "ab-allowlist/2", "map": args.map,
                   "note": "CANDIDATES - never commit. For each entry set class "
                           f"({', '.join(CLASSES)}) and a concrete reason, or delete it "
                           "(real bug: fix the engine). Then run "
                           f"`abtest.py promote {args.map} --reviewer NAME`.",
                   "entries": old + new}, f, indent=1)
    print(f"recorded {len(new)} candidate(s) to {pending}")


def cmd_promote(args):
    """Move reviewed candidates (class set, reason no longer PENDING) from
    pending/<map>.json into <map>.json, stamping reviewer + date."""
    pend, err = load_allowlist(args.allowlists, args.map, pending=True)
    if err:
        die(err)
    if not pend:
        die(f"no pending candidates for {args.map}")
    done, keep = [], []
    for e in pend:
        reviewed = e["class"] in CLASSES and not e["reason"].upper().startswith("PENDING")
        (done if reviewed else keep).append(e)
    if not done:
        die("no reviewed candidates: set class and reason in the pending file first")
    today = datetime.date.today().isoformat()
    for e in done:
        e["reviewer"] = args.reviewer
        e["date"] = today
    cur, err = load_allowlist(args.allowlists, args.map)
    if err:
        die(err)
    path = os.path.join(args.allowlists, f"{args.map}.json")
    with open(path, "w") as f:
        json.dump({"format": "ab-allowlist/2", "map": args.map, "entries": cur + done},
                  f, indent=1)
    ppath = os.path.join(args.allowlists, "pending", f"{args.map}.json")
    if keep:
        with open(ppath) as f:
            doc = json.load(f)
        doc["entries"] = keep
        with open(ppath, "w") as f:
            json.dump(doc, f, indent=1)
    else:
        os.remove(ppath)
    print(f"promoted {len(done)} entr{'y' if len(done) == 1 else 'ies'} to {path}; "
          f"{len(keep)} still pending")


def cmd_validate(args):
    manifest = load_manifest(args.manifest)
    problems = []
    for mp in manifest["maps"]:
        p = os.path.join(KIT, manifest["demo_dir"], mp["demo"])
        if not os.path.isfile(p):
            problems.append(f"missing demo: {p}")
        if not static_tick_list(manifest, mp):
            problems.append(f"{mp['name']}: empty static tick list")
    known = {m["name"] for m in manifest["maps"]}
    try:
        listing = sorted(os.listdir(args.allowlists))
    except OSError as e:
        die(f"cannot list {args.allowlists}: {e}")
    for name in listing:
        if not name.endswith(".json"):
            continue
        if name[:-5] not in known:
            problems.append(f"allowlist for unknown map: {name}")
        _, err = load_allowlist(args.allowlists, name[:-5])
        if err:
            problems.append(err)
    if problems:
        print(f"FAIL: {len(problems)} problem(s):")
        for p in problems:
            print(f"  - {p}")
        sys.exit(1)
    print(f"PASS: manifest OK ({len(manifest['maps'])} maps), allowlists OK")


def main():
    ap = argparse.ArgumentParser(description="levelmesh A/B capture + pixel-diff driver (v2)",
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", help="path to uzdoom (default: build/uzdoom)")
    ap.add_argument("--build", default=os.path.join(REPO_ROOT, "build"))
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(p, runs=False):
        p.add_argument("--manifest", default=DEFAULT_MANIFEST)
        p.add_argument("--allowlists", default=DEFAULT_ALLOWLISTS)
        if runs:
            p.add_argument("--timeout", type=int, default=600)
            p.add_argument("--runs", help="runs root (default: tools/abtest/runs)")
            p.add_argument("--fullbright", action="store_true",
                           help="set r_ab_fullbright 1 on both paths")
            p.add_argument("--gamestate", action="store_true",
                           help="dump + set-diff exposure state (r_ab_gamestate 1)")

    p = sub.add_parser("run", help="launch capture pairs and compare")
    p.add_argument("--set-a", action="append", metavar="'CVAR VALUE'",
                   help="extra cvar pin on path A only (repeatable; overrides the template, "
                        "e.g. 'gl_uselevelmesh 1' for a levelmesh-vs-levelmesh A/B)")
    p.add_argument("--set-b", action="append", metavar="'CVAR VALUE'",
                   help="extra cvar pin on path B only (repeatable)")
    p.add_argument("--determinism", action="store_true",
                   help="classic vs classic, strict (no tolerance, no allowlist)")
    p.add_argument("--map")
    p.add_argument("--only")
    p.add_argument("--mode", choices=MODES)
    p.add_argument("--backend", choices=BACKENDS)
    common(p, runs=True)
    p.set_defaults(fn=cmd_run)

    p = sub.add_parser("calibrate", help="classic-vs-classic tolerance statistics")
    p.add_argument("--map")
    p.add_argument("--mode", choices=MODES)
    p.add_argument("--repeats", type=int, default=3)
    common(p, runs=True)
    p.set_defaults(fn=cmd_calibrate)

    p = sub.add_parser("diff", help="compare two existing capture dirs")
    p.add_argument("dir_a")
    p.add_argument("dir_b")
    p.add_argument("--map", required=True)
    p.add_argument("--mode", choices=MODES)
    p.add_argument("--strict", action="store_true", help="no tolerance, no allowlist")
    p.add_argument("--gamestate", action="store_true")
    common(p)
    p.set_defaults(fn=cmd_diff)

    p = sub.add_parser("statediff", help="set-diff the .gstate dumps of two capture dirs")
    p.add_argument("dir_a")
    p.add_argument("dir_b")
    p.add_argument("--map", required=True)
    p.add_argument("--mode", choices=MODES)
    common(p)
    p.set_defaults(fn=cmd_statediff)

    p = sub.add_parser("record", help="stage failing clusters as pending candidates")
    p.add_argument("map")
    p.add_argument("mode", choices=MODES)
    p.add_argument("dir_a")
    p.add_argument("dir_b")
    p.add_argument("--gamestate", action="store_true")
    common(p)
    p.set_defaults(fn=cmd_record)

    p = sub.add_parser("promote", help="move reviewed candidates into the allowlist")
    p.add_argument("map")
    p.add_argument("--reviewer", required=True)
    common(p)
    p.set_defaults(fn=cmd_promote)

    p = sub.add_parser("validate", help="validate manifest + allowlists")
    common(p)
    p.set_defaults(fn=cmd_validate)

    args = ap.parse_args()
    args.build = os.path.abspath(args.build)
    if args.engine is None:
        args.engine = os.path.join(args.build, "uzdoom")
    args.fn(args)


if __name__ == "__main__":
    main()
