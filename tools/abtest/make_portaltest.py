#!/usr/bin/env python3
"""
make_portaltest.py — generate wads/portaltest.wad and its scripted demos
demos/portaltest/PORTALTEST_MAP0{1,2}.lmp, the portal A/B maps
(.scratch/levelmesh-impl ticket 15).

ROOMS separate rooms (768 x 768, x0 = 1024 * i, y 0 .. 768), closed by
one-sided walls; the camera spot is (x0 + 384, 64), looking north. A room
whose portal leads somewhere has its destination area north of it
(y DY .. DY + 768, same x), a closed room of its own with other textures
and a few things (a lamp column, a tech pillar). Unless noted a room has
floor 0, CEIL3_5 ceiling at 192, light 192, STARTAN3 walls; destination
areas light 160, BROWN1 / BIGBRIK1 walls.

  0  visual line portal (Line_SetPortal type 0) on the middle of the north
     wall (x0 + 256 .. x0 + 512), to the south wall of the area north.
  1  rotated visual portal with floor alignment (PORG_FLOOR): the
     destination line is the area's east wall, its floor 64 higher. The
     portal line is two-sided (a thin sector behind it): the classic never
     draws a one-sided portal line with floor / ceiling alignment
     (HWWall::Process returns from its alignment switch).
  2  linked line portals (type 3), both ways: the room's north wall and
     the area's south wall (two-sided, with a thin sector behind each).
  3  mirror (Line_Mirror) on the north wall; a pillar and a lamp in the
     room.
  4  two facing mirrors (west and east walls): recursion up to
     r_portal_recursions.
  5  stacked sectors (Sector_SetPortal type 0): a 256 x 256 floor portal
     in the middle (alpha 0) shows the lower room (floor -256, ceiling 0)
     in the area north, whose region under it has a ceiling portal back.
  6  skybox: an F_SKY1 courtyard; the level's default SkyViewpoint
     (tid 0) stands in the area north (a tall room with pillars, its own
     ceiling F_SKY1: the regular sky inside the skybox).
  7  horizon line (Line_Horizon) on the north wall.
  8  plane mirror: floor_reflect 0.5, a pillar and a lamp.
  9  Eternity horizon portal (Sector_SetPortal type 4) on the ceiling,
     planes from a control sector south of the room (floor -64 FLAT5_4,
     ceiling 320 CEIL5_1). The classic shows black there: HWWall::PutPortal
     creates the HWEEHorizonPortal but never pushes it to the portal list.
 10  nested: a visual portal (as room 0) into an area with a mirror on its
     north wall and a pillar.
 11  sector-stack portal seen from below: the room's middle has a ceiling
     portal (alpha 0) into the upper room in the area north (floor 192,
     ceiling 448), whose region above it has a floor portal back.

MAP01: as above. MAP02: the same with fog (fadecolor 405060 on every
sector) and r_portal_recursions left at the default.

Each demo visits the rooms in order (warp, look north, turn, step closer,
look up / down), captured every 5 tics.

Usage: make_portaltest.py [--out wads/portaltest.wad] [--demodir demos/portaltest]
"""

import argparse
import os

import make_demo
from make_lighttest import write_wad
from make_skytest import Map, rect, hole

ROOMS = 12
STRIDE = 1024
W = 768
DY = 1280
SKY = "F_SKY1"
CEIL = "CEIL3_5"
FLOOR = "FLOOR0_1"


def things(m, x0, y0, light=True):
    m.thing(x0 + 192, y0 + 576, 48)		# tech pillar
    if light:
        m.thing(x0 + 576, y0 + 576, 2028)	# lamp column


