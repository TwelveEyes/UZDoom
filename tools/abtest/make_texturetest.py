#!/usr/bin/env python3
"""
make_texturetest.py — generate wads/texturetest.wad, the runtime texture
state A/B map (.scratch/levelmesh-impl ticket 11).

A row of ROOMS rooms (256 wide, x = 256 * i) joined by two-sided lines, as
in make_lighttest.py. Floors alternate (0 / 16) so every divider has a lower
tier. The demo (demos/texturetest/, see the README) changes the state with
line specials (make_demo.py `special:` steps) and one switch press:

  0  animated textures: north wall FIREBLU1, floor NUKAGE1 (ANIMATED
     defaults), ceiling CEIL5_1
  1  wall scroller: Scroll_Wall on the north wall (line id 1), started by
     the demo; floor scroller: Scroll_Floor on the room (sector tag 1)
  2  switch: south wall SW1BRCOM, use -> Floor_LowerByValue on room 3
     (repeatable: SW1 -> SW2 -> SW1 after a second)
  3  floor texture change: Floor_LowerToLowestTxTy (tag 3) lowers it to the
     lowest neighbour (room 4, floor -16, FLAT5_4) and takes its texture
  4  Line_SetTextureOffset on the north wall (line id 4): x / y offsets
  5  Line_SetTextureScale on the north wall (line id 5): a scale the bake
     cannot express (classic fallback), then back to 1
  6  two-sided mid texture MIDGRATE on the divider 6 | 7 (line id 6, no
     wrap): an x offset (levelmesh), then a y offset (moves the clipped
     geometry: classic fallback)
  7  plain end room

Usage: make_texturetest.py [--out wads/texturetest.wad]
"""

import argparse
import os

from make_lighttest import block, write_wad

ROOMS = 8
W = 256
DEPTH = 512


def sector_props(i):
    p = {"heightfloor": 16 if i % 2 else 0, "heightceiling": 160, "texturefloor": "FLOOR0_1",
         "textureceiling": "CEIL5_1", "lightlevel": 192}
    if i == 0:
        p.update(texturefloor="NUKAGE1")
    elif i == 1:
        p.update(id=1, texturefloor="FLOOR4_8")
    elif i == 3:
        p.update(id=3, texturefloor="FLOOR5_1")
    elif i == 4:
        p.update(heightfloor=-16, texturefloor="FLAT5_4")
    return p


def build_textmap():
    verts, vindex = [], {}

    def vert(x, y):
        if (x, y) not in vindex:
            vindex[(x, y)] = len(verts)
            verts.append((x, y))
        return vindex[(x, y)]

    sides, lines = [], []

    def side(sector, mid="-", top="-", bottom="-"):
        sides.append({"sector": sector, "texturemiddle": mid, "texturetop": top, "texturebottom": bottom})
        return len(sides) - 1

    walltex = ["BROWN1", "GRAY1", "STONE2"]
    for i in range(ROOMS):
        x0, x1 = i * W, (i + 1) * W
        tex = walltex[i % 3]
        south = {"v1": vert(x1, 0), "v2": vert(x0, 0), "sidefront": side(i, tex), "blocking": True}
        north = {"v1": vert(x0, DEPTH), "v2": vert(x1, DEPTH), "sidefront": side(i, tex), "blocking": True}
        if i == 0:
            sides[north["sidefront"]]["texturemiddle"] = "FIREBLU1"
        elif i == 1:
            north["id"] = 1
            sides[north["sidefront"]]["texturemiddle"] = "BIGBRIK1"
        elif i == 2:
            sides[south["sidefront"]]["texturemiddle"] = "SW1BRCOM"
            south.update(special=20, arg0=3, arg1=16, arg2=8, playeruse=True, repeatspecial=True)
        elif i in (4, 5):
            north["id"] = i
            sides[north["sidefront"]]["texturemiddle"] = "STARTAN3"
        lines += [south, north]
        if i == 0:
            lines.append({"v1": vert(x0, DEPTH), "v2": vert(x0, 0), "sidefront": side(0, tex), "blocking": True})
        if i == ROOMS - 1:
            lines.append({"v1": vert(x1, 0), "v2": vert(x1, DEPTH), "sidefront": side(i, tex), "blocking": True})
        else:
            # divider at x1: front = east room (i + 1), back = room i
            mid = "MIDGRATE" if i == 6 else "-"
            front = side(i + 1, mid, "-", "STARTAN3")
            back = side(i, mid, "-", "STARTAN3")
            d = {"v1": vert(x1, 0), "v2": vert(x1, DEPTH), "sidefront": front, "sideback": back, "twosided": True}
            if i == 6:
                d["id"] = 6
            lines.append(d)

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
map MAP01 "Texture state test"
{
	sky1 = "SKY1"
}
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--out", default=os.path.join(here, "wads", "texturetest.wad"))
    args = ap.parse_args()
    lumps = [("MAPINFO", MAPINFO), ("MAP01", b""), ("TEXTMAP", build_textmap()), ("ENDMAP", b"")]
    write_wad(args.out, lumps)
    print("wrote %s (%d rooms)" % (args.out, ROOMS))


if __name__ == "__main__":
    main()
