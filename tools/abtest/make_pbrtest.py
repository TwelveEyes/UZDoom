#!/usr/bin/env python3
"""
make_pbrtest.py — generate wads/pbrtest.pk3, its scripted demos
demos/pbrtest/PBRTEST_MAP0{1,2,3}.lmp and ab_manifest_pbrtest.json, the
specular / PBR material A/B map (.scratch/levelmesh-impl ticket 19 step 8).

The pk3 holds generated 128 x 128 PNGs (seeded, reproducible):

  textures/PBRWALL.png, textures/PBRFLAT.png
      base colors: bricks (wall), a 4 x 4 tile grid (flat). Under
      textures/ they are ns_newtextures entries, so the maps and GLDEFS
      find them by their short names (no TEXTURES lump needed).
  materials/pbr/wall_normal.png, wall_specular.png
  materials/pbr/flat_normal.png, flat_metallic.png, flat_roughness.png, flat_ao.png
      material layers, referenced by full path in GLDEFS (TexMan looks up
      names with a '/' as full lump names). Normal maps are tangent-space,
      RGB = n * 0.5 + 0.5, from a height field (brick bevels, tile domes).

GLDEFS (grammar: src/r_data/gldefs.cpp ParseMaterial):

  material texture "PBRWALL" { normal .. specular .. glossiness 20 specularlevel 1 }
      normal + specular -> SHADER_Specular (FMaterial, hw_material.cpp)
  material texture "PBRFLAT" { normal .. metallic .. roughness .. ao .. }
      normal + metallic + roughness + ao (no specular) -> SHADER_PBR

One layout (a 768 x 768 room, PBRWALL walls round a solid 128 x 128
pillar, PBRFLAT floor, CEIL3_5 ceiling, light 144) lit by two point lights
and a spot light (make_dynlighttest.py's writer). Each vantage is its own
map lump, maps/MAP0<v>.wad (abtest.py static mode: +map, no input); the
demos hold, turn 20 degrees each way and back.

Usage: make_pbrtest.py [--out wads/pbrtest.pk3] [--demodir demos/pbrtest]
                       [--manifest ab_manifest_pbrtest.json]
"""

import argparse
import io
import os
import tempfile
import zipfile

import numpy as np
from PIL import Image

from make_dynlighttest import POINT, SPOT, light, player, write_demos, write_manifest
from make_lighttest import write_wad
from make_skytest import Map, rect, hole

SIZE = 128
VANTAGES = [(384, 64, 90), (64, 64, 45), (704, 704, 225)]

GLDEFS = b"""
material texture "PBRWALL"
{
	normal "materials/pbr/wall_normal.png"
	specular "materials/pbr/wall_specular.png"
	glossiness 20
	specularlevel 1
}

material texture "PBRFLAT"
{
	normal "materials/pbr/flat_normal.png"
	metallic "materials/pbr/flat_metallic.png"
	roughness "materials/pbr/flat_roughness.png"
	ao "materials/pbr/flat_ao.png"
}
"""


def png(a):
    """uint8 array (H, W) or (H, W, 3) -> PNG bytes."""
    buf = io.BytesIO()
    Image.fromarray(np.ascontiguousarray(a, dtype=np.uint8)).save(buf, "PNG")
    return buf.getvalue()


