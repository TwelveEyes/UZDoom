#!/usr/bin/env python3
"""
make_lighttest.py — generate wads/lighttest.wad, the shading-resolve A/B map
(.scratch/levelmesh-impl ticket 10).

A row of ROOMS rooms (256 wide, x = 256 * i) joined by two-sided lines. The
north wall zigzags (diagonal lines: smooth fake contrast); floors and
ceilings alternate so every divider has an upper and a lower tier. Each room
exercises one part of the classic light chain:

  0  plain light 192 (fake contrast reference)
  1  light 160, lightcolor ff8040, desaturation 0.5
  2  light 140, fadecolor 404080 (fog: no fake contrast, fog density from light)
  3  light 120, fadecolor 404080, fogdensity 200 (explicit sector density)
  4  light 100 (dark light-mode branch), floor + ceiling sector glow
  5  light 176, floor FLAT14 (GLDEFS auto glow -> fullbright flat + texture glow),
     ceiling CEIL1_2 with a GLDEFS Texture glow (not auto). Static flats: the
     levelmesh does not animate textures yet (ticket 11).
  6  light 220, side lights: absolute, relative, per tier, nofakecontrast,
     smoothlighting, lightfog
  7  light 80, lightfloor -40 (relative), lightceiling 200 (absolute)
  8  light 255, floor glow (dropped: white light at 255)
  9  light 150, fadecolor 404080 with lightfog sides (outside fog in MAP02)

MAP01 uses the defaults. MAP02 is the same geometry with MAPINFO fogdensity,
outsidefog (= room 2/3/9 fade) + outsidefogdensity, smoothlighting and
forcefakecontrast. MAP03 adds a fade table (LEVEL_HASFADETABLE).

Usage: make_lighttest.py [--out wads/lighttest.wad]
"""

import argparse
import os
import struct

ROOMS = 10
W = 256
DEPTH = 512
ZIG = 64

FADE = 0x404080


