#!/usr/bin/env python3
"""
make_polytest.py — generate wads/polytest.wad and its scripted demo
demos/polytest/POLYTEST_MAP01.lmp, the polyobject A/B map
(.scratch/levelmesh-impl ticket 13).

A row of ROOMS rooms (256 wide, x = 256 * i, floor 0, ceiling 192, light
160) joined by open two-sided lines, as in make_3dfloortest.py. Each room
holds one polyobject (two in room 9) around (x0 + 128, 288), north of the
camera spot (x0 + 128, 48). The polyobjects are built in origin containers
south of the row (y < 0): one closed sector per polyobject holding its
anchor (PolyAnchor 9300) and its lines (Polyobj_StartLine on the first,
front sides facing out). The start spot (PolySpawn 9301) is in the room.
Polyobject numbers are 1 + the room index (room 9: 10 and 13).

  0  static box (64 x 64), STARTAN3; the demo starts a Scroll_Wall on its
     south side (line id 500): the side state of a polyobject side
  1  slide: box 64 x 32, Polyobj_Move east 64 then back (translation)
  2  rotate: bar 112 x 16 around its centre, Polyobj_RotateLeft 90 degrees
     then RotateRight 45 (rotation; the walls' fake contrast changes)
  3  swing door: slab 96 x 8 hinged at its west end, Polyobj_DoorSwing 90
     degrees, 35 tics open, swings back
  4  cross sectors: an inner sector (x0 + 128 .. x0 + 224, y 160 .. 416:
     floor 24, ceiling 160, light 96) east of a 48 x 48 box that moves 96
     east into it and back (the classic splits it per subsector and takes
     each piece's sector planes and light)
  5  two-sided masked: a 64 x 64 box of two-sided MIDGRATE lines (back
     side = the container), rotated 45 degrees
  6  moving planes: static box; the room's ceiling lowers 64 and the floor
     rises 24, then both return (plane anchoring of polyobject walls)
  7  light: static box; the room's light drops to 64, its colour turns red,
     then the light returns to 200 (light ref of polyobject walls)
  8  3D light: ExtraFloor_LightOnly on the room (light 64 below z 96); a
     box slides 48 east and back (the classic splits its walls at the light
     plane)
  9  mirror: box 10 (Polyobj_StartLine mirror = 13) west of box 13; box 10
     moves 48 west, its mirror 48 east, then back
  10 sliding door: an opening in the north wall (x0 + 96 .. x0 + 160) to a
     closet (y 512 .. 640); a 64 x 8 door poly just inside it slides 64 east
     into the void behind the wall (Polyobj_DoorSlide), waits, closes
  11 texture offsets: static box (south side line id 511),
     Line_SetTextureOffset x 16 / y 8, then 40 / 0

The demo visits every room (warp to x0 + 128, y 48), looks north at the
polyobject, then north-east, while the room's specials run. Captured every
5 tics.

Usage: make_polytest.py [--out wads/polytest.wad] [--demo demos/polytest/POLYTEST_MAP01.lmp]
"""

import argparse
import os

import make_demo
from make_3dfloortest import block
from make_lighttest import write_wad

ROOMS = 12
W = 256
DEPTH = 512
FLOOR, CEIL, LIGHT = 0, 192, 160
CY = 288            # room y of the polyobjects' start spots
CONTAINER = 176     # origin container size (square)

MIRROR = 13         # the mirror polyobject of room 9


def rect(cx, cy, w, h):
    """Outward-facing ring (front = right-hand side = outside): the
    polyobject lines' winding, relative to (cx, cy)."""
    x0, y0, x1, y1 = cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


