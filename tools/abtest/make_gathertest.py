#!/usr/bin/env python3
"""
make_gathertest.py — generate wads/gathertest.wad, its scripted demos
demos/gathertest/GATHERTEST_MAP0{1,2,3,4}.lmp and ab_manifest_gathertest.json,
the actor / particle gather A/B maps (.scratch/levelmesh-impl ticket 17).

Every monster carries the UDMF `dormant` flag: it stands in its first spawn
frame (Deactivate sets tics = -1), never looks, never moves, never fights.
Monsters, pillars, lamps and barrels are the actors the gather has to find;
the ParticleFountain things (DoomEdNum 9027-9033, not dormant: the fountain
runs from load) feed Level->Particles / ParticlesInSubsec.

MAP01: ROOMS separate rooms (768 x 768, x0 = 1024 * i, y 0 .. 768), closed by
one-sided walls. Unless noted floor 0, ceiling 192, light 192, STARTAN3.

  0  open room: 12 monsters of mixed size at varied distances, tech pillars,
     lamp columns, barrels. The camera (x0 + 384, 64) looks north and sweeps.
  1  doorway reveal: a corridor (y 64 .. 288, ceiling 128) with a T-shaped
     side room behind a 128 wide opening in its north wall; 8 monsters in the
     wide part. The camera walks east along the corridor at 16 units per tic
     (scripted per-tic warps), then back west at 24, always looking north:
     the monsters appear and disappear through the opening. Latency probe,
     captured every tic.
  2  pit: a -128 pit behind a ledge; the camera stands near the ledge and
     sees only the upper body of the cyberdemon in the pit, the pit floor
     is hidden; the other pit monsters are fully occluded. Monsters on the
     ledge beyond.
  3  particles: ceiling 320, light 200; fountains in view, one behind a
     solid pillar, one beside / behind the camera (a turn brings it into
     view); r_particles at its default (on).
  4  fast turn: 24 monsters in two rings around the camera; a full turn at
     20 degrees per tic, then 40 per tic, then a slow one. Latency probe,
     captured every tic.
  5  / 6  teleport pair: two monster rooms; the camera jumps A -> B -> A -> B
     (warp + a different facing); captured every tic after each jump. Latency
     probe.
  7  line portal (portaltest room 0): the destination area north holds 4
     monsters; two more in the room.
  8  mirror (portaltest room 3, a wider mirror, no solid pillar): 5 monsters
     in the mirrored room, a tech pillar, a lamp.
  9  sector stack (portaltest room 5): a floor portal in the middle shows the
     lower room (floor -256) in the area north; monsters on both sides near
     the portal edges (sector portal thing lists), the camera looks down.

MAP02: a 2048 x 2048 arena: a 2048 x 1536 floor (ceiling 512) with 48 pillars
and ~370 monsters on a jittered grid, a 256 wide gap in its north wall into a
long hall (y 1568 .. 2048) with three large occluders and ~40 monsters. A
slow camera sweep, a wall stare, a diagonal walk and a walk down the hall.

MAP03 / MAP04: the same 1024 x 1024 room with 20 monsters in view and the
same 1100 tic demo (a gentle sway); MAP04 adds 2000 closed decoy rooms (a
monster in each) far to the east, never visible: the level-size scaling pair
for perf runs (run_matrix.sh --demos demos/gathertest --tics 1050 --monsters).

The manifest sets "monsters": true: abtest.py then omits -nomonsters.

Captured every 5 tics, every tic around the latency probes (rooms 1, 4, 5 / 6
after each jump; the manifest's "dense" ranges). The manifest is written next
to the wad (ab_manifest_gathertest.json), tick lists from the demo scripts.

Usage: make_gathertest.py [--out wads/gathertest.wad] [--demodir demos/gathertest]
                          [--manifest ab_manifest_gathertest.json]
"""

import argparse
import json
import math
import os
import random

import make_demo
from make_lighttest import write_wad
from make_skytest import Map, rect, hole

ROOMS = 10
STRIDE = 1024
W = 768
DY = 1280
CEIL = "CEIL3_5"
SEED = 0x6A7E17

# Monsters (DoomEdNum): small, medium, large; 7 (spider mastermind, radius 128) singly.
SMALL = (3004, 3001, 9)
MEDIUM = (3002, 3005, 3003, 69, 66, 64)
LARGE = (67, 68, 16, 71)
MIXED = (3004, 3002, 67, 3001, 3005, 16, 9, 3003, 68, 69, 66, 71, 64)

