#!/usr/bin/env python3
"""
make_transtest.py — generate wads/transtest.wad and its scripted demos
demos/transtest/TRANSTEST_MAP0{1,2}.lmp, the translucency / fog boundary
/ hack fill A/B maps (.scratch/levelmesh-impl ticket 16). Translucent and
FF_FOG 3D floors are in make_3dfloortest.py (rooms 8, 9).

ROOMS separate rooms (768 x 768, x0 = 1024 * i, y 0 .. 768), closed by
one-sided walls; the camera spot is (x0 + 384, 64), looking north. Unless
noted a room has floor 0, CEIL3_5 ceiling at 192, light 192, STARTAN3
walls. Rooms split into east-west strips have a two-sided line at each
strip boundary (y = 256, 512 unless noted).

  0  translucent mid textures with sprites between them: MIDGRATE at
     alpha 0.5 (y 256), MIDBARS3 additive at alpha 0.75 (y 384), an
     opaque MIDGRATE (y 512); a lamp, a barrel and a column between the
     lines (DrawSorted splits / orders them with the walls).
  1  fog boundaries: strips with fadecolor none / 405060 (light 128) /
     806040 (light 160); the line between the fogged strips carries a
     MIDGRATE at alpha 0.5 (RENDERWALL_M2SNF beside the fog sheet).
  2  fake planes (SSRF_PLANEHACK, HandleMissingTextures): three regions
     enclosed by two-sided lines without lower / upper textures:
     - a pit (floor -64, a tech pillar in it, half sunk): the classic draws the rim's
       floor at 0 over it (DoOneSectorLower; the eye is above the rim);
     - a raised-ceiling box (ceiling 256 in a 192 ceiling): the rim's
       ceiling at 192 over it (DoOneSectorUpper; the eye is below);
     - an invisible bridge (floor 64, a barrel on it): the floor at 0 is
       drawn over it (DoFakeBridge; the eye is below its top), so the
       barrel floats.
  3  translucent portal plane: a 256 x 256 stacked floor portal at alpha
     128 (Sector_SetPortal arg4) in the middle, showing a lower room in the
     area north (floor -256, ceiling 0) under the room's own floor.
  4  translucency changed at run time: an opaque MIDGRATE (line 400)
     becomes alpha 0.25 (TranslucentLine), then additive alpha 0.6.
  5  moving: translucent MIDBARS3 (alpha 0.5) on the boundary of a lift
     (tag 105, floor lowers 64 and comes back) and of a ceiling that
     lowers 64 and comes back (tag 106).
  6  stress: eight translucent lines (alternating MIDGRATE 0.5 and
     MIDBARS3 additive 0.5) every 80 units, a lamp between each pair.
  7  flood gaps (SSRF_FLOODHACK, PrepareLowerGap / PrepareUpperGap): two
     strips spanning the room (they touch the one-sided walls, so the
     fake-plane checks fail): a step up to floor 24 (y 256) without a lower
     texture, then a ceiling down to 120 (y 448) without an upper texture.
     The classic floods each gap with the back sector's plane through a
     stencil (the lower one only while the eye is above floor 24).
  8  self-referencing deep water: a 256 x 256 region whose lines have the
     deep sector (floor -64) on both sides. Its subsectors take the
     surrounding sector as render sector, so the floor is drawn at 0 while
     a tech pillar inside sinks to -64 (half sunk).

MAP01: as above. MAP02: the same with lightmode 2 (Doom) set in MAPINFO
(other light-level math, fog boundaries by light level).

Each demo visits the rooms in order (warp, look north, turn, step
closer), running the room's specials; captured every 5 tics.

Usage: make_transtest.py [--out wads/transtest.wad] [--demodir demos/transtest]
"""

import argparse
import os

import make_demo
from make_lighttest import write_wad
from make_skytest import Map, rect, hole

ROOMS = 9
STRIDE = 1024
W = 768
DY = 1280
CEIL = "CEIL3_5"
F = 1 << 16


def strips(m, x0, ys, props):
    """East-west strips of room x0 split at ys; props per strip."""
    x1 = x0 + W
    bounds = [0, *ys, W]
    for k in range(len(bounds) - 1):
        p = dict(ceil=192, ctex=CEIL)
        p.update(props[k])
        m.sector([rect(x0, bounds[k], x1, bounds[k + 1])], **p)


