#!/usr/bin/env python3
"""
make_3dfloortest.py — generate wads/3dfloortest.wad and its scripted demo
demos/3dfloortest/3DFLOORTEST_MAP01.lmp, the 3D-floor A/B map
(.scratch/levelmesh-impl ticket 12).

A row of ROOMS rooms (256 wide, x = 256 * i, floor 0, ceiling 192, light
160) joined by open two-sided lines, so every whole-room 3D floor also shows
its sides on the dividers seen from the neighbours. The 3D floors are
Sector_Set3DFloor lines on small control sectors south of the row (y < 0).
"box" rooms tag an inner 128 x 128 sector (y 192..320) instead of the room.

  0  box, solid, 8..32, control light = room light (the 3D light equals the
     sector's: no visible light change)
  1  box, solid, 56..88, control light 96 + red light color: the floor under
     the box takes the 3D light (P_GetPlaneLight)
  2  room, non-solid, 64..80, light 96 + green: the room's walls are split
     at the 3D floor (Put3DWall pieces below it), floor in the 3D light
  3  room, non-solid, bottom -64, sloped top -32..160 across x, light 80:
     the light plane (the 3D floor's top) crosses the bottom edge of the
     north / south walls (banded walls: the classic split planes)
  4  room, two non-solid 3D floors: 8..24 light 224, and 104..120 light 64
     with FF_DOUBLESHADOW
  5  room, non-solid, 16..32, light 96, moved by the demo
     (FloorAndCeiling_RaiseByValue / LowerByValue on its control sector)
  6  box, solid, 24..88, masked sides (MIDGRATE)
  7  room, non-solid, 64..80, light 128: the demo changes the control
     sector's light (Light_ChangeToValue) and color (Sector_SetColor)
  8  box, solid, 24..88, translucent (alpha 128: classic, ticket 16)
  9  room, FF_FOG 3D floor 0..96 (fog: classic, ticket 16)
  10 room, ExtraFloor_LightOnly, light 64 below 96
  11 box, solid, 24..88, FF_UPPERTEXTURE (sides take the box lines' upper
     texture)
  12 room, swimmable (FF_BOTHPLANES | FF_ALLSIDES), opaque, 0..56: the
     camera is inside it
  13 room, non-solid, bottom 16, sloped top 40..104, light 80: the light
     plane lies inside the north / south walls (SplitWall cuts them along it)

The demo visits every room (warp to x0 + 128, y 48), looks north, then
north-east across the divider into the next room. Captured every 5 tics.

Usage: make_3dfloortest.py [--out wads/3dfloortest.wad] [--demo demos/3dfloortest/3DFLOORTEST_MAP01.lmp]
"""

import argparse
import math
import os

import make_demo
from make_lighttest import write_wad

ROOMS = 14
W = 256
DEPTH = 512
FLOOR, CEIL, LIGHT = 0, 192, 160
BOX = (64, 192, 192, 320)       # x0 + .., y range of the inner box sector

# Sector_Set3DFloor (160): arg1 type, arg2 flags (specials.cpp Set3DFloor)
SOLID, SWIM, NONSOLID = 1, 2, 3
F_DOUBLESHADOW, F_FOG, F_UPPER = 2, 4, 16

