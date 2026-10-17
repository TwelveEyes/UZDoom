#!/usr/bin/env python3
"""
make_skytest.py — generate wads/skytest.wad and its scripted demos
demos/skytest/SKYTEST_MAP0{1,2,3}.lmp, the sky A/B maps
(.scratch/levelmesh-impl ticket 14).

ROOMS separate rooms (768 x 768, x = 1024 * i), closed by one-sided walls,
so each room's sky rules are seen alone. The camera spot is
(x0 + 384, 64), looking north. Unless noted, a room is a courtyard: floor
0, F_SKY1 ceiling at 192, light 192, STARTAN3 walls (the classic draws a
sky wall above every one-sided wall: HWWall::SkyNormal, to z 32768).

  0  open sky: a low pillar (ceiling 96, CEIL3_5: an upper tier under the
     sky, a sky wall above it, HWWall::SkyTop) and a tall closed pillar
     (floor = ceiling 320: a lower tier that rises above the sky ceiling,
     with a sky wall on the same faces above 192). The camera looks up.
  1  sky hack: the south half's sky ceiling is 256, the north half's 128
     (no upper tier between two sky ceilings, no sky wall: the sky shows
     down to 128). A closed building (224) in the north half rises above
     the 128 sky: SkyTop puts a sky wall on its faces above 128.
  2  sky doors: two doors in the north wall, closed at load: a sky-ceiling
     door (sky-sky closed: a sky wall from the door's ceiling up,
     HWF_SKYHACK) and a CEIL5_1 door (a sky wall above the courtyard's
     sky). Behind them a sky corridor with a tall pillar. Door_Open on
     both, then Door_Close.
  3  noskywalls: the north part (y 448 .. 768) has a CEIL5_1 ceiling at
     320. The boundary has a sky wall above 192 on its east half; the west
     half's line is noskywalls (the north part shows above the sky).
  4  sky floor: a pit (x0 + 256 .. x0 + 512, y 256 .. 512, floor -48,
     F_SKY1: HWWall::SkyBottom, the floor sky portal); the camera (y 240,
     at the pit's edge) looks down.
  5  sky-hack mid texture: as room 1, the dividing line carries a
     two-sided MIDGRATE (HWF_SKYHACK mid textures, GLDL_MASKEDWALLSOFS).
  6  MBF sky transfer: the west half takes its sky from line 650's upper
     texture (SKY3, Static_Init 255), the east half from line 651's (SKY2,
     flipped). The demo scrolls line 650 (Scroll_Wall): a moving sky.
  7  sector sky textures: UDMF skyceiling = "SKY2" on the west half,
     "SKY3" on the east half (sector_t::planes[].skytexture).
  8  moving sky ceiling: CEIL3_5 courtyard (ceiling 256) with a sky sector
     in the middle (ceiling 192, tag 108) that lowers 96 and comes back.

The transfer lines of room 6 sit in a closed control sector south of it.

MAP01: sky1 SKY1, still. MAP02: doublesky with SKY1 and SKY3, both
scrolling. MAP03: skyfog 160 with fadecolor 405060 on every sector (the
sky fog rule of HWSkyInfo::init) and forcenoskystretch.

Each demo visits the rooms in order (warp, look north, look up / down,
turn), running the room's specials; captured every 5 tics. The demos send
the pitch limits (make_demo.py pitch_limit): without them every pitch
step is clamped to 0. r_skymode is a
cvar: run the A/B with --set-a/--set-b 'r_skymode 1' for the other mode.

Usage: make_skytest.py [--out wads/skytest.wad] [--demodir demos/skytest]
"""

import argparse
import os

import make_demo
from make_3dfloortest import block
from make_lighttest import write_wad

ROOMS = 9
STRIDE = 1024
W = 768
SKY = "F_SKY1"
LIGHT = 192
FADE = "405060"
F = 1 << 16