TECH_PILLAR, LAMP, BARREL = 48, 2028, 2035
FOUNTAINS = {"red": 9027, "green": 9028, "blue": 9029, "yellow": 9030, "purple": 9031, "black": 9032, "white": 9033}


def face(x, y, cx, cy):
    """Thing angle (degrees, 45 steps) towards (cx, cy)."""
    a = math.degrees(math.atan2(cy - y, cx - x))
    return int(round(a / 45.0)) * 45 % 360


def mon(m, x, y, type, angle=0):
    """A dormant monster."""
    m.thing(x, y, type, angle)
    m.things[-1]["dormant"] = True


def mons(m, cx, cy, spots, types=MIXED):
    """Dormant monsters on spots [(x, y)], types cycling, facing (cx, cy)."""
    for k, (x, y) in enumerate(spots):
        mon(m, x, y, types[k % len(types)], face(x, y, cx, cy))


def room_sector(m, x0, **kw):
    return m.sector([rect(x0, 0, x0 + W, W)], ceil=192, ctex=CEIL, **kw)


def build_map01():
    m = Map(False)
    room = dict(ceil=192, ctex=CEIL)
    dest = dict(ceil=192, ctex="FLAT5_4", ftex="FLAT14", light=160)
    for i in range(ROOMS):
        x0, x1 = i * STRIDE, i * STRIDE + W
        mx = (x0 + 256, x0 + 512)
        cam = (x0 + 384, 64)
        if i == 0:
            m.sector([rect(x0, 0, x1, W)], **room)
            spots = [(120, 200), (300, 170), (470, 230), (650, 190), (200, 330), (560, 360),
                     (380, 330), (90, 480), (690, 520), (300, 580), (500, 640), (384, 710)]
            mons(m, cam[0], cam[1], [(x0 + x, y) for x, y in spots], MIXED)
            for x, y in ((230, 260), (540, 290), (640, 420), (120, 640)):
                m.thing(x0 + x, y, TECH_PILLAR)
            for x, y in ((400, 450), (150, 560), (700, 700)):
                m.thing(x0 + x, y, LAMP)
            for x, y in ((250, 430), (600, 470), (450, 150), (60, 360)):
                m.thing(x0 + x, y, BARREL)
        elif i == 1:
            # corridor y 64 .. 288 with the opening x0 + 384 .. 512 in its north wall
            op = (x0 + 384, x0 + 512)
            m.sector([rect(x0, 64, x1, 288, xs=op)], ceil=128, ctex=CEIL, light=176)
            t = [(op[0], 288), (op[1], 288), (op[1], 352), (x0 + 640, 352), (x0 + 640, 704), (x0 + 256, 704),
                 (x0 + 256, 352), (op[0], 352)]
            m.sector([t], ceil=192, ctex=CEIL, light=200, wall="BROWN1")
            spots = [(300, 400), (350, 500), (290, 600), (420, 660), (560, 420), (600, 560), (470, 480), (540, 660)]
            mons(m, x0 + 448, 100, [(x0 + x, y) for x, y in spots], MIXED)
            m.thing(x0 + 448, 520, LAMP)
        elif i == 2:
            pit = rect(x0 + 192, 256, x0 + 576, 512)
            m.sector([rect(x0, 0, x1, W), hole(pit)], **room)
            m.sector([pit], floor=-128, ceil=192, ctex=CEIL, ftex="FLAT5_4", wall="GRAY1")
            mon(m, x0 + 384, 360, 16, 270)		# cyberdemon: the upper body shows over the ledge
            mon(m, x0 + 270, 330, 3001, 270)		# fully hidden
            mon(m, x0 + 490, 350, 3003, 270)
            mon(m, x0 + 340, 450, 3002, 270)
            mons(m, x0 + 384, 200, [(x0 + 120, 330), (x0 + 650, 340), (x0 + 200, 620), (x0 + 560, 600), (x0 + 384, 680)],
                 (3004, 3005, 69, 67, 3001))
            m.thing(x0 + 100, 560, TECH_PILLAR)
        elif i == 3:
            blk = rect(x0 + 320, 400, x0 + 448, 528)
            m.sector([rect(x0, 0, x1, W), hole(blk)], ceil=320, ctex=CEIL, light=200)
            m.sector([blk], floor=320, ceil=320, ftex="FLAT5_4", ctex="FLAT5_4", wall="BIGBRIK1", light=200)
            m.thing(x0 + 200, 300, FOUNTAINS["red"])
            m.thing(x0 + 560, 320, FOUNTAINS["blue"])
            m.thing(x0 + 384, 250, FOUNTAINS["green"])
            m.thing(x0 + 384, 620, FOUNTAINS["yellow"])		# behind the solid pillar
            m.thing(x0 + 330, 660, FOUNTAINS["purple"])
            m.thing(x0 + 480, 330, FOUNTAINS["purple"])
            m.thing(x0 + 270, 480, FOUNTAINS["red"])
            m.thing(x0 + 540, 520, FOUNTAINS["white"])
            m.thing(x0 + 200, 600, FOUNTAINS["green"])
            for k, (fx, fy) in enumerate(((300, 140), (470, 150), (230, 200), (540, 210), (350, 330), (430, 340), (130, 400), (640, 420))):
                m.thing(x0 + fx, fy, 9027 + k % 7)
            m.thing(x0 + 720, 40, FOUNTAINS["white"])			# beside the camera: a turn shows it
            m.thing(x0 + 40, 700, FOUNTAINS["white"])
            m.thing(x0 + 110, 520, LAMP)
            m.thing(x0 + 660, 560, TECH_PILLAR)
        elif i == 4:
            m.sector([rect(x0, 0, x1, W)], **room)
            c = (x0 + 384, 384)
            outer = [(c[0] + 290 * math.cos(math.radians(a)), c[1] + 290 * math.sin(math.radians(a))) for a in range(0, 360, 30)]
            inner = [(c[0] + 160 * math.cos(math.radians(a)), c[1] + 160 * math.sin(math.radians(a))) for a in range(15, 360, 30)]
            mons(m, c[0], c[1], [(round(x), round(y)) for x, y in outer + inner], MIXED)
        elif i == 5:
            m.sector([rect(x0, 0, x1, W)], **room)
            spots = [(150, 300), (330, 400), (500, 330), (640, 520), (260, 620), (560, 660), (700, 200), (80, 560),
                     (700, 60), (620, 130), (560, 40)]
            mons(m, x0 + 384, 64, [(x0 + x, y) for x, y in spots], (3004, 3005, 67, 3001, 69, 3003, 16, 66, 3002, 9, 71))
            m.thing(x0 + 420, 560, LAMP)
        elif i == 6:
            m.sector([rect(x0, 0, x1, W)], light=176, ceil=192, ctex=CEIL, wall="BROWN1")
            spots = [(120, 520), (300, 440), (470, 500), (630, 420), (200, 300), (400, 250), (560, 280), (330, 130),
                     (100, 700), (200, 650), (60, 600), (290, 730)]
            mons(m, x0 + 384, 700, [(x0 + x, y) for x, y in spots], (3002, 9, 3003, 71, 3004, 68, 3001, 64, 67, 3005, 66, 69))
            m.thing(x0 + 140, 140, TECH_PILLAR)
        elif i == 7:
            m.sector([rect(x0, 0, x1, W, xs=mx)], **room)
            m.edge((mx[0], W), (mx[1], W), special=156, arg0=900 + i, arg2=0)
            pil = rect(x0 + 352, DY + 384, x0 + 416, DY + 448)
            m.sector([rect(x0, DY, x1, DY + W, xs=mx), hole(pil)], wall="BROWN1", **dest)
            m.sector([pil], floor=128, ceil=128, ftex="FLAT5_4", ctex="FLAT5_4", wall="BIGBRIK1", light=160)
            m.edge((mx[0], DY), (mx[1], DY), id=900 + i)
            mons(m, x0 + 384, DY - 400, [(x0 + 330, DY + 130), (x0 + 450, DY + 200), (x0 + 300, DY + 300), (x0 + 470, DY + 520)],
                 (3003, 69, 3002, 67))
            mons(m, x0 + 384, 64, [(x0 + 200, 400), (x0 + 600, 300)], (3001, 66))
            m.thing(x0 + 192, DY + 576, TECH_PILLAR)
            m.thing(x0 + 576, DY + 576, LAMP)
        elif i == 8:
            mm = (x0 + 128, x0 + 640)
            m.sector([rect(x0, 0, x1, W, xs=mm)], **room)
            m.edge((mm[0], W), (mm[1], W), special=182)
            mons(m, x0 + 384, 64, [(x0 + 160, 500), (x0 + 600, 560), (x0 + 256, 660), (x0 + 520, 650), (x0 + 384, 600)],
                 (3003, 67, 3005, 3002, 69))
            m.thing(x0 + 192, 256, LAMP)
            m.thing(x0 + 384, 420, TECH_PILLAR)
        elif i == 9:
            pit = rect(x0 + 256, 256, x0 + 512, 512)
            low = rect(x0 + 256, DY + 256, x0 + 512, DY + 512)
            m.sector([pit], ftex="FLOOR0_1", id=51, **room)
            m.sector([rect(x0, 0, x1, W), hole(pit)], **room)
            m.sector([low], floor=-256, ceil=0, ctex=CEIL, ftex="FLAT14", wall="BROWN1", light=160, id=52)
            m.sector([rect(x0, DY, x1, DY + W), hole(low)], floor=-256, ceil=0, ctex="FLAT5_4", ftex="FLAT14", wall="BROWN1", light=160)
            m.edge((x0 + 256, 256), (x0 + 512, 256), special=57, arg0=51, arg1=0, arg2=0, arg3=0, arg4=0)
            m.edge((x0 + 256, DY + 256), (x0 + 512, DY + 256), special=57, arg0=51, arg1=0, arg2=0, arg3=1)
            m.edge((x0 + 256, DY + 256), (x0 + 256, DY + 512), special=57, arg0=52, arg1=0, arg2=1, arg3=0, arg4=0)
            m.edge((x0 + 256, 256), (x0 + 256, 512), special=57, arg0=52, arg1=0, arg2=1, arg3=1)
            # both sides of the portal edges: the main room outside the pit, the lower room in / outside its hole
            mons(m, x0 + 384, 200, [(x0 + 224, 320), (x0 + 544, 420), (x0 + 384, 224), (x0 + 224, 470)], (3004, 3003, 3001, 3002))
            mons(m, x0 + 384, DY + 100, [(x0 + 240, DY + 300), (x0 + 270, DY + 420), (x0 + 500, DY + 240),
                                         (x0 + 384, DY + 480), (x0 + 530, DY + 460), (x0 + 384, DY + 330)],
                 (3003, 3002, 3004, 69, 3001, 3005))
            m.thing(x0 + 320, DY + 600, LAMP)
    m.thing(384, 64, 1, 90)
    return m.textmap()


