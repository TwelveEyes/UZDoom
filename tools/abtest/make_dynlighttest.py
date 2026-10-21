#!/usr/bin/env python3
"""
make_dynlighttest.py — generate wads/dynlighttest.wad, its scripted demos
demos/dynlighttest/DYNLIGHTTEST_MAP*.lmp and ab_manifest_dynlighttest.json,
the dynamic-light A/B maps (.scratch/levelmesh-impl ticket 19).

Four areas, each one map layout with map-placed dynamic lights
(wadsrc/static/mapinfo/common.txt editor numbers, DynamicLight classes in
wadsrc/static/zscript/actors/shared/dynlights.zs). Point lights: arg0..2 =
r, g, b, arg3 = radius. Spot lights (P_SpawnMapThing reshuffles their args):
arg0 = 0xRRGGBB, arg1 / arg2 = inner / outer cone angle, arg3 = radius; the
thing's angle and pitch (positive = down) aim the cone. Light z = the
thing's height above the floor. Sectors are light 128..144, so the lights
show.

  area 1  light types: point (9800), additive (9810), subtractive (9820),
          attenuated (9830), spot (9840), additive spot (9850), attenuated
          spot (9870); radii 64 .. 448. A platform (floor 32) north of
          y 640; one light just north of the step (lights both floors, not
          the step's south-facing lower tier), one just south of it below
          the platform floor (lights the lower tier, not the platform).
  area 2  reachability: rooms A and B separated by a 64-unit solid block
          (one-sided walls), a light 8 units in front of B's west wall
          (must not light A); a low-ceiling pocket in A (ceiling 96) with a
          light at z 128, above its ceiling; a corridor loop of four
          sectors (floors 0 / 8 / 16 / 8) round a solid core, lights in two
          corners and on the north side.
  area 3  3D floors: a box sector (tag 300) with two solid 3D floors
          (Sector_Set3DFloor, as make_3dfloortest.py), bands 96..128 and
          224..256; lights below, between and above the bands, one outside
          the box at band height, a spot from the north. A balcony (floor
          240) looks down on the band tops.
  area 4  linked line portals (Line_SetPortal type 3, both ways, as
          make_portaltest.py room 2): room A and area B (y 1280 ..);
          lights within radius of each portal line, a spot in B aimed at
          the portal.

Every vantage is its own map lump (same layout, its own player start), so
abtest.py's static mode (+map, no input) captures it: lump MAP<v><a>, v =
vantage 0..3, a = area 1..4 (MAP01..MAP04 are vantage 0). VANTAGES lists
the starts. Each vantage's demo holds still, turns 20 degrees left, 20
right and back (demo mode); the lights are static, so every capture of a
static run shows the same frame.

Usage: make_dynlighttest.py [--out wads/dynlighttest.wad] [--demodir demos/dynlighttest]
                            [--manifest ab_manifest_dynlighttest.json]
"""

import argparse
import json
import os

import make_demo
from make_lighttest import write_wad
from make_skytest import Map, rect, hole

LIGHT = 128
CEIL = "CEIL3_5"
FLOOR = "FLOOR0_1"
DY = 1280

# editor numbers (wadsrc/static/mapinfo/common.txt)
POINT, POINT_ADD, POINT_SUB, POINT_ATTEN = 9800, 9810, 9820, 9830
SPOT, SPOT_ADD, SPOT_SUB, SPOT_ATTEN = 9840, 9850, 9860, 9870

DEMO_TICS = 70
STATIC_TICKS = [1, 2, 4, 8, 16, 35, 70]


def light(kind, x, y, height, rgb, radius, angle=0, pitch=0, cone=None):
    """A map light thing. cone = (inner, outer) degrees for spot lights."""
    t = {"x": float(x), "y": float(y), "height": float(height), "angle": angle, "type": kind,
         "skill1": True, "skill2": True, "skill3": True, "skill4": True, "skill5": True, "single": True}
    if cone:
        r, g, b = rgb
        t.update(arg0=(r << 16) | (g << 8) | b, arg1=cone[0], arg2=cone[1], arg3=radius)
    else:
        t.update(arg0=rgb[0], arg1=rgb[1], arg2=rgb[2], arg3=radius)
    if pitch:
        t["pitch"] = pitch
    return t