class Map:
    """Sectors as polygons (counter-clockwise rings; holes clockwise). An
    edge shared by two sectors (opposite directions) becomes a two-sided
    line, any other edge a one-sided wall facing into its sector."""

    def __init__(self, fog):
        self.fog = fog
        self.verts, self.vindex = [], {}
        self.sectors, self.rings = [], []
        self.edge_props = {}
        self.extra_lines, self.things = [], []

    def vert(self, x, y):
        k = (x, y)
        if k not in self.vindex:
            self.vindex[k] = len(self.verts)
            self.verts.append(k)
        return self.vindex[k]

    def sector(self, rings, floor=0, ceil=192, ftex="FLOOR0_1", ctex=SKY, light=LIGHT, wall="STARTAN3", **extra):
        props = {"heightfloor": floor, "heightceiling": ceil, "texturefloor": ftex, "textureceiling": ctex,
                 "lightlevel": light}
        if self.fog:
            props["fadecolor"] = int(FADE, 16)
        props.update(extra)
        self.sectors.append(props)
        s = len(self.sectors) - 1
        for ring in rings:
            for k in range(len(ring)):
                self.rings.append((s, ring[k], ring[(k + 1) % len(ring)], wall))
        return s

    def edge(self, a, b, **props):
        """Line / side properties of the undirected edge a-b: line keys,
        'mid' / 'top' / 'bottom' for both sides, and 'both_sides' (a sector
        index) to make a two-sided edge self-referencing (that sector on both
        sides)."""
        self.edge_props[frozenset((a, b))] = props

    def thing(self, x, y, type, angle=0):
        self.things.append({"x": float(x), "y": float(y), "angle": angle, "type": type, "skill1": True,
                            "skill2": True, "skill3": True, "skill4": True, "skill5": True, "single": True})

    def textmap(self):
        by_edge = {}
        for s, a, b, wall in self.rings:
            by_edge.setdefault(frozenset((a, b)), []).append((s, a, b, wall))
        sides, lines = [], []

        def side(sector, mid="-", top="-", bottom="-"):
            sides.append({"sector": sector, "texturemiddle": mid, "texturetop": top, "texturebottom": bottom})
            return len(sides) - 1

        for key, users in by_edge.items():
            props = dict(self.edge_props.get(key, {}))
            mid, top, bottom = props.pop("mid", None), props.pop("top", None), props.pop("bottom", None)
            both = props.pop("both_sides", None)
            if len(users) == 1:
                s, a, b, wall = users[0]
                # a -> b runs counter-clockwise around s: s is on the left,
                # the front (right) side of b -> a.
                l = {"v1": self.vert(*b), "v2": self.vert(*a), "sidefront": side(s, mid or wall, top or "-"),
                     "blocking": True}
            elif len(users) == 2:
                (s1, a, b, w1), (s2, _, _, w2) = users
                if both is not None:
                    s1 = s2 = both
                l = {"v1": self.vert(*b), "v2": self.vert(*a), "twosided": True,
                     "sidefront": side(s1, mid or "-", top or w1, bottom or w1),
                     "sideback": side(s2, mid or "-", top or w2, bottom or w2)}
            else:
                raise ValueError("edge %s shared by %d sectors" % (sorted(key), len(users)))
            l.update(props)
            lines.append(l)
        for l in self.extra_lines:
            lines.append(l)

        out = ['namespace = "zdoom";\n']
        for t in self.things:
            out.append(block("thing", t))
        for x, y in self.verts:
            out.append(block("vertex", {"x": float(x), "y": float(y)}))
        for l in lines:
            out.append(block("linedef", l))
        for s in sides:
            out.append(block("sidedef", s))
        for s in self.sectors:
            out.append(block("sector", s))
        return "".join(out).encode("ascii")