ARENA_W, ARENA_H = 2048, 1536
HALL_Y = 1568
CAMS2 = ((1024, 300), (1024, 1200))


def build_map02():
    rng = random.Random(SEED)
    m = Map(False)
    pillars = []
    for i in range(8):
        for j in range(6):
            cx, cy = 140 + 240 * i, 140 + 240 * j
            # the monster grid sits at 80 * k + 100: a pillar centre is 40 off both axes
            pillars.append(rect(cx - 24, cy - 24, cx + 24, cy + 24))
    m.sector([rect(0, 0, ARENA_W, ARENA_H, xs=(896, 1152))] + [hole(p) for p in pillars], ceil=512, ctex=CEIL)
    for p in pillars:
        m.sector([p], floor=512, ceil=512, ftex="FLAT5_4", ctex="FLAT5_4", wall="BIGBRIK1")
    m.sector([rect(0, ARENA_H, 896, HALL_Y)], floor=512, ceil=512, ftex="FLAT5_4", ctex="FLAT5_4", wall="BROWN1")
    m.sector([rect(1152, ARENA_H, ARENA_W, HALL_Y)], floor=512, ceil=512, ftex="FLAT5_4", ctex="FLAT5_4", wall="BROWN1")
    m.sector([rect(896, ARENA_H, 1152, HALL_Y)], ceil=512, ctex=CEIL)
    occ = [rect(448, 1720, 640, 1900), rect(1100, 1720, 1300, 1940), rect(1700, 1750, 1850, 1990)]
    m.sector([rect(0, HALL_Y, ARENA_W, ARENA_H + 512, xs=(896, 1152))] + [hole(o) for o in occ],
             ceil=512, ctex="CEIL5_1", light=176, wall="BROWN1")
    for o in occ:
        m.sector([o], floor=512, ceil=512, ftex="FLAT5_4", ctex="FLAT5_4", wall="BIGBRIK1")
    count = 0
    for gy in range(100, 1451, 80):
        for gx in range(100, 1951, 80):
            if rng.random() < 0.1:
                continue
            if any(abs(gx - cx) < 50 and abs(gy - cy) < 50 for cx, cy in CAMS2):
                continue
            t = rng.choice(MIXED if rng.random() < 0.25 else SMALL + MEDIUM)
            mon(m, gx, gy, t, rng.choice(range(0, 360, 45)))
            count += 1
    # the hall lane y 1568 .. 1720 is clear (the camera walks it at y 1650)
    hall = [(200, 1780), (260, 1950), (330, 1800), (380, 1850), (400, 1990),
            (700, 1790), (760, 1830), (820, 1780), (860, 1960), (700, 1720),
            (1000, 1760), (1050, 1950), (1350, 1750), (1400, 1830), (1460, 1730), (1520, 1850), (1380, 1920),
            (1180, 1990), (1580, 1730), (1620, 1990),
            (1900, 1800), (1960, 1860), (1900, 1730), (1960, 1960), (1560, 1800)]
    for k, (x, y) in enumerate(hall):
        mon(m, x, y, MIXED[k % len(MIXED)], 180)
        count += 1
    for x, y in ((120, 1700), (120, 1900), (580, 1690), (1200, 1700), (1650, 1700)):
        m.thing(x, y, LAMP)
    m.thing(1024, 64, 1, 90)
    print("MAP02: %d monsters, %d pillars, %d occluders" % (count, len(pillars), len(occ)))
    return m.textmap()