def area1():
    m = Map(False)
    room = dict(ceil=192, ctex=CEIL, ftex=FLOOR, light=LIGHT)
    m.sector([rect(0, 0, 1536, 640)], wall="STARTAN3", **room)
    m.sector([rect(0, 640, 1536, 1024)], floor=32, ceil=192, ctex=CEIL, ftex="FLOOR4_8", light=LIGHT, wall="GRAY1")
    lights = [
        light(POINT, 256, 400, 48, (255, 96, 64), 128),
        light(POINT_ADD, 576, 400, 64, (64, 255, 96), 160),
        light(POINT_SUB, 896, 400, 48, (160, 160, 160), 192),
        light(POINT_ATTEN, 1280, 400, 72, (96, 128, 255), 256),
        # straddling the step (two-sided line y 640, platform floor 32)
        light(POINT, 448, 652, 24, (255, 224, 128), 192),
        light(POINT_ATTEN, 1024, 628, 16, (255, 128, 255), 160),
        # small radius near the south wall
        light(POINT, 1152, 16, 96, (255, 255, 255), 64),
        light(SPOT, 768, 96, 128, (255, 128, 32), 448, angle=90, pitch=20, cone=(15, 30)),
        light(SPOT_ATTEN, 1408, 160, 96, (64, 192, 255), 384, angle=0, cone=(10, 25)),
        light(SPOT_ADD, 128, 960, 64, (128, 255, 128), 320, angle=0, pitch=10, cone=(20, 35)),
    ]
    vantages = [(768, 32, 90), (64, 400, 0), (1472, 992, 225), (1100, 560, 315)]
    return m, lights, vantages


def area2():
    m = Map(False)
    # room A with a low-ceiling pocket (x 256 .. 512, y 576 .. 768) open on
    # three sides
    ring_a = [(0, 0), (768, 0), (768, 768), (512, 768), (512, 576), (256, 576), (256, 768), (0, 768)]
    m.sector([ring_a], ceil=192, ctex=CEIL, ftex=FLOOR, light=LIGHT, wall="STARTAN3")
    m.sector([rect(256, 576, 512, 768)], ceil=96, ctex="CEIL5_1", ftex=FLOOR, light=LIGHT, wall="STARTAN3")
    # room B: x 832 .. 1600, the void x 768 .. 832 is the solid wall
    m.sector([rect(832, 0, 1600, 768)], ceil=192, ctex=CEIL, ftex="FLOOR4_8", light=144, wall="BROWN1")
    # corridor loop round a solid core (x 128 .. 1472, y -512 .. -192)
    cor = dict(ceil=128, ctex="CEIL5_1", light=LIGHT, wall="GRAY1")
    m.sector([rect(0, -640, 1600, -512, xs=(128, 1472))], floor=0, ftex=FLOOR, **cor)
    m.sector([rect(1472, -512, 1600, -192)], floor=8, ftex="FLOOR4_8", **cor)
    m.sector([rect(0, -192, 1600, -64, xs=(128, 1472))], floor=16, ftex=FLOOR, **cor)
    m.sector([rect(0, -512, 128, -192)], floor=8, ftex="FLOOR4_8", **cor)
    lights = [
        light(POINT, 840, 384, 64, (255, 64, 64), 256),       # 8 units in front of B's west wall
        light(POINT, 384, 704, 128, (255, 160, 64), 192),     # above the pocket's ceiling (96)
        light(POINT, 192, 192, 48, (128, 128, 255), 160),
        light(POINT, 64, -576, 48, (64, 255, 255), 384),
        light(POINT_ADD, 1536, -128, 48, (255, 64, 255), 384),
        light(POINT_ATTEN, 800, -128, 48, (255, 255, 128), 256),
    ]
    vantages = [(384, 64, 90), (600, 384, 0), (1500, 384, 180), (64, -256, 270)]
    return m, lights, vantages


