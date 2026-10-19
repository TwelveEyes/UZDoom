#!/usr/bin/env python3
"""
make_oobtest.py — generate wads/oobtest.wad, its scripted demos
demos/oobtest/OOBTEST_MAP0{1,2,3}.lmp and ab_manifest_oobtest.json, the
game-logic feedback A/B maps (.scratch/levelmesh-impl ticket 18: exposure
marks, cullcolor, dither, out-of-body view rules).

The three maps share one layout; a ZScript event handler (OobTestHandler,
MAPINFO gameinfo AddEventHandlers) sets the view per map in PlayerEntered:

  MAP01  chase camera: SetViewPos((-192, 0, 64), VPSF_ALLOWOUTOFBOUNDS), an
         out-of-body view (Viewpoint.bDoOob): secret sectors, radar
         (r_radarclipper) and dither (r_dithertransparency) rules apply.
  MAP02  orthographic top-down: SetViewPos((-512, 0, 0),
         VPSF_ALLOWOUTOFBOUNDS | VPSF_ORTHOGRAPHIC), the demo pitches to -89:
         the ortho viewbox rule (RenderOrthoNoFog) applies.
  MAP03  in-body view (no ViewPos): the exposure set-diff (--gamestate).

Layout (floor 0, ceiling 192, light 192 unless noted):

  A1 (x 0 .. 512, y 0 .. 768) and A2 (x 512 .. 1024): two sectors of equal
     heights; the line between them has no textures (an exposure quad: no
     wall of the levelmesh exposes it). A solid pillar in A1 (x 224 .. 288,
     y 448 .. 512); the dither stop puts the player north of it and the chase
     camera south of it, so the camera's trace to the player (and to the
     dormant imp at (256, 700), in the player's sight) dithers the pillar.
  pit in A2 (x 640 .. 896, y 192 .. 448): floor -128 (the classic clipper
     marks the pit floor's subsectors from the ledge, the exact GPU test
     only the visible ones).
  secret S (x 1024 .. 1280, y 256 .. 512): sector special 1024 (SECRET_MASK),
     ceiling 128, open to A2; the demos never enter it.
  window sill (x 128 .. 384, y 768 .. 832): floor 64, ceiling 128, between
     A1 and the far room F (y 832 .. 1600, ceiling 256, light 176): F's floor
     near the sill is hidden behind it.
  line portal: F's north wall (x 384 .. 640) shows room D (y 2048 .. 2560)
     through Line_SetPortal.

Captured every 5 tics, every tic around the pit, the secret opening and the
dither stop (the manifest's "dense" ranges). Every demo ends with 40 tics
without movement before the last capture, so the one-frame readback latency
of the exposure marks never reaches the game-state dump.

Usage: make_oobtest.py [--out wads/oobtest.wad] [--demodir demos/oobtest]
                       [--manifest ab_manifest_oobtest.json]
"""

import argparse
import json
import os

import make_demo
from make_lighttest import write_wad
from make_skytest import Map, rect, hole

CEIL = "CEIL3_5"
IMP, LAMP, TECH_PILLAR = 3001, 2028, 48
SECRET_MASK = 1024
PORTAL_ID = 918


def build_map():
    m = Map(False)
    room = dict(ceil=192, ctex=CEIL)
    m.sector([rect(0, 0, 512, 768, xs=(128, 384), ys=(256, 512)), hole(rect(224, 448, 288, 512))], **room)
    m.sector([rect(224, 448, 288, 512)], floor=192, ceil=192, ftex="FLAT5_4", ctex="FLAT5_4", wall="BIGBRIK1")
    pit = rect(640, 192, 896, 448)
    m.sector([rect(512, 0, 1024, 768, ys=(256, 512)), hole(pit)], **room)
    m.sector([pit], floor=-128, ceil=192, ctex=CEIL, ftex="FLAT5_4", wall="GRAY1")
    m.sector([rect(1024, 256, 1280, 512)], ceil=128, ctex=CEIL, light=160, wall="BROWN1", special=SECRET_MASK)
    m.sector([rect(128, 768, 384, 832)], floor=64, ceil=128, ctex=CEIL, ftex="FLAT5_4", wall="BROWN1")
    m.sector([rect(0, 832, 1024, 1600, xs=(128, 384, 640))], ceil=256, ctex=CEIL, light=176, wall="BROWN1")
    m.edge((384, 1600), (640, 1600), special=156, arg0=PORTAL_ID, arg2=0)
    m.sector([rect(0, 2048, 1024, 2560, xs=(384, 640))], ceil=256, ctex="FLAT5_4", ftex="FLAT14", light=160, wall="BROWN1")
    m.edge((384, 2048), (640, 2048), id=PORTAL_ID)
    m.thing(256, 700, IMP, 90)		# dither target, facing away from the camera
    m.things[-1]["dormant"] = True
    m.thing(700, 1300, IMP, 270)
    m.things[-1]["dormant"] = True
    m.thing(512, 2300, LAMP)
    m.thing(800, 1000, TECH_PILLAR)
    m.thing(256, 128, 1, 90)
    return m.textmap()