SCALE_MON = 20


def scale_room(m):
    m.sector([rect(0, 0, 1024, 1024)], ceil=256, ctex=CEIL)
    k = 0
    for row in range(4):
        for col in range(5):
            x = 120 + col * 196 + (row % 2) * 40
            y = 300 + row * 200
            mon(m, x, y, MIXED[k % len(MIXED)], face(x, y, 512, 80))
            k += 1
    for x, y in ((250, 400), (770, 600), (512, 700)):
        m.thing(x, y, TECH_PILLAR)
    m.thing(512, 80, 1, 90)


def build_map03():
    m = Map(False)
    scale_room(m)
    return m.textmap()


DECOY_COLS, DECOY_ROWS = 50, 40
DECOY_X0 = 4096


def build_map04():
    m = Map(False)
    scale_room(m)
    rng = random.Random(SEED + 1)
    for r in range(DECOY_ROWS):
        for c in range(DECOY_COLS):
            x, y = DECOY_X0 + c * 256, r * 256
            m.sector([rect(x, y, x + 128, y + 128)], ceil=128 + 16 * rng.randrange(8), ctex=CEIL, light=96 + 16 * rng.randrange(8))
            mon(m, x + 64, y + 64, SMALL[(r + c) % 3], 0)
    return m.textmap()