# room -> (tag the inner box instead of the room, [3D floor controls])
# control: dict(bottom, top, light, tex, type, flags, alpha, color, fade, slope, special)
ROOMDEFS = {
    0: (True, [dict(bottom=8, top=32, light=LIGHT, tex="METAL")]),
    1: (True, [dict(bottom=56, top=88, light=96, color=0xff8080, tex="WOOD1")]),
    2: (False, [dict(bottom=64, top=80, light=96, color=0x80ff80, tex="STONE2", type=NONSOLID)]),
    3: (False, [dict(bottom=-64, top=160, light=80, tex="STONE2", type=NONSOLID, slope=(-32, 160))]),
    4: (False, [dict(bottom=8, top=24, light=224, tex="METAL", type=NONSOLID),
                dict(bottom=104, top=120, light=64, tex="WOOD1", type=NONSOLID, flags=F_DOUBLESHADOW)]),
    5: (False, [dict(bottom=16, top=32, light=96, tex="METAL", type=NONSOLID, id=205)]),
    6: (True, [dict(bottom=24, top=88, light=LIGHT, tex="MIDGRATE")]),
    7: (False, [dict(bottom=64, top=80, light=128, tex="STONE2", type=NONSOLID, id=207)]),
    8: (True, [dict(bottom=24, top=88, light=LIGHT, tex="METAL", alpha=128)]),
    9: (False, [dict(bottom=0, top=96, light=LIGHT, tex="-", type=NONSOLID, flags=F_FOG, fade=0x4060a0)]),
    10: (False, [dict(bottom=0, top=96, light=64, tex="-", special=50)]),
    11: (True, [dict(bottom=24, top=88, light=LIGHT, tex="METAL", flags=F_UPPER)]),
    12: (False, [dict(bottom=0, top=56, light=LIGHT, tex="SFALL1", type=SWIM)]),
    13: (False, [dict(bottom=16, top=104, light=80, tex="STONE2", type=NONSOLID, slope=(40, 104))]),
}