def north_y(x):
    return DEPTH + (ZIG if (x // W) % 2 else 0)


def sector_props(i):
    p = {"heightfloor": 16 if i % 2 else 0, "heightceiling": 160 if i % 3 == 1 else 128,
         "texturefloor": "FLOOR0_1", "textureceiling": "CEIL5_1", "lightlevel": 192}
    if i == 1:
        p.update(lightlevel=160, lightcolor=0xff8040, desaturation=0.5)
    elif i == 2:
        p.update(lightlevel=140, fadecolor=FADE)
    elif i == 3:
        p.update(lightlevel=120, fadecolor=FADE, fogdensity=200)
    elif i == 4:
        p.update(lightlevel=100, floorglowcolor=0x00ff00, floorglowheight=64.0,
                 ceilingglowcolor=0xff0000, ceilingglowheight=48.0)
    elif i == 5:
        p.update(lightlevel=176, texturefloor="FLAT14", textureceiling="CEIL1_2")
    elif i == 6:
        p.update(lightlevel=220)
    elif i == 7:
        p.update(lightlevel=80, lightfloor=-40, lightceiling=200, lightceilingabsolute=True)
    elif i == 8:
        p.update(lightlevel=255, floorglowcolor=0x00ffff, floorglowheight=64.0)
    elif i == 9:
        p.update(lightlevel=150, fadecolor=FADE)
    return p


def side_props(room, which):
    """Extra sidedef keys for walls of a room (which: 'south', 'north', 'div')."""
    if room == 6:
        return {"south": {"light": 48, "lightabsolute": True},
                "north": {"light": -32, "nofakecontrast": True},
                "div": {"light_top": 40, "lightabsolute_top": True, "light_bottom": -24, "smoothlighting": True}}[which]
    if room == 9:
        return {"south": {"light": 32, "lightfog": True}, "north": {"light": 32}, "div": {"light": 16, "lightfog": True}}[which]
    if room == 0 and which == "north":
        return {"smoothlighting": True}
    return {}


def fmt(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        return "%.3f" % v
    if isinstance(v, str):
        return '"%s"' % v
    return str(v)


def block(kind, props):
    lines = ["%s\n{" % kind]
    for k, v in props.items():
        lines.append("%s = %s;" % (k, fmt(v)))
    lines.append("}\n")
    return "\n".join(lines)


def build_textmap():
    verts = []
    vindex = {}

    def vert(x, y):
        if (x, y) not in vindex:
            vindex[(x, y)] = len(verts)
            verts.append((x, y))
        return vindex[(x, y)]

    sides = []
    lines = []

    def side(sector, tex_mid="-", tex_top="-", tex_bottom="-", extra=None):
        s = {"sector": sector, "texturemiddle": tex_mid, "texturetop": tex_top, "texturebottom": tex_bottom}
        if extra:
            s.update(extra)
        sides.append(s)
        return len(sides) - 1

    walltex = ["BROWN1", "GRAY1", "STONE2"]
    for i in range(ROOMS):
        x0, x1 = i * W, (i + 1) * W
        tex = walltex[i % 3]
        # south wall: east -> west (front faces +y)
        lines.append({"v1": vert(x1, 0), "v2": vert(x0, 0), "sidefront": side(i, tex, extra=side_props(i, "south")), "blocking": True})
        # north wall (diagonal): west -> east (front faces -y)
        lines.append({"v1": vert(x0, north_y(x0)), "v2": vert(x1, north_y(x1)), "sidefront": side(i, tex, extra=side_props(i, "north")), "blocking": True})
        if i == 0:
            lines.append({"v1": vert(x0, north_y(x0)), "v2": vert(x0, 0), "sidefront": side(0, tex), "blocking": True})
        if i == ROOMS - 1:
            lines.append({"v1": vert(x1, 0), "v2": vert(x1, north_y(x1)), "sidefront": side(i, tex), "blocking": True})
        else:
            # divider at x1: (x1, 0) -> (x1, north): front = east room (i + 1), back = room i
            front = side(i + 1, "-", "STARTAN3", "STARTAN3", side_props(i + 1, "div"))
            back = side(i, "-", "STARTAN3", "STARTAN3", side_props(i, "div"))
            lines.append({"v1": vert(x1, 0), "v2": vert(x1, north_y(x1)), "sidefront": front, "sideback": back, "twosided": True})

    out = ['namespace = "zdoom";\n']
    out.append(block("thing", {"x": 128.0, "y": 64.0, "angle": 90, "type": 1, "skill1": True, "skill2": True,
                               "skill3": True, "skill4": True, "skill5": True, "single": True}))
    for x, y in verts:
        out.append(block("vertex", {"x": float(x), "y": float(y)}))
    for l in lines:
        out.append(block("linedef", l))
    for s in sides:
        out.append(block("sidedef", s))
    for i in range(ROOMS):
        out.append(block("sector", sector_props(i)))
    return "".join(out).encode("ascii")


MAPINFO = b"""
map MAP01 "Light test (defaults)"
{
	sky1 = "SKY1"
}

map MAP02 "Light test (level fog, outside fog, smooth + forced fake contrast)"
{
	sky1 = "SKY1"
	fogdensity = 120
	outsidefog = "40 40 80"
	outsidefogdensity = 60
	smoothlighting
	forcefakecontrast
}

map MAP03 "Light test (fade table)"
{
	sky1 = "SKY1"
	fadetable = "FOGMAP"
}
"""

GLDEFS = b"""
Glow
{
	Flats
	{
		FLAT14
	}
	Texture "CEIL1_2", "4060ff", 96
}
"""


def write_wad(path, lumps):
    data = b""
    directory = []
    offset = 12
    for name, content in lumps:
        directory.append((offset, len(content), name))
        data += content
        offset += len(content)
    with open(path, "wb") as f:
        f.write(b"PWAD" + struct.pack("<ii", len(lumps), offset))
        f.write(data)
        for off, size, name in directory:
            f.write(struct.pack("<ii", off, size) + name.encode("ascii").ljust(8, b"\0"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--out", default=os.path.join(here, "wads", "lighttest.wad"))
    args = ap.parse_args()
    textmap = build_textmap()
    lumps = [("MAPINFO", MAPINFO), ("GLDEFS", GLDEFS)]
    for m in ("MAP01", "MAP02", "MAP03"):
        lumps += [(m, b""), ("TEXTMAP", textmap), ("ENDMAP", b"")]
    write_wad(args.out, lumps)
    print("wrote %s (%d rooms, 3 maps)" % (args.out, ROOMS))


if __name__ == "__main__":
    main()
