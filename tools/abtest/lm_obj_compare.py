#!/usr/bin/env python3
# lm_obj_compare.py — visual check of the levelmesh bake (ticket 06).
#
# Starts a map on Vulkan with gl_uselevelmesh 1 and lm_bake_objdir set and
# takes a classic r_ab_capture screenshot at --tick. Then
# CPU-rasterizes the baked OBJ from the same viewpoint (the hwrenderer
# projection: fovratio 4/3 on widescreen, pixelstretch 1.2; camera from the
# ABCAPTURE view line)
# and writes <out>: classic | bake (shaded) | classic with bake edges overlaid.
#
#   lm_obj_compare.py --iwad DOOM2.WAD --map MAP01 [--pwad X] [--demo LMP --ticks T1,T2] --out sheets/lmbake_map01.png

import argparse, os, re, subprocess, tempfile
import numpy as np
from PIL import Image, ImageDraw
from gamescope_wrap import wrap as gamescope_wrap, env as gamescope_env

KIT = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.abspath(os.path.join(KIT, "..", "..", "build"))
W, H = 1280, 720


def run_engine(iwad, pwad, mapname, demo, ticks, workdir):
    ini = os.path.join(workdir, "cfg.ini")
    cmd = [os.path.join(BUILD, "uzdoom"), "-config", ini, "-iwad", os.path.join("wads", iwad)]
    if pwad:
        cmd += ["-file", os.path.join("wads", pwad)]
    cmd += ["-width", str(W), "-height", str(H), "-nomonsters", "-rngseed", "12345", "+set", "vid_fullscreen", "0",
            "+set", "vid_preferbackend", "1", "+set", "gl_uselevelmesh", "1", "+set", "lm_bake_objdir", workdir,
            "+set", "screenblocks", "12", "+set", "r_drawplayersprites", "0", "+set", "crosshair", "0",
            "+set", "fov", "90", "+set", "vid_scalemode", "0", "+set", "vid_scalefactor", "1",
            "+set", "r_ab_capture", ",".join(map(str, ticks)) + ":" + workdir]
    cmd += ["-playdemo", os.path.abspath(demo)] if demo else ["+map", mapname]
    out = subprocess.run(gamescope_wrap(cmd, W, H), cwd=BUILD, env=gamescope_env(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace", timeout=600).stdout
    views = {}
    last = None
    for l in out.splitlines():
        m = re.match(r"ABCAPTURE (\S+\.png)$", l.strip())
        if m:
            last = m.group(1)
        m = re.match(r"ABCAPTURE view ([-\d.]+) ([-\d.]+) ([-\d.]+) yaw ([-\d.]+) pitch ([-\d.]+)", l.strip())
        if m and last:
            views[last] = [float(m.group(i)) for i in range(1, 6)]
    obj = os.path.join(workdir, mapname + ".obj")
    if not views or not os.path.exists(obj):
        raise SystemExit("engine run failed:\n" + "\n".join(l for l in out.splitlines() if "\x1b" not in l)[-3000:])
    return sorted(views.items(), key=lambda kv: int(re.search(r"_(\d+)\.png$", kv[0]).group(1))), obj


def load_obj(path):
    vs, fs = [], []
    with open(path) as f:
        for l in f:
            if l.startswith("v "):
                _, x, y, z = l.split()
                vs.append((float(x), -float(z), float(y)))  # back to Doom (x, y, z)
            elif l.startswith("f "):
                fs.append([int(t) - 1 for t in l.split()[1:4]])
    return np.array(vs, np.float64), np.array(fs, np.int64)


def camera(P, pos, yaw, pitch):
    a, p = np.radians(yaw), np.radians(pitch)
    fwd = np.array([np.cos(p) * np.cos(a), np.cos(p) * np.sin(a), -np.sin(p)])
    up = np.array([np.sin(p) * np.cos(a), np.sin(p) * np.sin(a), np.cos(p)])
    right = np.array([np.sin(a), -np.cos(a), 0.0])
    d = P - pos
    d[:, 2] *= 1.2  # pixelstretch, applied before the view rotation (HWDrawInfo::SetViewMatrix)
    return np.stack([d @ right, d @ up, d @ fwd], 1)


def to_screen(C, w, h):
    fH = np.tan(np.radians(45)) / (4 / 3)
    fW = fH * (w / h)
    return np.stack([(C[:, 0] / (C[:, 2] * fW) * 0.5 + 0.5) * w, (0.5 - C[:, 1] / (C[:, 2] * fH) * 0.5) * h], 1)


def clip_near(tri, near):
    out = []
    for i in range(len(tri)):
        a, b = tri[i], tri[(i + 1) % len(tri)]
        ina, inb = a[2] >= near, b[2] >= near
        if ina:
            out.append(a)
        if ina != inb:
            t = (near - a[2]) / (b[2] - a[2])
            out.append(a + (b - a) * t)
    return out


def rasterize(V, F, pos, yaw, pitch, w, h):
    C = camera(V, pos, yaw, pitch)
    img = np.zeros((h, w, 3), np.float32)
    zb = np.full((h, w), np.inf)
    edges = []
    near = 1.0
    light = np.array([0.3, 0.5, 0.81])
    for f in F:
        n = np.cross(V[f[1]] - V[f[0]], V[f[2]] - V[f[0]])
        ln = np.linalg.norm(n)
        if ln == 0:
            continue
        n /= ln
        shade = 0.35 + 0.65 * abs(n @ light)
        col = (np.array([0.55, 0.75, 1.0]) if abs(n[2]) < 0.5 else np.array([1.0, 0.8, 0.5])) * shade
        tri = C[f]
        if np.all(tri[:, 2] < near):
            continue
        poly = np.array(clip_near(list(tri), near)) if np.any(tri[:, 2] < near) else tri
        S = to_screen(poly, w, h)
        zs = poly[:, 2]
        if len(poly) == 3:
            edges.append((S, zs))
        for k in range(1, len(poly) - 1):
            idx = [0, k, k + 1]
            p, z3 = S[idx], zs[idx]
            area = (p[1, 0] - p[0, 0]) * (p[2, 1] - p[0, 1]) - (p[2, 0] - p[0, 0]) * (p[1, 1] - p[0, 1])
            if abs(area) < 1e-9:
                continue
            x0, x1 = int(max(0, np.floor(p[:, 0].min()))), int(min(w - 1, np.ceil(p[:, 0].max())))
            y0, y1 = int(max(0, np.floor(p[:, 1].min()))), int(min(h - 1, np.ceil(p[:, 1].max())))
            if x0 > x1 or y0 > y1:
                continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            w0 = ((p[2, 0] - p[1, 0]) * (ys - p[1, 1]) - (p[2, 1] - p[1, 1]) * (xs - p[1, 0])) / area
            w1 = ((p[0, 0] - p[2, 0]) * (ys - p[2, 1]) - (p[0, 1] - p[2, 1]) * (xs - p[2, 0])) / area
            w2 = 1 - w0 - w1
            inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not inside.any():
                continue
            iz = w0 / z3[0] + w1 / z3[1] + w2 / z3[2]
            z = 1 / np.where(inside, iz, 1e-30)
            sub = zb[y0:y1 + 1, x0:x1 + 1]
            upd = inside & (z < sub)
            sub[upd] = z[upd]
            img[y0:y1 + 1, x0:x1 + 1][upd] = col
    return (img * 255).astype(np.uint8), edges, zb


def visible_edges(draw, edges, zb):
    h, w = zb.shape
    for p, z in edges:
        for i in range(len(p)):
            j = (i + 1) % len(p)
            n = int(max(abs(p[j, 0] - p[i, 0]), abs(p[j, 1] - p[i, 1]))) + 1
            if n > 4000:
                continue
            t = np.linspace(0, 1, n)
            x = p[i, 0] + (p[j, 0] - p[i, 0]) * t
            y = p[i, 1] + (p[j, 1] - p[i, 1]) * t
            iz = (1 - t) / z[i] + t / z[j]
            xi, yi = x.astype(int), y.astype(int)
            ok = (xi >= 0) & (xi < w) & (yi >= 0) & (yi < h)
            xi, yi, iz = xi[ok], yi[ok], iz[ok]
            vis = 1 / iz <= zb[yi, xi] * 1.01 + 0.5
            for px, py in zip(xi[vis], yi[vis]):
                draw.point((px, py), fill=(255, 0, 255))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iwad", default="DOOM2.WAD")
    ap.add_argument("--pwad")
    ap.add_argument("--map", required=True)
    ap.add_argument("--demo", help="play this demo instead of +map")
    ap.add_argument("--ticks", default="20")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    work = tempfile.mkdtemp(prefix="lmcmp_")
    views, obj = run_engine(a.iwad, a.pwad, a.map, a.demo, [int(t) for t in a.ticks.split(",")], work)
    V, F = load_obj(obj)
    rows = []
    for shot, (x, y, z, yaw, pitch) in views:
        classic = Image.open(shot).convert("RGB")
        w, h = classic.size
        img, edges, zb = rasterize(V, F, np.array([x, y, z]), yaw, pitch, w, h)
        over = classic.copy()
        visible_edges(ImageDraw.Draw(over), edges, zb)
        row = Image.new("RGB", (w * 3, h))
        for i, im in enumerate((classic, Image.fromarray(img), over)):
            row.paste(im, (i * w, 0))
        rows.append(row.resize((w * 3 // 2, h // 2)))
        print(f"{a.map} {os.path.basename(shot)}: view ({x}, {y}, {z}) yaw {yaw} pitch {pitch}")
    sheet = Image.new("RGB", (rows[0].width, sum(r.height for r in rows)))
    yy = 0
    for r in rows:
        sheet.paste(r, (0, yy))
        yy += r.height
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    sheet.save(a.out)
    print(f"{len(F)} tris, wrote {a.out}")


if __name__ == "__main__":
    main()