def build_textmap():
    m = Map(False)
    for i in range(ROOMS):
        x0, x1 = i * STRIDE, i * STRIDE + W
        if i == 0:
            strips(m, x0, (256, 384, 512), [{}, {}, {}, {}])
            m.edge((x0, 256), (x1, 256), mid="MIDGRATE", alpha=0.5)
            m.edge((x0, 384), (x1, 384), mid="MIDBARS3", alpha=0.75, renderstyle="add")
            m.edge((x0, 512), (x1, 512), mid="MIDGRATE")
            m.thing(x0 + 300, 320, 2028)	# lamp
            m.thing(x0 + 450, 450, 2035)	# barrel
            m.thing(x0 + 384, 640, 48)		# tech column
        elif i == 1:
            strips(m, x0, (256, 512), [{}, dict(fadecolor=0x405060, lightlevel=128), dict(fadecolor=0x806040, lightlevel=160)])
            m.edge((x0, 512), (x1, 512), mid="MIDGRATE", alpha=0.5)
            m.thing(x0 + 384, 640, 2028)
        elif i == 2:
            pit = rect(x0 + 96, 320, x0 + 288, 512)
            box = rect(x0 + 480, 320, x0 + 672, 512)
            bridge = rect(x0 + 288, 576, x0 + 480, 704)
            m.sector([rect(x0, 0, x1, W), hole(pit), hole(box), hole(bridge)], ceil=192, ctex=CEIL)
            m.sector([pit], floor=-64, ceil=192, ctex=CEIL, ftex="FLAT14")
            m.sector([box], ceil=256, ctex="FLAT5_4")
            m.sector([bridge], floor=64, ceil=192, ctex=CEIL, ftex="FLAT14")
            for r in (pit, box, bridge):
                for k in range(len(r)):
                    m.edge(r[k], r[(k + 1) % len(r)], bottom="-", top="-")
            m.thing(x0 + 192, 416, 48)	# tech pillar in the pit (half sunk)
            m.thing(x0 + 384, 640, 2035)	# barrel on the bridge
        elif i == 3:
            pit = rect(x0 + 256, 256, x0 + 512, 512)
            low = rect(x0 + 256, DY + 256, x0 + 512, DY + 512)
            m.sector([pit], ceil=192, ctex=CEIL, id=31)
            m.sector([rect(x0, 0, x1, W), hole(pit)], ceil=192, ctex=CEIL)
            m.sector([low], floor=-256, ceil=0, ctex=CEIL, ftex="FLAT14", wall="BROWN1", lightlevel=160)
            m.sector([rect(x0, DY, x1, DY + W), hole(low)], floor=-256, ceil=0, ctex="FLAT5_4", ftex="FLAT14", wall="BROWN1",
                     lightlevel=160)
            m.edge((x0 + 256, 256), (x0 + 512, 256), special=57, arg0=31, arg1=0, arg2=0, arg3=0, arg4=128)
            m.edge((x0 + 256, DY + 256), (x0 + 512, DY + 256), special=57, arg0=31, arg1=0, arg2=0, arg3=1)
            m.thing(x0 + 320, DY + 320, 2028)
            m.thing(x0 + 448, DY + 448, 48)
        elif i == 4:
            strips(m, x0, (384,), [{}, {}])
            m.edge((x0, 384), (x1, 384), mid="MIDGRATE", id=400)
            m.thing(x0 + 384, 600, 2028)
        elif i == 5:
            lift = rect(x0 + 128, 320, x0 + 320, 448)
            door = rect(x0 + 448, 320, x0 + 640, 448)
            m.sector([rect(x0, 0, x1, W), hole(lift), hole(door)], ceil=256, ctex=CEIL)
            m.sector([lift], floor=64, ceil=256, ctex=CEIL, wall="BROWN1", id=105)
            m.sector([door], ceil=192, ctex=CEIL, wall="BROWN1", id=106)
            for x in (x0 + 128, x0 + 448):
                m.edge((x, 320), (x + 192, 320), mid="MIDBARS3", alpha=0.5, top="BROWN1", bottom="BROWN1")
            m.thing(x0 + 384, 600, 2028)
        elif i == 6:
            ys = tuple(160 + 80 * k for k in range(8))
            strips(m, x0, ys, [{} for _ in range(9)])
            for k, y in enumerate(ys):
                if k % 2 == 0:
                    m.edge((x0, y), (x1, y), mid="MIDGRATE", alpha=0.5)
                else:
                    m.edge((x0, y), (x1, y), mid="MIDBARS3", alpha=0.5, renderstyle="add")
                if k < 7:
                    m.thing(x0 + 200 + 50 * k, y + 40, 2028)
        elif i == 7:
            ys = (256, 448)
            strips(m, x0, ys, [{}, dict(floor=24), dict(ceil=120)])
            for y in ys:
                m.edge((x0, y), (x1, y), bottom="-", top="-")
            m.thing(x0 + 384, 600, 2028)
        elif i == 8:
            pool = rect(x0 + 256, 256, x0 + 512, 512)
            m.sector([rect(x0, 0, x1, W), hole(pool)], ceil=192, ctex=CEIL)
            deep = m.sector([pool], floor=-64, ceil=192, ctex=CEIL, ftex="FWATER1")
            for k in range(len(pool)):
                m.edge(pool[k], pool[(k + 1) % len(pool)], both_sides=deep, bottom="-", top="-")
            m.thing(x0 + 384, 384, 48)	# tech pillar in the deep water (half sunk)
    m.thing(384, 64, 1, 90)
    return m.textmap()