def rect(x0, y0, x1, y1, xs=(), ys=()):
    """Counter-clockwise rectangle, with extra vertices on the south / north
    edges at xs and on the west / east edges at ys (shared edges must have
    the same vertices on both sides)."""
    south = [(x, y0) for x in sorted({x0, x1, *xs}) if x0 <= x <= x1]
    east = [(x1, y) for y in sorted({y0, y1, *ys}) if y0 < y < y1]
    north = [(x, y1) for x in sorted({x0, x1, *xs}, reverse=True) if x0 <= x <= x1]
    west = [(x0, y) for y in sorted({y0, y1, *ys}, reverse=True) if y0 < y < y1]
    return south + east + north + west


def hole(ring):
    return list(reversed(ring))


def build_textmap(fog):
    m = Map(fog)
    for i in range(ROOMS):
        x0, x1 = i * STRIDE, i * STRIDE + W
        if i == 0:
            low = rect(x0 + 160, 400, x0 + 288, 528)
            tall = rect(x0 + 480, 400, x0 + 608, 528)
            m.sector([rect(x0, 0, x1, W), hole(low), hole(tall)])
            m.sector([low], ceil=96, ctex="CEIL3_5", wall="BROWN1")
            m.sector([tall], floor=320, ceil=320, ftex="FLAT5_4", ctex="FLAT5_4", wall="BROWN1")
        elif i in (1, 5):
            south = rect(x0, 0, x1, 384)
            bld = rect(x0 + 320, 560, x0 + 448, 688)
            m.sector([south], ceil=256)
            m.sector([rect(x0, 384, x1, W), hole(bld)], ceil=128)
            m.sector([bld], floor=224, ceil=224, ftex="FLAT5_4", ctex="FLAT5_4", wall="BIGBRIK1")
            if i == 5:
                m.edge((x0, 384), (x1, 384), mid="MIDGRATE")
        elif i == 2:
            ds, dn = (x0 + 128, x0 + 256), (x0 + 512, x0 + 640)
            xs = (*ds, *dn)
            m.sector([rect(x0, 0, x1, 512, xs=xs)])
            m.sector([rect(ds[0], 512, ds[1], 528)], ceil=0, wall="BIGDOOR2", id=102)
            m.sector([rect(dn[0], 512, dn[1], 528)], ceil=0, ctex="FLAT20", wall="BIGDOOR2", id=103)
            pil = rect(x0 + 352, 640, x0 + 416, 704)
            m.sector([rect(x0, 528, x1, W, xs=xs), hole(pil)])
            m.sector([pil], floor=288, ceil=288, ftex="FLAT5_4", ctex="FLAT5_4", wall="BROWN1")
            # Between the doors the courtyard's and the corridor's walls are
            # one-sided (y 512 and y 528 are different edges).
        elif i == 3:
            mid = x0 + 384
            m.sector([rect(x0, 0, x1, 448, xs=(mid,))])
            m.sector([rect(x0, 448, x1, W, xs=(mid,))], ceil=320, ctex="CEIL5_1", wall="BROWN96")
            m.edge((x0, 448), (mid, 448), noskywalls=True)
        elif i == 4:
            pit = rect(x0 + 256, 256, x0 + 512, 512)
            m.sector([rect(x0, 0, x1, W), hole(pit)])
            m.sector([pit], floor=-48, ftex=SKY, wall="GRAY1")
        elif i == 6:
            mid = x0 + 384
            m.sector([rect(x0, 0, mid, W)], id=600)
            m.sector([rect(mid, 0, x1, W)], id=601)
            # control sector south of the room: the transfer lines
            c = rect(x0 + 256, -400, x0 + 512, -272)
            m.sector([c], ceil=128, ctex="FLAT20", wall="GRAY1")
            m.edge((x0 + 256, -400), (x0 + 512, -400), top="SKY3", special=190, arg0=600, arg1=255, arg2=0, id=650)
            m.edge((x0 + 512, -272), (x0 + 256, -272), top="SKY2", special=190, arg0=601, arg1=255, arg2=1, id=651)
        elif i == 7:
            mid = x0 + 384
            m.sector([rect(x0, 0, mid, W)], skyceiling="SKY2")
            m.sector([rect(mid, 0, x1, W)], skyceiling="SKY3")
        elif i == 8:
            inner = rect(x0 + 256, 256, x0 + 512, 512)
            m.sector([rect(x0, 0, x1, W), hole(inner)], ceil=256, ctex="CEIL3_5")
            m.sector([inner], ceil=192, wall="BROWN1", id=108)
    m.thing(384, 64, 1, 90)
    return m.textmap()


