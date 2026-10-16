#!/usr/bin/env python3
"""wad_map.py — look at a Doom-format map's geometry when placing A/B ticks,
vantages or scripted demo steps.

  wad_map.py plot MAP [--wad WAD] [--out PNG] [--width 1600]
      Top-down line plot: one-sided lines white, two-sided gray, lines of
      tagged sectors red; map coordinates labelled every 256 units.
      Default output: logs/plot_<MAP>.png. Prints the map's bounds.

  wad_map.py sectors MAP [--wad WAD] [--all]
      Sectors with a tag or a special (every sector with --all): floor,
      ceiling, light, special, tag and the bounding box of their lines.

WAD defaults to wads/DOOM2.WAD under this directory. Only the binary Doom
map format is read (not Hexen-format or UDMF maps: use the TEXTMAP).
"""

import argparse
import os
import struct
import sys

KIT = os.path.dirname(os.path.abspath(__file__))


def load_map(wad, mapname):
    with open(wad, "rb") as f:
        data = f.read()
    n, off = struct.unpack("<4xii", data[:12])
    dirs = [struct.unpack("<ii8s", data[off + 16 * i:off + 16 * i + 16]) for i in range(n)]
    names = [d[2].rstrip(b"\0").decode("ascii", "replace").upper() for d in dirs]
    if mapname.upper() not in names:
        sys.exit(f"{mapname} not in {wad}")
    i = names.index(mapname.upper())

    def lump(name):
        for j in range(i + 1, min(i + 12, n)):
            if names[j] == name:
                return data[dirs[j][0]:dirs[j][0] + dirs[j][1]]
        return None

    if lump("TEXTMAP") is not None or lump("BEHAVIOR") is not None:
        sys.exit(f"{mapname} is not a Doom-format map (UDMF or Hexen format)")
    L, S, V, SD = lump("LINEDEFS"), lump("SECTORS"), lump("VERTEXES"), lump("SIDEDEFS")
    verts = [struct.unpack("<hh", V[k:k + 4]) for k in range(0, len(V), 4)]
    # floor, ceiling, floor tex, ceiling tex, light, special, tag
    secs = [struct.unpack("<hh8s8shhh", S[k:k + 26]) for k in range(0, len(S), 26)]
    # x offset, y offset, upper, lower, mid, sector
    sides = [struct.unpack("<hh8s8s8sh", SD[k:k + 30]) for k in range(0, len(SD), 30)]
    # v1, v2, flags, special, tag, front side, back side
    lines = [struct.unpack("<hhhhhhh", L[k:k + 14]) for k in range(0, len(L), 14)]
    return verts, secs, sides, lines


def cmd_plot(args):
    from PIL import Image, ImageDraw
    verts, secs, sides, lines = load_map(args.wad, args.map)
    xs, ys = [v[0] for v in verts], [v[1] for v in verts]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    sc = args.width / (x1 - x0)
    h = int((y1 - y0) * sc) + 1
    im = Image.new("RGB", (args.width + 1, h + 1), "black")
    d = ImageDraw.Draw(im)

    def p(v):
        return ((v[0] - x0) * sc, (y1 - v[1]) * sc)

    for v1, v2, _flags, _special, _tag, s1, s2 in lines:
        tagged = any(sd >= 0 and secs[sides[sd][5]][6] for sd in (s1, s2))
        col = "red" if tagged else ("white" if s2 < 0 else "gray")
        d.line([p(verts[v1]), p(verts[v2])], fill=col, width=2 if tagged else 1)
    for gx in range((x0 // 256) * 256, x1, 256):
        d.text(((gx - x0) * sc, 0), str(gx), fill="yellow")
    for gy in range((y0 // 256) * 256, y1, 256):
        d.text((0, (y1 - gy) * sc), str(gy), fill="yellow")
    out = args.out or os.path.join(KIT, "logs", f"plot_{args.map}.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    im.save(out)
    print(f"bounds x {x0}..{x1} y {y0}..{y1}; wrote {out}")


def cmd_sectors(args):
    verts, secs, sides, lines = load_map(args.wad, args.map)
    bb = {}
    for v1, v2, _flags, _special, _tag, s1, s2 in lines:
        for sd in (s1, s2):
            if sd < 0:
                continue
            b = bb.setdefault(sides[sd][5], [1e9, 1e9, -1e9, -1e9])
            for v in (verts[v1], verts[v2]):
                b[0], b[1] = min(b[0], v[0]), min(b[1], v[1])
                b[2], b[3] = max(b[2], v[0]), max(b[3], v[1])
    for i, s in enumerate(secs):
        if s[6] or s[5] or args.all:
            print(i, "floor", s[0], "ceil", s[1], "light", s[4], "special", s[5], "tag", s[6], "bbox", bb.get(i))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, func in (("plot", cmd_plot), ("sectors", cmd_sectors)):
        p = sub.add_parser(name)
        p.add_argument("map")
        p.add_argument("--wad", default=os.path.join(KIT, "wads", "DOOM2.WAD"))
        p.set_defaults(func=func)
        if name == "plot":
            p.add_argument("--out")
            p.add_argument("--width", type=int, default=1600)
        else:
            p.add_argument("--all", action="store_true")
    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