# room -> list of polyobjects: dict(po, shape (ring offsets from the start
# spot), tex, twosided, mirror, lineid (south side), spot offset)
def polydefs():
    d = {}
    d[0] = [dict(po=1, shape=rect(0, 0, 64, 64), tex="STARTAN3", lineid=500)]
    d[1] = [dict(po=2, shape=rect(0, 0, 64, 32), tex="BROWN96")]
    d[2] = [dict(po=3, shape=rect(0, 0, 112, 16), tex="METAL")]
    # swing door: hinge (start spot) at the west end
    d[3] = [dict(po=4, shape=[(0, -4), (96, -4), (96, 4), (0, 4)], tex="WOOD1", spot=(-48, 0))]
    d[4] = [dict(po=5, shape=rect(0, 0, 48, 48), tex="STARTAN3", spot=(-48, 0))]
    d[5] = [dict(po=6, shape=rect(0, 0, 64, 64), tex="MIDGRATE", twosided=True)]
    d[6] = [dict(po=7, shape=rect(0, 0, 64, 64), tex="STARTAN3")]
    d[7] = [dict(po=8, shape=rect(0, 0, 64, 64), tex="STARTAN3")]
    d[8] = [dict(po=9, shape=rect(0, 0, 64, 32), tex="BROWN96")]
    d[9] = [dict(po=10, shape=rect(0, 0, 48, 48), tex="STARTAN3", spot=(-40, 0), mirror=MIRROR),
            dict(po=MIRROR, shape=rect(0, 0, 48, 48), tex="BROWN96", spot=(40, 0))]
    # the door: just inside the closet (y 516 .. 524), start spot at its centre
    d[10] = [dict(po=11, shape=rect(0, 0, 64, 8), tex="BIGDOOR2", spot=(0, 520 - CY))]
    d[11] = [dict(po=12, shape=rect(0, 0, 64, 64), tex="STARTAN3", lineid=511)]
    return d