MAPINFO = b"""
map MAP01 "Gather test"
{
	sky1 = "SKY1"
}

map MAP02 "Gather test (arena)"
{
	sky1 = "SKY1"
}

map MAP03 "Gather test (small)"
{
	sky1 = "SKY1"
}

map MAP04 "Gather test (large)"
{
	sky1 = "SKY1"
}
"""


def look_steps(t):
    return [("face", 90, t + 1), ("face", 70, t + 20), ("face", 110, t + 35), ("face", 90, t + 50)]


def steps_map01():
    """(steps, tics, dense ranges). Each room's block starts at tic t."""
    steps, dense = [], []
    t = 2

    def warp(x, y, at):
        steps.append("warp:%d,%d@%d" % (x, y, at))

    def act(kind, v, at):
        steps.append("%s:%d@%d" % (kind, v, at))

    def start(x0, cx, cy, ang):
        warp(x0 + cx, cy, t)
        act("pitch", 0, t + 1)
        act("face", ang, t + 1)

    # 0 open room: look around, step closer
    x0 = 0
    start(x0, 384, 64, 90)
    for dt, ang in ((20, 60), (35, 120), (50, 90)):
        act("face", ang, t + dt)
    warp(x0 + 384, 250, t + 55)
    act("pitch", 15, t + 70)
    act("pitch", 0, t + 85)
    t += 90

    # 1 doorway reveal: east at 16 u/tic, back west at 24 u/tic
    x0 = 1 * STRIDE
    start(x0, 32, 128, 90)
    x = 32
    k = 0
    while x <= 736:
        warp(x0 + x, 128, t + 4 + k)
        x += 16
        k += 1
    east_end = t + 4 + k
    x = 736
    while x >= 32:
        warp(x0 + x, 128, east_end + 3 + (736 - x) // 24)
        x -= 24
    dense.append([t + 3, east_end + 3 + (736 - 32) // 24 + 2])
    t += 100

    # 2 pit: stand at the ledge, look down a little, sidestep
    x0 = 2 * STRIDE
    start(x0, 384, 200, 90)
    act("pitch", -10, t + 10)
    warp(x0 + 384, 150, t + 30)
    warp(x0 + 384, 230, t + 50)
    act("pitch", -25, t + 51)
    warp(x0 + 300, 230, t + 70)
    act("face", 80, t + 71)
    t += 90

    # 3 particles: look, turn right (the fountain beside the camera), left
    x0 = 3 * STRIDE
    start(x0, 384, 64, 90)
    for dt, ang in ((30, 70), (50, 20), (70, 160), (90, 110)):
        act("face", ang, t + dt)
    t += 110

    # 4 fast turn: 20 deg/tic, 40 deg/tic, then slow
    x0 = 4 * STRIDE
    start(x0, 384, 384, 90)
    a = 90
    for k in range(1, 19):
        act("face", (a + 20 * k) % 360, t + 5 + k)
    a = (a + 360) % 360
    for k in range(1, 10):
        act("face", (a + 40 * k) % 360, t + 30 + k)
    a = (a + 360) % 360
    for k in range(1, 9):
        act("face", (a + 45 * k) % 360, t + 50 + 5 * k)
    dense.append([t + 4, t + 45])
    t += 100

    # 5 / 6 teleport: A -> B -> A -> B, every tic after each jump
    xa, xb = 5 * STRIDE, 6 * STRIDE
    warp(xa + 384, 64, t)
    act("pitch", 0, t + 1)
    act("face", 90, t + 1)
    jumps = ((t + 30, xb + 384, 700, 270), (t + 55, xa + 384, 64, 0), (t + 80, xb + 384, 700, 180))
    for at, x, y, ang in jumps:
        warp(x, y, at)
        act("face", ang, at)
        dense.append([at - 1, at + 12])
    act("face", 90, t + 25)
    act("face", 270, t + 50)
    act("face", 90, t + 105)
    t += 110

    # 7 line portal: look, step towards the portal
    x0 = 7 * STRIDE
    start(x0, 384, 64, 90)
    for dt, ang in ((20, 70), (35, 110), (50, 90)):
        act("face", ang, t + dt)
    warp(x0 + 384, 560, t + 55)
    act("pitch", 15, t + 70)
    act("pitch", 0, t + 85)
    t += 90

    # 8 mirror: from the sides of the pillar, towards the mirror (its monsters show twice)
    x0 = 8 * STRIDE
    start(x0, 384, 64, 90)
    warp(x0 + 230, 150, t + 15)
    act("face", 75, t + 16)
    warp(x0 + 540, 150, t + 45)
    act("face", 105, t + 46)
    warp(x0 + 384, 250, t + 70)
    act("face", 90, t + 71)
    t += 90

    # 9 sector stack: stand over the floor portal, look down through it all round, then level
    x0 = 9 * STRIDE
    start(x0, 384, 300, 90)
    act("pitch", -45, t + 3)
    for dt, ang in ((15, 130), (30, 180), (45, 0), (58, 50)):
        act("face", ang, t + dt)
    act("pitch", -65, t + 66)
    act("face", 90, t + 74)
    act("pitch", 0, t + 80)
    t += 90
    return steps, t + 5, dense


def steps_map02():
    steps = []
    t = 2
    steps += ["warp:1024,300@%d" % t, "pitch:0@%d" % (t + 1)]
    for k in range(72):						# one 360 sweep, 1 degree per tic
        steps.append("face:%d@%d" % ((90 + 5 * (k + 1)) % 360, t + 1 + 5 * k))
    t += 370
    steps += ["warp:1024,60@%d" % t, "face:270@%d" % (t + 1)]		# the south wall
    t += 60
    steps += ["warp:1024,1200@%d" % t, "face:90@%d" % (t + 1)]
    for k in range(1, 7):
        steps.append("face:%d@%d" % (90 + (45 if k % 2 else -45), t + 20 * k))
    t += 130
    x, y = 200, 205
    for k in range(100):						# diagonal walk, ~16 u/tic
        steps.append("warp:%d,%d@%d" % (x + 16 * k, y + 11 * k, t + k))
    steps += ["face:45@%d" % (t + 1)]
    t += 105
    steps += ["warp:60,1650@%d" % t, "face:0@%d" % (t + 1)]
    t += 60
    for k in range(120):						# down the hall
        steps.append("warp:%d,1650@%d" % (60 + 16 * k, t + k))
    t += 125
    steps += ["warp:1024,1500@%d" % t, "face:90@%d" % (t + 1)]
    t += 80
    return steps, t + 5


def steps_scale():
    steps = ["warp:512,80@2", "pitch:0@3"]
    for k in range(1, 36):
        steps.append("face:%d@%d" % (round(90 + 28 * math.sin(2 * math.pi * k / 12.0)), 3 + 30 * k))
    return steps, 1100


def ticks(dt_tics, dense=()):
    last = dt_tics - 2
    out = {"start": 5, "step": 5, "last": last - last % 5}
    if dense:
        out["dense"] = dense
    return out


def write_demo(demodir, name, steps, tics, start_angle=90.0):
    cmds, warps, specials = make_demo.build_script(steps, start_angle, tics)
    data = make_demo.build(name, cmds, 0xA1B2C3D4, ["god", "noclip"], warps, specials, pitch_limit=(90, 90))
    path = os.path.join(demodir, "GATHERTEST_%s.lmp" % name)
    with open(path, "wb") as f:
        f.write(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--out", default=os.path.join(here, "wads", "gathertest.wad"))
    ap.add_argument("--demodir", default=os.path.join(here, "demos", "gathertest"))
    ap.add_argument("--manifest", default=os.path.join(here, "ab_manifest_gathertest.json"))
    args = ap.parse_args()

    lumps = [("MAPINFO", MAPINFO)]
    for name, tm in (("MAP01", build_map01()), ("MAP02", build_map02()), ("MAP03", build_map03()), ("MAP04", build_map04())):
        lumps += [(name, b""), ("TEXTMAP", tm), ("ENDMAP", b"")]
    write_wad(args.out, lumps)
    print("wrote %s (4 maps)" % args.out)

    os.makedirs(args.demodir, exist_ok=True)
    s1, t1, dense = steps_map01()
    s2, t2 = steps_map02()
    s3, t3 = steps_scale()
    write_demo(args.demodir, "MAP01", s1, t1)
    write_demo(args.demodir, "MAP02", s2, t2)
    write_demo(args.demodir, "MAP03", s3, t3)
    write_demo(args.demodir, "MAP04", s3, t3)
    print("wrote %s/GATHERTEST_MAP0{1,2,3,4}.lmp (%d, %d, %d tics)" % (args.demodir, t1, t2, t3))

    with open(os.path.join(here, "ab_manifest_portaltest.json")) as f:
        man = json.load(f)
    man["determinism_map"] = "GATHERTEST_MAP01"
    man["comment"] = ("Actor / particle gather A/B (.scratch/levelmesh-impl ticket 17): wads/gathertest.wad and "
                      "demos/gathertest/ (both from make_gathertest.py), dormant monsters, pillars, lamps, barrels and "
                      "particle fountains. MAP01: ten rooms (open room, doorway reveal, pit, particles, fast turn, "
                      "teleport pair, line portal, mirror, sector stack); MAP02: a 2048 x 2048 arena with ~400 monsters "
                      "and a hall with large occluders; MAP03 / MAP04: the level-size scaling pair (MAP04 adds 2000 "
                      "decoy rooms). Captured every 5 tics, every tic around the latency probes (demo_ticks.dense). "
                      "Same tolerance as ab_manifest.json.")
    man["monsters"] = True		# abtest.py then omits -nomonsters (the maps hold only dormant ones)
    man["demo_dir"] = "demos/gathertest"
    man["demo_ticks"] = ticks(t1, dense)
    man["maps"] = [{"name": "GATHERTEST_MAP0%d" % n, "iwad": "DOOM2.WAD", "pwad": "gathertest.wad", "warp": "MAP0%d" % n,
                    "demo": "GATHERTEST_MAP0%d.lmp" % n, "static_ticks": None} for n in (1, 2, 3, 4)]
    for mp, t in zip(man["maps"][1:], (t2, t3, t3)):
        mp["demo_ticks"] = ticks(t)
    with open(args.manifest, "w") as f:
        json.dump(man, f, indent=1)
        f.write("\n")
    print("wrote %s" % args.manifest)


if __name__ == "__main__":
    main()