def area3():
    m = Map(False)
    box = rect(320, 320, 704, 640)
    m.sector([rect(0, 0, 1024, 896), hole(box)], ceil=320, ctex=CEIL, ftex=FLOOR, light=LIGHT, wall="STARTAN3")
    m.sector([box], ceil=320, ctex=CEIL, ftex=FLOOR, light=LIGHT, wall="STARTAN3", id=300)
    m.sector([rect(0, 896, 1024, 1024)], floor=240, ceil=320, ctex=CEIL, ftex="FLOOR4_8", light=LIGHT, wall="GRAY1")
    # control sectors: Sector_Set3DFloor (160) tag 300, type 1 (solid), alpha 255
    for k, (bottom, top, lt) in enumerate(((96, 128, 128), (224, 256, 96))):
        cx = 96 + 160 * k
        m.sector([rect(cx, -200, cx + 64, -136)], floor=bottom, ceil=top, ftex="FLAT5_4", ctex="FLAT20", light=lt,
                 wall="METAL")
        m.edge((cx, -200), (cx + 64, -200), special=160, arg0=300, arg1=1, arg2=0, arg3=255)
    lights = [
        light(POINT, 512, 480, 176, (255, 200, 80), 192),     # between the bands
        light(POINT, 420, 420, 40, (80, 160, 255), 160),      # below band 1
        light(POINT_ADD, 600, 560, 290, (128, 255, 128), 160),  # above band 2
        light(POINT_ATTEN, 280, 480, 176, (255, 96, 96), 192),  # outside the box at band height
        light(SPOT, 512, 880, 200, (255, 255, 255), 512, angle=270, pitch=25, cone=(12, 28)),
    ]
    vantages = [(512, 96, 90), (512, 960, 270), (96, 480, 0)]
    return m, lights, vantages


def area4():
    m = Map(False)
    room = dict(ceil=192, ctex=CEIL, ftex=FLOOR, light=LIGHT)
    dest = dict(ceil=192, ctex="FLAT5_4", ftex="FLAT14", light=144)
    mx = (256, 512)
    m.sector([rect(0, 0, 768, 768, xs=mx)], wall="STARTAN3", **room)
    m.edge((mx[0], 768), (mx[1], 768), special=156, arg0=902, arg2=3, id=903)
    m.sector([rect(0, DY, 768, DY + 768, xs=mx)], wall="BROWN1", **dest)
    m.edge((mx[0], DY), (mx[1], DY), special=156, arg0=903, arg2=3, id=902)
    # Traversable portals need a back sector (FLevelLocals::UpdatePortal):
    # a thin one behind each line, never seen.
    m.sector([rect(mx[0], 768, mx[1], 784)], wall="STARTAN3", **room)
    m.sector([rect(mx[0], DY - 16, mx[1], DY)], wall="BROWN1", **dest)
    lights = [
        light(POINT, 384, 704, 64, (255, 128, 64), 256),      # 64 units south of A's portal
        light(POINT_ADD, 300, DY + 56, 80, (64, 255, 128), 224),  # 56 units north of B's portal
        light(SPOT, 384, DY + 600, 96, (128, 128, 255), 512, angle=270, pitch=10, cone=(15, 30)),
    ]
    vantages = [(384, 128, 90), (384, DY + 700, 270), (700, 400, 150)]
    return m, lights, vantages


AREAS = {1: area1, 2: area2, 3: area3, 4: area4}
TITLES = {1: "light types", 2: "reachability", 3: "3D floors", 4: "line portals"}


def player(x, y, angle):
    return {"x": float(x), "y": float(y), "angle": angle, "type": 1, "skill1": True, "skill2": True,
            "skill3": True, "skill4": True, "skill5": True, "single": True}


def lump_name(area, vantage):
    return "MAP%d%d" % (vantage, area)


def build_maps():
    """[(lump, area, vantage, (x, y, angle), textmap)]"""
    out = []
    for a, fn in AREAS.items():
        m, lights, vantages = fn()
        for v, start in enumerate(vantages):
            m.things = [player(*start)] + lights
            out.append((lump_name(a, v), a, v, start, m.textmap()))
    return out