def fmt(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        return "%.9g" % v
    if isinstance(v, str):
        return '"%s"' % v
    return str(v)


def block(kind, props):
    return "%s\n{\n%s}\n" % (kind, "".join("%s = %s;\n" % (k, fmt(v)) for k, v in props.items()))


def ceiling_slope(x0, z0, z1):
    """UDMF ceilingplane_a..d for z = z0 + (z1 - z0) * (x - x0) / W
    (normalized: the loader normalizes a, b, c but not d). z = -(d + a x) / c
    with c = -1 (a ceiling's normal points down): a = k, d = z0 - k x0."""
    k = (z1 - z0) / W
    n = math.sqrt(k * k + 1)
    return {"ceilingplane_a": k / n, "ceilingplane_b": 0.0, "ceilingplane_c": -1.0 / n, "ceilingplane_d": (z0 - k * x0) / n}


def build_textmap():
    verts, vindex = [], {}

    def vert(x, y):
        if (x, y) not in vindex:
            vindex[(x, y)] = len(verts)
            verts.append((x, y))
        return vindex[(x, y)]

    sides, lines, sectors = [], [], []

    def side(sector, mid="-", top="-", bottom="-"):
        sides.append({"sector": sector, "texturemiddle": mid, "texturetop": top, "texturebottom": bottom})
        return len(sides) - 1

    def sector(props):
        sectors.append(props)
        return len(sectors) - 1

    walltex = ["BROWN1", "GRAY1", "STARTAN3"]
    room = [sector({"heightfloor": FLOOR, "heightceiling": CEIL, "texturefloor": "FLOOR0_1",
                    "textureceiling": "CEIL3_5", "lightlevel": LIGHT}) for _ in range(ROOMS)]

    for i in range(ROOMS):
        x0, x1 = i * W, (i + 1) * W
        tex = walltex[i % 3]
        boxed, controls = ROOMDEFS[i]
        lines.append({"v1": vert(x1, 0), "v2": vert(x0, 0), "sidefront": side(room[i], tex), "blocking": True})
        lines.append({"v1": vert(x0, DEPTH), "v2": vert(x1, DEPTH), "sidefront": side(room[i], tex), "blocking": True})
        if i == 0:
            lines.append({"v1": vert(x0, 0), "v2": vert(x0, DEPTH), "sidefront": side(room[0], tex), "blocking": True})
        if i == ROOMS - 1:
            lines.append({"v1": vert(x1, DEPTH), "v2": vert(x1, 0), "sidefront": side(room[i], tex), "blocking": True})
        else:
            lines.append({"v1": vert(x1, 0), "v2": vert(x1, DEPTH), "sidefront": side(room[i + 1]),
                          "sideback": side(room[i]), "twosided": True})

        tag = 100 + i
        if boxed:
            bx0, by0, bx1, by1 = x0 + BOX[0], BOX[1], x0 + BOX[2], BOX[3]
            box = sector({"heightfloor": FLOOR, "heightceiling": CEIL, "texturefloor": "FLOOR0_1",
                          "textureceiling": "CEIL3_5", "lightlevel": LIGHT, "id": tag})
            # counter-clockwise ring: the front (right-hand) side is the box
            ring = [(bx0, by0), (bx0, by1), (bx1, by1), (bx1, by0)]
            for k in range(4):
                a, b = ring[k], ring[(k + 1) % 4]
                lines.append({"v1": vert(*a), "v2": vert(*b), "sidefront": side(box, "-", "BIGBRIK1"),
                              "sideback": side(room[i], "-", "BIGBRIK1"), "twosided": True})
        else:
            sectors[room[i]]["id"] = tag

        for k, c in enumerate(controls):
            cx0, cy0 = x0 + 96, -200 - 128 * (k + 1)
            props = {"heightfloor": c["bottom"], "heightceiling": c["top"], "texturefloor": "FLAT5_4",
                     "textureceiling": "FLAT20", "lightlevel": c["light"]}
            if "color" in c:
                props["lightcolor"] = c["color"]
            if "fade" in c:
                props["fadecolor"] = c["fade"]
            if "id" in c:
                props["id"] = c["id"]
            if "slope" in c:
                props.update(ceiling_slope(x0, *c["slope"]))
            cs = sector(props)
            ring = [(cx0, cy0), (cx0, cy0 + 64), (cx0 + 64, cy0 + 64), (cx0 + 64, cy0)]
            for e in range(4):
                a, b = ring[e], ring[(e + 1) % 4]
                l = {"v1": vert(*a), "v2": vert(*b), "sidefront": side(cs, c["tex"] if e == 0 else "METAL"), "blocking": True}
                if e == 0:
                    if c.get("special") == 50:
                        l.update(special=50, arg0=tag, arg1=0)
                    else:
                        l.update(special=160, arg0=tag, arg1=c.get("type", SOLID), arg2=c.get("flags", 0), arg3=c.get("alpha", 255))
                lines.append(l)

    out = ['namespace = "zdoom";\n']
    out.append(block("thing", {"x": 128.0, "y": 48.0, "angle": 90, "type": 1, "skill1": True, "skill2": True,
                               "skill3": True, "skill4": True, "skill5": True, "single": True}))
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
map MAP01 "3D floor test"
{
    sky1 = "SKY1"
}
"""

HOLD = 30       # tics per room
MOVE = 120      # room 5: the 3D floor rises 96 units at speed 8 (1 / tic), then lowers


def demo_steps():
    """make_demo.py script steps and the demo length."""
    steps, t = [], 2
    for i in range(ROOMS):
        x0 = i * W
        steps.append("warp:%d,48@%d" % (x0 + 128, t))
        steps.append("face:90@%d" % (t + 1))
        steps.append("face:40@%d" % (t + HOLD // 2))
        hold = HOLD
        if i == 5:
            steps.append("special:96,205,8,96@%d" % (t + 2))
            steps.append("face:90@%d" % (t + 40))
            steps.append("special:95,205,32,96@%d" % (t + MOVE - 20))
            hold = MOVE + 20
        elif i == 7:
            steps.append("special:112,207,32@%d" % (t + 6))
            steps.append("special:212,207,255,64,64@%d" % (t + 11))
            steps.append("special:112,207,200@%d" % (t + 21))
        t += hold
    return steps, t + 5


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--out", default=os.path.join(here, "wads", "3dfloortest.wad"))
    ap.add_argument("--demo", default=os.path.join(here, "demos", "3dfloortest", "3DFLOORTEST_MAP01.lmp"))
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