MAPINFO = b"""
gameinfo
{
	AddEventHandlers = "OobTestHandler"
}

map MAP01 "OOB test (chase camera)"
{
	sky1 = "SKY1"
}

map MAP02 "OOB test (ortho)"
{
	sky1 = "SKY1"
}

map MAP03 "OOB test (in-body)"
{
	sky1 = "SKY1"
}
"""

ZSCRIPT = b"""version "4.12"

class OobTestHandler : StaticEventHandler
{
	override void PlayerEntered(PlayerEvent e)
	{
		let mo = players[e.PlayerNumber].mo;
		if (mo == null) return;
		String map = level.mapname.MakeUpper();
		if (map == "MAP01") mo.SetViewPos((-192, 0, 64), VPSF_ALLOWOUTOFBOUNDS);
		else if (map == "MAP02") mo.SetViewPos((-512, 0, 0), VPSF_ALLOWOUTOFBOUNDS | VPSF_ORTHOGRAPHIC);
	}
}
"""


def steps_walk(ortho=False):
    """(steps, tics, dense ranges)."""
    steps, dense = [], []

    def at(kind, v, t):
        steps.append("%s:%s@%d" % (kind, v, t))

    at("warp", "256,128", 2)
    at("pitch", -89 if ortho else 0, 3)
    at("face", 90, 3)
    at("face", 60, 20)
    # the pit from its ledge, looking down
    at("warp", "768,100", 40)
    at("face", 90, 41)
    if not ortho:
        at("pitch", -20, 60)
        at("pitch", 0, 80)
    dense.append([38, 85])
    # the window into the far room
    at("warp", "256,700", 100)
    at("face", 90, 101)
    # the secret opening (never entered)
    at("warp", "960,384", 130)
    at("face", 0, 131)
    dense.append([128, 170])
    # the far room, the portal, back to the window
    at("warp", "512,1200", 170)
    at("face", 90, 171)
    at("face", 270, 210)
    # the dither stop: the player north of the pillar, the chase camera south
    # of it (the camera-to-player trace crosses the pillar), the imp ahead
    at("warp", "256,560", 240)
    at("face", 90, 241)
    at("face", 80, 270)
    dense.append([238, 300])
    return steps, 315, dense


def ticks(dt_tics, dense=()):
    last = dt_tics - 2
    out = {"start": 5, "step": 5, "last": last - last % 5}
    if dense:
        out["dense"] = dense
    return out


def write_demo(demodir, name, steps, tics):
    cmds, warps, specials = make_demo.build_script(steps, 90.0, tics)
    data = make_demo.build(name, cmds, 0xA1B2C3D4, ["god", "noclip"], warps, specials, pitch_limit=(90, 90))
    path = os.path.join(demodir, "OOBTEST_%s.lmp" % name)
    with open(path, "wb") as f:
        f.write(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--out", default=os.path.join(here, "wads", "oobtest.wad"))
    ap.add_argument("--demodir", default=os.path.join(here, "demos", "oobtest"))
    ap.add_argument("--manifest", default=os.path.join(here, "ab_manifest_oobtest.json"))
    args = ap.parse_args()

    tm = build_map()
    lumps = [("MAPINFO", MAPINFO), ("ZSCRIPT", ZSCRIPT)]
    for name in ("MAP01", "MAP02", "MAP03"):
        lumps += [(name, b""), ("TEXTMAP", tm), ("ENDMAP", b"")]
    write_wad(args.out, lumps)
    print("wrote %s (3 maps)" % args.out)

    os.makedirs(args.demodir, exist_ok=True)
    walk, t, dense = steps_walk()
    ortho, _, _ = steps_walk(ortho=True)
    write_demo(args.demodir, "MAP01", walk, t)
    write_demo(args.demodir, "MAP02", ortho, t)
    write_demo(args.demodir, "MAP03", walk, t)
    print("wrote %s/OOBTEST_MAP0{1,2,3}.lmp (%d tics)" % (args.demodir, t))

    with open(os.path.join(here, "ab_manifest_portaltest.json")) as f:
        man = json.load(f)
    man["determinism_map"] = "OOBTEST_MAP01"
    man["comment"] = ("Game-logic feedback A/B (.scratch/levelmesh-impl ticket 18): wads/oobtest.wad and demos/oobtest/ "
                      "(both from make_oobtest.py). One layout, three views: MAP01 a chase camera (out-of-body: secret, "
                      "radar, dither rules), MAP02 an orthographic top-down camera (viewbox rule), MAP03 the in-body view "
                      "(exposure set-diff with --gamestate). Captured every 5 tics, every tic around the pit, the secret "
                      "opening and the dither stop (demo_ticks.dense). Same tolerance as ab_manifest.json.")
    man["monsters"] = True		# abtest.py then omits -nomonsters (the imps are dormant)
    man["demo_dir"] = "demos/oobtest"
    man["demo_ticks"] = ticks(t, dense)
    man["maps"] = [{"name": "OOBTEST_MAP0%d" % n, "iwad": "DOOM2.WAD", "pwad": "oobtest.wad", "warp": "MAP0%d" % n,
                    "demo": "OOBTEST_MAP0%d.lmp" % n, "static_ticks": None} for n in (1, 2, 3)]
    with open(args.manifest, "w") as f:
        json.dump(man, f, indent=1)
        f.write("\n")
    print("wrote %s" % args.manifest)


if __name__ == "__main__":
    main()