def normal_map(h, strength):
    """Tangent-space normals of the (tileable) height field h, RGB = n * 0.5 + 0.5."""
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5
    n = np.stack([-dx * strength, dy * strength, np.ones_like(h)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return np.clip((n * 0.5 + 0.5) * 255.0 + 0.5, 0, 255)


def brick_height():
    """Bricks 64 x 32 (rows offset by 32), 4-px mortar, 3-px bevel: 1 on the face, 0 in the mortar."""
    y, x = np.mgrid[0:SIZE, 0:SIZE]
    row = y // 32
    bx = (x + 32 * (row % 2)) % 64
    by = y % 32
    edge = np.minimum(np.minimum(bx, 63 - bx), np.minimum(by, 31 - by)).astype(float) - 2.0
    return np.clip(edge / 3.0, 0.0, 1.0), (row * 2 + (x + 32 * (row % 2)) // 64)


def tile_height():
    """4 x 4 tiles of 32 px, each a dome: 1 at the centre, 0 at the grout."""
    y, x = np.mgrid[0:SIZE, 0:SIZE]
    u = (x % 32 - 15.5) / 16.0
    v = (y % 32 - 15.5) / 16.0
    return np.clip(1.0 - np.maximum(np.abs(u), np.abs(v)) ** 4, 0.0, 1.0), (y // 32) * 4 + x // 32


def build_images():
    rng = np.random.default_rng(19)
    files = {}

    h, brick = brick_height()
    noise = rng.uniform(-18.0, 18.0, (SIZE, SIZE))
    tint = rng.uniform(0.8, 1.2, brick.max() + 1)[brick]
    face = np.stack([150 * tint, 70 * tint, 50 * tint], axis=-1) + noise[..., None]
    mortar = np.full((SIZE, SIZE, 3), 110.0) + noise[..., None] * 0.5
    m = (h > 0.0)[..., None]
    files["textures/PBRWALL.png"] = png(np.clip(np.where(m, face, mortar), 0, 255))
    files["materials/pbr/wall_normal.png"] = png(normal_map(h * 6.0, 1.0))
    files["materials/pbr/wall_specular.png"] = png(np.clip(32 + 160 * h + noise * 0.5, 0, 255))

    h, tile = tile_height()
    checker = ((tile // 4) + tile) % 2
    noise = rng.uniform(-10.0, 10.0, (SIZE, SIZE))
    a = np.array([60, 90, 140], float)
    b = np.array([200, 190, 160], float)
    base = np.where(checker[..., None] == 1, a, b) * (0.75 + 0.25 * h)[..., None] + noise[..., None]
    files["textures/PBRFLAT.png"] = png(np.clip(base, 0, 255))
    files["materials/pbr/flat_normal.png"] = png(normal_map(h * 8.0, 1.0))
    files["materials/pbr/flat_metallic.png"] = png(np.where(checker == 1, 255.0, 0.0))
    rough = 48.0 + 176.0 * (tile % 4) / 3.0
    files["materials/pbr/flat_roughness.png"] = png(np.clip(rough + noise, 0, 255))
    files["materials/pbr/flat_ao.png"] = png(96.0 + 159.0 * h)
    return files


def build_map():
    m = Map(False)
    pillar = rect(320, 320, 448, 448)
    m.sector([rect(0, 0, 768, 768), hole(pillar)], ceil=192, ctex="CEIL3_5", ftex="PBRFLAT", light=144, wall="PBRWALL")
    lights = [
        light(POINT, 200, 560, 64, (255, 240, 220), 320),
        light(POINT, 560, 560, 32, (128, 160, 255), 192),
        light(SPOT, 600, 100, 120, (255, 224, 192), 512, angle=135, pitch=15, cone=(15, 35)),
    ]
    maps = []
    for v, start in enumerate(VANTAGES):
        m.things = [player(*start)] + lights
        maps.append(("MAP0%d" % (v + 1), 1, v, start, m.textmap()))
    return maps


def map_wad(lump, textmap):
    fd, tmp = tempfile.mkstemp(suffix=".wad")
    os.close(fd)
    try:
        write_wad(tmp, [(lump, b""), ("TEXTMAP", textmap), ("ENDMAP", b"")])
        with open(tmp, "rb") as f:
            return f.read()
    finally:
        os.unlink(tmp)


def mapinfo(maps):
    return "\n".join('map %s "PBR / specular material test, vantage %d"\n{\n\tsky1 = "SKY1"\n}\n' % (lump, v)
                     for lump, _, v, _, _ in maps).encode("ascii")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--out", default=os.path.join(here, "wads", "pbrtest.pk3"))
    ap.add_argument("--demodir", default=os.path.join(here, "demos", "pbrtest"))
    ap.add_argument("--manifest", default=os.path.join(here, "ab_manifest_pbrtest.json"))
    args = ap.parse_args()

    maps = build_map()
    files = {"mapinfo.txt": mapinfo(maps), "gldefs.txt": GLDEFS.lstrip()}
    files.update(build_images())
    for lump, _, _, _, tm in maps:
        files["maps/%s.wad" % lump] = map_wad(lump, tm)
    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as z:
        for name in sorted(files):
            # fixed timestamp: the pk3 is byte-reproducible
            z.writestr(zipfile.ZipInfo(name, date_time=(2026, 10, 21, 0, 0, 0)), files[name], zipfile.ZIP_DEFLATED)
    print("wrote %s (%d files, %d maps)" % (args.out, len(files), len(maps)))
    for lump, _, v, start, _ in maps:
        print("  %s  vantage %d at (%d, %d) angle %d" % (lump, v, *start))

    write_demos(args.demodir, "PBRTEST", maps)
    print("wrote %s/PBRTEST_MAP0*.lmp" % args.demodir)

    write_manifest(args.manifest, here, "PBRTEST", "pbrtest.pk3", "demos/pbrtest", maps,
                   "Specular / PBR material A/B (.scratch/levelmesh-impl ticket 19 step 8): wads/pbrtest.pk3, "
                   "demos/pbrtest/ and this manifest (all from make_pbrtest.py). PBRWALL walls (normal + specular: "
                   "SHADER_Specular) round a pillar, PBRFLAT floor (normal + metallic + roughness + ao: SHADER_PBR), "
                   "two point lights and a spot. Each vantage its own map lump (MAP01..MAP03) for static mode. "
                   "lights.pk3 is loaded (extra_files). Same tolerance as ab_manifest.json.")
    print("wrote %s" % args.manifest)


if __name__ == "__main__":
    main()