def mapinfo(maps):
    parts = []
    for lump, a, v, start, _ in maps:
        parts.append('map %s "Dynamic light test: %s, vantage %d"\n{\n\tsky1 = "SKY1"\n}\n' % (lump, TITLES[a], v))
    return "\n".join(parts).encode("ascii")


def demo_steps(angle):
    return ["face:%d@20" % ((angle + 20) % 360), "face:%d@40" % ((angle - 20) % 360),
            "face:%d@55" % angle], DEMO_TICS


def write_demos(demodir, prefix, maps):
    os.makedirs(demodir, exist_ok=True)
    for lump, _, _, (x, y, angle), _ in maps:
        steps, tics = demo_steps(angle)
        cmds, warps, specials = make_demo.build_script(steps, float(angle), tics)
        data = make_demo.build(lump, cmds, 0xA1B2C3D4, ["god", "noclip"], warps, specials)
        with open(os.path.join(demodir, "%s_%s.lmp" % (prefix, lump)), "wb") as f:
            f.write(data)


def write_manifest(path, here, prefix, pwad, demo_dir, maps, comment):
    with open(os.path.join(here, "ab_manifest_lighttest.json")) as f:
        man = json.load(f)
    man["ab_backends"] = ["vulkan"]
    man["resolution"] = [2560, 1440]
    man["extra_files"] = ["lights.pk3"]
    man["determinism_map"] = "%s_%s" % (prefix, maps[0][0])
    man["static_ticks_default"] = STATIC_TICKS
    man["comment"] = comment
    man["demo_dir"] = demo_dir
    last = DEMO_TICS - 5
    man["demo_ticks"] = {"start": 5, "step": 5, "last": last}
    man["maps"] = [{"name": "%s_%s" % (prefix, lump), "iwad": "DOOM2.WAD", "pwad": pwad, "warp": lump,
                    "demo": "%s_%s.lmp" % (prefix, lump), "static_ticks": None} for lump, *_ in maps]
    with open(path, "w") as f:
        json.dump(man, f, indent=1)
        f.write("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--out", default=os.path.join(here, "wads", "dynlighttest.wad"))
    ap.add_argument("--demodir", default=os.path.join(here, "demos", "dynlighttest"))
    ap.add_argument("--manifest", default=os.path.join(here, "ab_manifest_dynlighttest.json"))
    args = ap.parse_args()

    maps = build_maps()
    maps.sort(key=lambda e: (e[2], e[1]))
    lumps = [("MAPINFO", mapinfo(maps))]
    for lump, _, _, _, tm in maps:
        lumps += [(lump, b""), ("TEXTMAP", tm), ("ENDMAP", b"")]
    write_wad(args.out, lumps)
    print("wrote %s (%d maps)" % (args.out, len(maps)))
    for lump, a, v, start, _ in maps:
        print("  %s  area %d (%s) vantage %d at (%d, %d) angle %d" % (lump, a, TITLES[a], v, *start))

    write_demos(args.demodir, "DYNLIGHTTEST", maps)
    print("wrote %s/DYNLIGHTTEST_MAP*.lmp (%d tics each)" % (args.demodir, DEMO_TICS))

    write_manifest(args.manifest, here, "DYNLIGHTTEST", "dynlighttest.wad", "demos/dynlighttest", maps,
                   "Dynamic-light A/B (.scratch/levelmesh-impl ticket 19): wads/dynlighttest.wad, demos/dynlighttest/ "
                   "and this manifest (all from make_dynlighttest.py). Four areas (light types, reachability through a "
                   "solid wall / above a ceiling / round a corridor loop, 3D-floor bands, linked line portals), each "
                   "vantage its own map lump (MAP<vantage><area>) for static mode; the demos hold, turn 20 degrees "
                   "each way and back. lights.pk3 is loaded (extra_files). Same tolerance as ab_manifest.json.")
    print("wrote %s" % args.manifest)


if __name__ == "__main__":
    main()