def build_textmap(fog):
    m = Map(fog)
    room = dict(ceil=192, ctex=CEIL)
    dest = dict(ceil=192, ctex="FLAT5_4", ftex="FLAT14", light=160)
    for i in range(ROOMS):
        x0, x1 = i * STRIDE, i * STRIDE + W
        mx = (x0 + 256, x0 + 512)
        if i in (0, 10):
            m.sector([rect(x0, 0, x1, W, xs=mx)], **room)
            m.edge((mx[0], W), (mx[1], W), special=156, arg0=900 + i, arg2=0)
            pil = rect(x0 + 352, DY + 384, x0 + 416, DY + 448)
            m.sector([rect(x0, DY, x1, DY + W, xs=mx), hole(pil)], wall="BROWN1", **dest)
            m.sector([pil], floor=128, ceil=128, ftex="FLAT5_4", ctex="FLAT5_4", wall="BIGBRIK1", light=160)
            m.edge((mx[0], DY), (mx[1], DY), id=900 + i)
            if i == 10:
                m.edge((x0 + 768, DY + W), (x0, DY + W), special=182)
            things(m, x0, DY)
        elif i == 1:
            ys = (DY + 256, DY + 512)
            m.sector([rect(x0, 0, x1, W, xs=mx)], **room)
            m.edge((mx[0], W), (mx[1], W), special=156, arg0=901, arg2=0, arg3=1)
            # Two-sided: a one-sided portal line with floor / ceiling alignment
            # is never drawn by the classic (HWWall::Process returns in its
            # alignment switch).
            m.sector([rect(mx[0], W, mx[1], W + 16)], **room)
            m.sector([rect(x0, DY, x1, DY + W, ys=ys)], wall="BIGBRIK1", floor=64, ceil=256, ctex="FLAT5_4", ftex="FLAT14", light=160)
            m.edge((x1, ys[0]), (x1, ys[1]), id=901)
            things(m, x0, DY)
        elif i == 2:
            m.sector([rect(x0, 0, x1, W, xs=mx)], **room)
            m.edge((mx[0], W), (mx[1], W), special=156, arg0=902, arg2=3, id=903)
            m.sector([rect(x0, DY, x1, DY + W, xs=mx)], wall="BROWN1", **dest)
            m.edge((mx[0], DY), (mx[1], DY), special=156, arg0=903, arg2=3, id=902)
            # Traversable portals need a back sector (FLevelLocals::UpdatePortal):
            # a thin one behind each line, never seen.
            m.sector([rect(mx[0], W, mx[1], W + 16)], **room)
            m.sector([rect(mx[0], DY - 16, mx[1], DY)], **dest)
            things(m, x0, DY)
        elif i == 3:
            pil = rect(x0 + 320, 384, x0 + 448, 512)
            m.sector([rect(x0, 0, x1, W, xs=mx), hole(pil)], **room)
            m.sector([pil], floor=96, ceil=96, ftex="FLAT5_4", ctex="FLAT5_4", wall="BIGBRIK1")
            m.edge((mx[0], W), (mx[1], W), special=182)
            m.thing(x0 + 192, 256, 2028)
        elif i == 4:
            ys = (256, 512)
            pil = rect(x0 + 352, 520, x0 + 416, 584)
            m.sector([rect(x0, 0, x1, W, ys=ys), hole(pil)], **room)
            m.sector([pil], floor=128, ceil=128, ftex="FLAT5_4", ctex="FLAT5_4", wall="BIGBRIK1")
            m.edge((x0, ys[0]), (x0, ys[1]), special=182)
            m.edge((x1, ys[0]), (x1, ys[1]), special=182)
        elif i == 5:
            pit = rect(x0 + 256, 256, x0 + 512, 512)
            low = rect(x0 + 256, DY + 256, x0 + 512, DY + 512)
            # The portal sectors first: they are the front side of their edges.
            m.sector([pit], ftex=FLOOR, id=51, **room)
            m.sector([rect(x0, 0, x1, W), hole(pit)], **room)
            m.sector([low], floor=-256, ceil=0, ctex=CEIL, ftex="FLAT14", wall="BROWN1", light=160, id=52)
            lpil = rect(x0 + 352, DY + 352, x0 + 416, DY + 416)
            m.sector([rect(x0, DY, x1, DY + W), hole(low)], floor=-256, ceil=0, ctex="FLAT5_4", ftex="FLAT14", wall="BROWN1", light=160)
            # floor portal: anchor on the pit's south edge, reference on the
            # lower region's (displacement (0, DY)); ceiling portal: anchor on
            # the lower region's west edge, reference on the pit's.
            m.edge((x0 + 256, 256), (x0 + 512, 256), special=57, arg0=51, arg1=0, arg2=0, arg3=0, arg4=0)
            m.edge((x0 + 256, DY + 256), (x0 + 512, DY + 256), special=57, arg0=51, arg1=0, arg2=0, arg3=1)
            m.edge((x0 + 256, DY + 256), (x0 + 256, DY + 512), special=57, arg0=52, arg1=0, arg2=1, arg3=0, arg4=0)
            m.edge((x0 + 256, 256), (x0 + 256, 512), special=57, arg0=52, arg1=0, arg2=1, arg3=1)
            m.thing(x0 + 320, DY + 320, 2028)
            m.thing(x0 + 448, DY + 448, 48)
        elif i == 6:
            m.sector([rect(x0, 0, x1, W)], ceil=192, ctex=SKY)
            p1 = rect(x0 + 128, DY + 384, x0 + 256, DY + 512)
            p2 = rect(x0 + 512, DY + 448, x0 + 640, DY + 576)
            m.sector([rect(x0, DY, x1, DY + W), hole(p1), hole(p2)], ceil=640, ctex=SKY, ftex="FLAT14", wall="BIGBRIK1", light=176)
            m.sector([p1], floor=256, ceil=256, ftex="FLAT5_4", ctex="FLAT5_4", wall="BROWN1", light=176)
            m.sector([p2], floor=448, ceil=448, ftex="FLAT5_4", ctex="FLAT5_4", wall="BROWN1", light=176)
            m.thing(x0 + 384, DY + 128, 9080)
        elif i == 7:
            m.sector([rect(x0, 0, x1, W, xs=mx)], **room)
            m.edge((mx[0], W), (mx[1], W), special=9)
        elif i == 8:
            pil = rect(x0 + 320, 384, x0 + 448, 512)
            m.sector([rect(x0, 0, x1, W), hole(pil)], floor_reflect=0.5, **room)
            m.sector([pil], floor=96, ceil=96, ftex="FLAT5_4", ctex="FLAT5_4", wall="BIGBRIK1")
            m.thing(x0 + 192, 256, 2028)
        elif i == 9:
            m.sector([rect(x0, 0, x1, W)], ceil=256, ctex=CEIL, id=90)
            c = rect(x0 + 256, -400, x0 + 512, -272)
            m.sector([c], floor=-64, ceil=320, ftex="FLAT5_4", ctex="CEIL5_1", wall="GRAY1")
            m.edge((x0 + 256, -400), (x0 + 512, -400), special=57, arg0=90, arg1=4, arg2=1)
        elif i == 11:
            hi = rect(x0 + 256, 256, x0 + 512, 512)
            up = rect(x0 + 256, DY + 256, x0 + 512, DY + 512)
            m.sector([hi], ctex=CEIL, id=110, ceil=192)
            m.sector([rect(x0, 0, x1, W), hole(hi)], **room)
            m.sector([up], floor=192, ceil=448, ftex="FLAT14", ctex="FLAT5_4", wall="BROWN1", light=160, id=111)
            m.sector([rect(x0, DY, x1, DY + W), hole(up)], floor=192, ceil=448, ftex="FLAT14", ctex="FLAT5_4", wall="BROWN1", light=160)
            m.edge((x0 + 256, 256), (x0 + 512, 256), special=57, arg0=110, arg1=0, arg2=1, arg3=0, arg4=0)
            m.edge((x0 + 256, DY + 256), (x0 + 512, DY + 256), special=57, arg0=110, arg1=0, arg2=1, arg3=1)
            m.edge((x0 + 256, DY + 256), (x0 + 256, DY + 512), special=57, arg0=111, arg1=0, arg2=0, arg3=0, arg4=0)
            m.edge((x0 + 256, 256), (x0 + 256, 512), special=57, arg0=111, arg1=0, arg2=0, arg3=1)
            m.thing(x0 + 320, DY + 320, 2028)
            m.thing(x0 + 448, DY + 448, 48)
    m.thing(384, 64, 1, 90)
    return m.textmap()