MAPINFO = b"""
map MAP01 "Translucency test"
{
	sky1 = "SKY1"
}

map MAP02 "Translucency test (lightmode 2)"
{
	sky1 = "SKY1"
	lightmode = 2
}
"""

HOLD = 90


def demo_steps():
    """make_demo.py script steps and the demo length. Each room: hold tics,
    look steps [(tic offset, kind, value)], specials [(tic offset, args)]."""
    look = [(1, "face", 90), (20, "face", 70), (35, "face", 110), (50, "face", 90),
            (55, "warp", (384, 200)), (70, "pitch", -15), (85, "pitch", 0)]
    rooms = {
        0: (HOLD, look, []),
        1: (HOLD, look, []),
        2: (HOLD, look[:4] + [(55, "warp", (384, 200)), (60, "pitch", -15), (75, "pitch", 15), (88, "pitch", 0)], []),
        3: (HOLD, [(1, "face", 90), (10, "warp", (384, 160)), (12, "pitch", -35), (40, "warp", (384, 300)), (45, "pitch", -60), (75, "pitch", 0)], []),
        4: (HOLD, look, [(15, (208, 400, 64, 0)), (50, (208, 400, 160, 1))]),
        5: (130, [(1, "face", 90), (20, "face", 60), (60, "face", 120), (100, "face", 90)],
            [(4, (20, 105, 16, 64)), (60, (23, 105, 16, 64)), (4, (40, 106, 16, 64)), (60, (41, 106, 16, 64))]),
        6: (HOLD, look, []),
        7: (HOLD, look[:4] + [(55, "warp", (384, 200)), (60, "pitch", -10), (70, "warp", (384, 330)), (75, "pitch", 10), (88, "pitch", 0)], []),
        8: (HOLD, look[:4] + [(55, "warp", (384, 160)), (60, "pitch", -25), (85, "pitch", 0)], []),
    }
    steps, t = [], 2
    for i in range(ROOMS):
        hold, looks, specials = rooms[i]
        x0 = i * STRIDE
        steps.append("warp:%d,%d@%d" % (x0 + 384, 64, t))
        steps.append("pitch:0@%d" % (t + 1))
        for dt, kind, v in looks:
            if kind == "warp":
                steps.append("warp:%d,%d@%d" % (x0 + v[0], v[1], t + dt))
            else:
                steps.append("%s:%d@%d" % (kind, v, t + dt))
        for dt, args in specials:
            steps.append("special:%s@%d" % (",".join(str(a) for a in args), t + dt))
        t += hold
    return steps, t + 5


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--out", default=os.path.join(here, "wads", "transtest.wad"))
    ap.add_argument("--demodir", default=os.path.join(here, "demos", "transtest"))
    args = ap.parse_args()
    tm = build_textmap()
    lumps = [("MAPINFO", MAPINFO)]
    for name in ("MAP01", "MAP02"):
        lumps += [(name, b""), ("TEXTMAP", tm), ("ENDMAP", b"")]
    write_wad(args.out, lumps)
    print("wrote %s (%d rooms, 2 maps)" % (args.out, ROOMS))

    steps, tics = demo_steps()
    cmds, warps, specials = make_demo.build_script(steps, 90.0, tics)
    os.makedirs(args.demodir, exist_ok=True)
    for name in ("MAP01", "MAP02"):
        data = make_demo.build(name, cmds, 0xA1B2C3D4, ["god", "noclip"], warps, specials, pitch_limit=(90, 90))
        with open(os.path.join(args.demodir, "TRANSTEST_%s.lmp" % name), "wb") as f:
            f.write(data)
    print("wrote %s/TRANSTEST_MAP0{1,2}.lmp (%d tics)" % (args.demodir, tics))


if __name__ == "__main__":
    main()