def build_textmap():
    verts, vindex = [], {}

    def vert(x, y):
        if (x, y) not in vindex:
            vindex[(x, y)] = len(verts)
            verts.append((x, y))
        return vindex[(x, y)]

    def newvert(x, y):
        # polyobject vertices must not be shared with other lines
        verts.append((x, y))
        return len(verts) - 1

    sides, lines, sectors, things = [], [], [], []

    def side(sector, mid="-", top="-", bottom="-"):
        sides.append({"sector": sector, "texturemiddle": mid, "texturetop": top, "texturebottom": bottom})
        return len(sides) - 1

    def sector(props):
        sectors.append(props)
        return len(sectors) - 1

    def thing(x, y, type, angle=0):
        things.append({"x": float(x), "y": float(y), "angle": angle, "type": type, "skill1": True, "skill2": True,
                       "skill3": True, "skill4": True, "skill5": True, "single": True})

    walltex = ["BROWN1", "GRAY1", "STARTAN3"]
    room = [sector({"heightfloor": FLOOR, "heightceiling": CEIL, "texturefloor": "FLOOR0_1",
                    "textureceiling": "CEIL3_5", "lightlevel": LIGHT, "id": 100 + i}) for i in range(ROOMS)]
    polys = polydefs()
    container = 0

    for i in range(ROOMS):
        x0, x1 = i * W, (i + 1) * W
        tex = walltex[i % 3]
        lines.append({"v1": vert(x1, 0), "v2": vert(x0, 0), "sidefront": side(room[i], tex), "blocking": True})
        if i == 10:
            # north wall with an opening (x0 + 96 .. x0 + 160) to a closet
            closet = sector({"heightfloor": FLOOR, "heightceiling": 128, "texturefloor": "FLOOR0_1",
                             "textureceiling": "CEIL3_5", "lightlevel": LIGHT})
            ox0, ox1 = x0 + 96, x0 + 160
            lines.append({"v1": vert(x0, DEPTH), "v2": vert(ox0, DEPTH), "sidefront": side(room[i], tex), "blocking": True})
            lines.append({"v1": vert(ox1, DEPTH), "v2": vert(x1, DEPTH), "sidefront": side(room[i], tex), "blocking": True})
            lines.append({"v1": vert(ox1, DEPTH), "v2": vert(ox0, DEPTH), "sidefront": side(closet, "-", "BROWN1"),
                          "sideback": side(room[i], "-", "BROWN1"), "twosided": True})
            ring = [(ox0, DEPTH), (ox0, DEPTH + 128), (ox1, DEPTH + 128), (ox1, DEPTH)]
            for k in range(3):
                a, b = ring[k], ring[k + 1]
                lines.append({"v1": vert(*a), "v2": vert(*b), "sidefront": side(closet, "GRAY1"), "blocking": True})
        else:
            lines.append({"v1": vert(x0, DEPTH), "v2": vert(x1, DEPTH), "sidefront": side(room[i], tex), "blocking": True})
        if i == 0:
            lines.append({"v1": vert(x0, 0), "v2": vert(x0, DEPTH), "sidefront": side(room[0], tex), "blocking": True})
        if i == ROOMS - 1:
            lines.append({"v1": vert(x1, DEPTH), "v2": vert(x1, 0), "sidefront": side(room[i], tex), "blocking": True})
        else:
            lines.append({"v1": vert(x1, 0), "v2": vert(x1, DEPTH), "sidefront": side(room[i + 1]),
                          "sideback": side(room[i]), "twosided": True})

        if i == 4:
            # inner sector: raised floor, lowered ceiling, darker
            inner = sector({"heightfloor": 24, "heightceiling": 160, "texturefloor": "FLAT5_4",
                            "textureceiling": "FLAT20", "lightlevel": 96})
            ring = [(x0 + 128, 160), (x0 + 128, 416), (x0 + 224, 416), (x0 + 224, 160)]
            for k in range(4):
                a, b = ring[k], ring[(k + 1) % 4]
                lines.append({"v1": vert(*a), "v2": vert(*b), "sidefront": side(inner, "-", "BIGBRIK1", "BIGBRIK1"),
                              "sideback": side(room[i], "-", "BIGBRIK1", "BIGBRIK1"), "twosided": True})
        if i == 8:
            # ExtraFloor_LightOnly control: light 64 below z 96
            cx0, cy0 = x0 + 96, -1400
            cs = sector({"heightfloor": 0, "heightceiling": 96, "texturefloor": "FLAT5_4",
                         "textureceiling": "FLAT20", "lightlevel": 64})
            ring = [(cx0, cy0), (cx0, cy0 + 64), (cx0 + 64, cy0 + 64), (cx0 + 64, cy0)]
            for e in range(4):
                a, b = ring[e], ring[(e + 1) % 4]
                l = {"v1": vert(*a), "v2": vert(*b), "sidefront": side(cs, "METAL"), "blocking": True}
                if e == 0:
                    l.update(special=50, arg0=100 + i, arg1=0)
                lines.append(l)

        # polyobjects: start spot in the room, origin container in the south
        for p in polys.get(i, []):
            sx, sy = p.get("spot", (0, 0))
            spot = (x0 + 128 + sx, CY + sy)
            thing(*spot, 9301, p["po"])
            ccx = x0 + 128
            ccy = -200 - (CONTAINER + 32) * container - CONTAINER / 2
            container += 1
            cont = sector({"heightfloor": FLOOR, "heightceiling": CEIL, "texturefloor": "FLAT5_4",
                           "textureceiling": "FLAT20", "lightlevel": LIGHT})
            h = CONTAINER / 2
            ring = [(ccx - h, ccy - h), (ccx - h, ccy + h), (ccx + h, ccy + h), (ccx + h, ccy - h)]
            for e in range(4):
                a, b = ring[e], ring[(e + 1) % 4]
                lines.append({"v1": vert(*a), "v2": vert(*b), "sidefront": side(cont, "GRAY1"), "blocking": True})
            thing(ccx, ccy, 9300, p["po"])
            shape = [(ccx + dx, ccy + dy) for dx, dy in p["shape"]]
            pv = [newvert(*v) for v in shape]
            for e in range(len(pv)):
                l = {"v1": pv[e], "v2": pv[(e + 1) % len(pv)]}
                if p.get("twosided"):
                    l.update(sidefront=side(cont, p["tex"]), sideback=side(cont, p["tex"]), twosided=True)
                else:
                    l.update(sidefront=side(cont, p["tex"]), blocking=True)
                if e == 0:
                    l.update(special=1, arg0=p["po"], arg1=p.get("mirror", 0))
                    if "lineid" in p:
                        l["id"] = p["lineid"]
                lines.append(l)

    out = ['namespace = "zdoom";\n']
    out.append(block("thing", {"x": 128.0, "y": 48.0, "angle": 90, "type": 1, "skill1": True, "skill2": True,
                               "skill3": True, "skill4": True, "skill5": True, "single": True}))
    for t in things:
        out.append(block("thing", t))
    for x, y in verts:
        out.append(block("vertex", {"x": float(x), "y": float(y)}))
    for l in lines:
        out.append(block("linedef", l))
    for s in sides:
        out.append(block("sidedef", s))
    for s in sectors:
        out.append(block("sector", s))
    return "".join(out).encode("ascii")