MAPINFO = b"""
map MAP01 "Sky test (sky1 still)"
{
	sky1 = "SKY1"
}

map MAP02 "Sky test (doublesky, scrolling)"
{
	sky1 = "SKY1", 1.0
	sky2 = "SKY3", 2.5
	doublesky
}

map MAP03 "Sky test (skyfog, fog, tiled sky)"
{
	sky1 = "SKY1"
	skyfog = 160
	forcenoskystretch
}
"""

HOLD = 80


def demo_steps():
    """make_demo.py script steps and the demo length. Each entry: room ->
    (hold tics, look steps [(tic offset, kind, degrees)], specials
    [(tic offset, special args)][, camera y])."""
    look = [(1, "face", 90), (20, "pitch", 30), (40, "pitch", 0), (41, "face", 60), (60, "face", 120)]
    rooms = {
        0: (HOLD, look, []),
        1: (HOLD, look, []),
        2: (150, [(1, "face", 90), (30, "pitch", 20), (110, "pitch", 0)],
            [(4, (11, 102, 32)), (4, (11, 103, 32)), (80, (10, 102, 32)), (80, (10, 103, 32))]),
        3: (HOLD, look, []),
        4: (HOLD, [(1, "face", 90), (20, "pitch", -45), (50, "pitch", 0), (51, "face", 60)], [], 240),
        5: (HOLD, look, []),
        6: (100, look + [(80, "face", 90)], [(4, (52, 650, 4 * F, 0, 0, 1))]),
        7: (HOLD, look, []),
        8: (130, [(1, "face", 90), (10, "pitch", 25), (120, "pitch", 0)],
            [(4, (40, 108, 16, 96)), (64, (41, 108, 16, 96))]),
    }
    steps, t = [], 2
    for i in range(ROOMS):
        hold, looks, specials = rooms[i][:3]
        y = rooms[i][3] if len(rooms[i]) > 3 else 64
        steps.append("warp:%d,%d@%d" % (i * STRIDE + 384, y, t))
        steps.append("pitch:0@%d" % (t + 1))
        for dt, kind, deg in looks:
            steps.append("%s:%d@%d" % (kind, deg, t + dt))
        for dt, args in specials:
            steps.append("special:%s@%d" % (",".join(str(a) for a in args), t + dt))
        t += hold
    return steps, t + 5


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--out", default=os.path.join(here, "wads", "skytest.wad"))
    ap.add_argument("--demodir", default=os.path.join(here, "demos", "skytest"))
    args = ap.parse_args()
    plain, fog = build_textmap(False), build_textmap(True)
    lumps = [("MAPINFO", MAPINFO)]
    for name, tm in (("MAP01", plain), ("MAP02", plain), ("MAP03", fog)):
        lumps += [(name, b""), ("TEXTMAP", tm), ("ENDMAP", b"")]
    write_wad(args.out, lumps)
    print("wrote %s (%d rooms, 3 maps)" % (args.out, ROOMS))

    steps, tics = demo_steps()
    cmds, warps, specials = make_demo.build_script(steps, 90.0, tics)
    os.makedirs(args.demodir, exist_ok=True)
    for name in ("MAP01", "MAP02", "MAP03"):
        data = make_demo.build(name, cmds, 0xA1B2C3D4, ["god", "noclip"], warps, specials, pitch_limit=(90, 90))
        path = os.path.join(args.demodir, "SKYTEST_%s.lmp" % name)
        with open(path, "wb") as f:
            f.write(data)
    print("wrote %s/SKYTEST_MAP0{1,2,3}.lmp (%d tics)" % (args.demodir, tics))


if __name__ == "__main__":
    main()
