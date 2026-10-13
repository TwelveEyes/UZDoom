#!/usr/bin/env python3
# lm_bake_survey.py — run the levelmesh bake (ticket 06) on the acceptance maps.
#
# Loads each map on Vulkan with gl_uselevelmesh 1, waits for the bake's three
# "LevelMeshBake <map>:" stat lines (plus the OBJ line with --obj), then kills
# the engine. Writes a JSON + markdown table. Uses a throwaway -config so the
# user's ini is not touched.
#
#   lm_bake_survey.py [--only SUBSTR] [--obj DIR] [--out results/lm_bake_survey]

import argparse, json, os, re, subprocess, sys, tempfile, time

KIT = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.abspath(os.path.join(KIT, "..", "..", "build"))

# (label, iwad, pwad, map) — the 26 acceptance maps (manifest.md)
MAPS = [("DOOM2", "DOOM2.WAD", None, m) for m in ("MAP01", "MAP07", "MAP21", "MAP23", "MAP27", "MAP30", "MAP31")]
MAPS += [("HEXEN", "HEXEN.WAD", None, m) for m in ("MAP01", "MAP10", "MAP27")]
MAPS += [("HERETIC", "HERETIC.WAD", None, m) for m in ("E1M2", "E5M6")]
MAPS += [("myhouse", "DOOM2.WAD", "myhouse.pk3", m) for m in ("MAP01", "20PAM")]
MAPS += [("Pirates", "DOOM2.WAD", "Pirates!.wad", m) for m in ("MAP50", "MAP51", "MAP57", "MAP43", "MAP49", "MAP54", "MAP58")]
MAPS += [("SOS", "DOOM2.WAD", "SOS_Boom.wad", m) for m in ("MAP32", "MAP12", "MAP45", "MAP46")]
MAPS += [("planisf", "DOOM2.WAD", "planisf2.wad", "MAP01")]

NUM = r"([\d.]+)"


def parse(lines):
    d = {}
    for l in lines:
        m = re.search(r"(\d+) surfaces \((\d+) walls, (\d+) flats, (\d+) 3D flats\), (\d+) vertices, (\d+) triangles, " + NUM +
                      r" ms \(walls " + NUM + r", flats " + NUM + r"\)", l)
        if m:
            k = ["surfaces", "walls", "flats", "flats3d", "vertices", "triangles"]
            d.update({k[i]: int(m.group(i + 1)) for i in range(6)})
            d.update(ms=float(m.group(7)), wall_ms=float(m.group(8)), flat_ms=float(m.group(9)))
        for key, pat in (("sides", r"sides (\d+)"), ("poly_skipped", r"poly skipped (\d+)"),
                         ("translucent_walls", r"translucent walls (\d+)"), ("portal_walls", r"portal walls (\d+)"),
                         ("fog_walls", r"fog walls (\d+)"), ("upper_missing", r"upper missing (\d+)"),
                         ("lower_missing", r"lower missing (\d+)"), ("translucent_flats", r"translucent flats (\d+)"),
                         ("portal_flats", r"portal/sky flats (\d+)"), ("untextured_flats", r"untextured flats (\d+)"),
                         ("heightsec", r"heightsec surfaces (\d+)"), ("z_mismatch", r"z mismatches (\d+)"),
                         ("v_mismatch", r"v mismatches (\d+)"), ("z_ulp", r"within 1 ulp: z (\d+)"), ("v_ulp", r"within 1 ulp: z \d+, v (\d+)"), ("off_bottom", r"offset-anchored bottom (\d+)"),
                         ("off_top", r" top (\d+) side"), ("off_side", r" side (\d+) flat"), ("off_flat", r" flat (\d+);"),
                         ("vscale_inconsistent", r"vScale inconsistent (\d+)"), ("planeref_overflow", r"planeRef overflow (\d+)"),
                         ("ff_vref_heuristic", r"3D vRef heuristic (\d+)")):
            if "LevelMeshBake" in l:
                m = re.search(pat, l)
                if m and key not in d:
                    d[key] = int(m.group(1))
    return d


def run(label, iwad, pwad, mapname, objdir, timeout):
    ini = tempfile.NamedTemporaryFile(suffix=".ini", delete=False).name
    cmd = ["stdbuf", "-oL", "-eL", os.path.join(BUILD, "uzdoom"), "-config", ini, "-iwad", os.path.join("wads", iwad)]
    if pwad:
        cmd += ["-file", os.path.join("wads", pwad)]
    cmd += ["-width", "1280", "-height", "720", "-nomonsters", "+set", "vid_preferbackend", "1",
            "+set", "gl_uselevelmesh", "1", "+set", "vid_vsync", "0"]
    if objdir:
        cmd += ["+set", "lm_bake_objdir", objdir]
    cmd += ["+map", mapname]
    want = 4 if objdir else 3
    p = subprocess.Popen(cmd, cwd=BUILD, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    lines, t0 = [], time.time()
    try:
        for l in p.stdout:
            if l.startswith(f"LevelMeshBake {mapname}:"):
                lines.append(l.strip())
                if len(lines) >= want:
                    break
            if time.time() - t0 > timeout:
                break
    finally:
        p.kill()
        p.wait()
        os.unlink(ini)
    d = parse(lines)
    d.update(label=label, map=mapname)
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only")
    ap.add_argument("--obj", help="directory for per-map OBJ exports")
    ap.add_argument("--out", default=os.path.join(KIT, "results", "lm_bake_survey"))
    ap.add_argument("--timeout", type=int, default=300)
    a = ap.parse_args()
    if a.obj:
        os.makedirs(a.obj, exist_ok=True)
    rows = []
    for label, iwad, pwad, m in MAPS:
        if a.only and a.only not in f"{label}_{m}":
            continue
        d = run(label, iwad, pwad, m, os.path.abspath(a.obj) if a.obj else None, a.timeout)
        rows.append(d)
        print(f"{label} {m}: tris {d.get('triangles')} ms {d.get('ms')} zmis {d.get('z_mismatch')} vmis {d.get('v_mismatch')}", flush=True)
    with open(a.out + ".json", "w") as f:
        json.dump(rows, f, indent=1)
    cols = ["label", "map", "sides", "surfaces", "walls", "flats", "flats3d", "triangles", "ms", "wall_ms", "flat_ms",
            "z_mismatch", "v_mismatch", "z_ulp", "v_ulp", "translucent_walls", "portal_walls", "fog_walls", "upper_missing", "lower_missing",
            "translucent_flats", "portal_flats", "heightsec", "off_bottom", "off_top", "off_side", "off_flat",
            "vscale_inconsistent", "planeref_overflow", "ff_vref_heuristic", "poly_skipped"]
    with open(a.out + ".md", "w") as f:
        f.write("| " + " | ".join(cols) + " |\n|" + "---|" * len(cols) + "\n")
        for r in rows:
            f.write("| " + " | ".join(str(r.get(c, "")) for c in cols) + " |\n")
    print("wrote", a.out + ".json", a.out + ".md")


if __name__ == "__main__":
    sys.exit(main())