MAPINFO = b"""
map MAP01 "Polyobject test"
{
    sky1 = "SKY1"
}
"""

HOLD = 30       # tics per room without movement


def demo_steps():
    """make_demo.py script steps and the demo length. Each entry: room ->
    (hold tics, [(tic offset, special args)])."""
    F = 1 << 16
    room_specials = {
        0: (60, [(4, (52, 500, F, 0, 0, 2))]),
        1: (90, [(4, (4, 2, 16, 0, 64)), (44, (4, 2, 16, 128, 64))]),
        2: (90, [(4, (2, 3, 16, 64)), (44, (3, 3, 16, 32))]),
        3: (120, [(4, (7, 4, 16, 64, 35))]),
        4: (130, [(4, (4, 5, 16, 0, 96)), (64, (4, 5, 16, 128, 96))]),
        5: (60, [(4, (2, 6, 16, 32))]),
        6: (110, [(4, (40, 106, 16, 64)), (4, (23, 106, 8, 24)), (60, (41, 106, 16, 64)), (60, (20, 106, 8, 24))]),
        7: (60, [(6, (112, 107, 64)), (16, (212, 107, 255, 64, 64)), (36, (112, 107, 200))]),
        8: (90, [(4, (4, 9, 16, 0, 48)), (44, (4, 9, 16, 128, 48))]),
        9: (90, [(4, (4, 10, 16, 128, 48)), (44, (4, 10, 16, 0, 48))]),
        10: (120, [(4, (8, 11, 16, 0, 64, 35))]),
        11: (60, [(6, (53, 511, 16 * F, 8 * F, 0, 2)), (30, (53, 511, 40 * F, 0, 0, 2))]),
    }
    steps, t = [], 2
    for i in range(ROOMS):
        x0 = i * W
        hold, specials = room_specials.get(i, (HOLD, []))
        steps.append("warp:%d,48@%d" % (x0 + 128, t))
        steps.append("face:90@%d" % (t + 1))
        steps.append("face:60@%d" % (t + hold - 10))
        for dt, args in specials:
            steps.append("special:%s@%d" % (",".join(str(a) for a in args), t + dt))
        t += hold
    return steps, t + 5


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--out", default=os.path.join(here, "wads", "polytest.wad"))
    ap.add_argument("--demo", default=os.path.join(here, "demos", "polytest", "POLYTEST_MAP01.lmp"))
    args = ap.parse_args()
    lumps = [("MAPINFO", MAPINFO), ("MAP01", b""), ("TEXTMAP", build_textmap()), ("ENDMAP", b"")]
    write_wad(args.out, lumps)
    print("wrote %s (%d rooms)" % (args.out, ROOMS))

    steps, tics = demo_steps()
    cmds, warps, specials = make_demo.build_script(steps, 90.0, tics)
    data = make_demo.build("MAP01", cmds, 0xA1B2C3D4, ["god", "noclip"], warps, specials)
    os.makedirs(os.path.dirname(args.demo), exist_ok=True)
    with open(args.demo, "wb") as f:
        f.write(data)
    print("wrote %s (%d tics)" % (args.demo, tics))


if __name__ == "__main__":
    main()