MAPINFO = b"""
map MAP01 "Portal test"
{
	sky1 = "SKY1"
}

map MAP02 "Portal test (fog)"
{
	sky1 = "SKY1"
}
"""

HOLD = 90


def demo_steps():
    """make_demo.py script steps and the demo length. Each room: hold tics,
    look steps [(tic offset, kind, value)]; 'warp' values are (x off, y)."""
    look = [(1, "face", 90), (20, "face", 70), (35, "face", 110), (50, "face", 90),
            (55, "warp", (384, 520)), (70, "pitch", 20), (85, "pitch", 0)]
    down = [(1, "face", 90), (10, "pitch", -40), (40, "warp", (384, 200)), (45, "pitch", -60), (75, "pitch", 0)]
    up = [(1, "face", 90), (10, "pitch", 40), (50, "warp", (384, 200)), (70, "pitch", 0)]
    rooms = {
        0: (HOLD, look), 1: (HOLD, look), 2: (HOLD, look), 3: (HOLD, look),
        4: (HOLD, [(1, "face", 0), (25, "face", 20), (45, "face", 180), (70, "face", 160)]),
        5: (HOLD, down),
        6: (HOLD, [(1, "face", 90), (10, "pitch", 30), (40, "face", 60), (60, "face", 130), (80, "pitch", 0)]),
        7: (HOLD, look), 8: (HOLD, [(1, "face", 90), (10, "pitch", -20), (40, "face", 60), (70, "pitch", 0)]),
        9: (HOLD, [(1, "face", 90), (10, "pitch", 25), (40, "face", 45), (70, "pitch", 0)]),
        10: (HOLD, look), 11: (HOLD, up),
    }
    steps, t = [], 2
    for i in range(ROOMS):
        hold, looks = rooms[i]
        x0 = i * STRIDE
        steps.append("warp:%d,%d@%d" % (x0 + 384, 64, t))
        steps.append("pitch:0@%d" % (t + 1))
        for dt, kind, v in looks:
            if kind == "warp":
                steps.append("warp:%d,%d@%d" % (x0 + v[0], v[1], t + dt))
            else:
                steps.append("%s:%d@%d" % (kind, v, t + dt))
        t += hold
    return steps, t + 5


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--out", default=os.path.join(here, "wads", "portaltest.wad"))
    ap.add_argument("--demodir", default=os.path.join(here, "demos", "portaltest"))
    args = ap.parse_args()
    lumps = [("MAPINFO", MAPINFO)]
    for name, tm in (("MAP01", build_textmap(False)), ("MAP02", build_textmap(True))):
        lumps += [(name, b""), ("TEXTMAP", tm), ("ENDMAP", b"")]
    write_wad(args.out, lumps)
    print("wrote %s (%d rooms, 2 maps)" % (args.out, ROOMS))

    steps, tics = demo_steps()
    cmds, warps, specials = make_demo.build_script(steps, 90.0, tics)
    os.makedirs(args.demodir, exist_ok=True)
    for name in ("MAP01", "MAP02"):
        data = make_demo.build(name, cmds, 0xA1B2C3D4, ["god", "noclip"], warps, specials, pitch_limit=(90, 90))
        with open(os.path.join(args.demodir, "PORTALTEST_%s.lmp" % name), "wb") as f:
            f.write(data)
    print("wrote %s/PORTALTEST_MAP0{1,2}.lmp (%d tics)" % (args.demodir, tics))


if __name__ == "__main__":
    main()
