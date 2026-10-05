#!/usr/bin/env python3
"""
scan_maps.py — levelmesh A/B: map inventory + feature scan (dev-machine gate).

Pins the hexen/heretic/classic-ports rows of manifest.md to maps that actually
carry each required feature (portals / sector_link / skybox / fog / fake-contrast).

Usage:
    python3 scan_maps.py <WAD> [<WAD> ...]

Layouts, keywords and special numbers are NOT guessed — each was verified
against UZDoom's engine source and/or the WADs themselves:

  * WAD header: 12 bytes (magic, numlumps, dir_offset). Directory entry:
    16 bytes (pos u32, size u32, name[8] NUL-padded). Names are used in full;
    a map marker is a lump named exactly MAP##.. / E#M#.
  * A map block = marker lump, then map-data lumps until the next marker or an
    ENDMAP lump (ZDoom convention, used by UDMF packs). A marker whose block
    carries no map data (e.g. a graphics patch happeninng to be named MAP03)
    is a phantom and is dropped from the inventory.
  * Two on-disk map formats are recognized:
      a) BINARY: THINGS / LINEDEFS / SIDEDEFS / VERTEXES / SECTORS (+ SSECTORS,
         NODES, REJECT, BLOCKMAP, ...). Linedef layout is chosen PER BLOCK
         exactly like the engine does (p_openmap.cpp: MapData::HasBehavior —
         the block carries a lump whose name starts with "BEHA"; the check is
         strnicmp(name, "BEHA", 4), and the actual lump name is "BEHAVIOR"):
             BEHA present -> maplinedef2_t 16B:  v1 u16@0, v2 u16@2,
                            flags u16@4, special u8@6, args[5] u8@7..11,
                            side1 u16@12, side2 u16@14
             else         -> maplinedef_t  14B:  v1 u16@0, v2 u16@2,
                            flags u16@4, special u16@6, side1 u16@10,
                            side2 u16@12
           (doomtype.h) Validated 100% clean over every map of DOOM2.WAD,
           HEXEN.WAD, HERETIC.WAD, STRIFE1.WAD, Pirates!.wad.
         Vertices are 4 bytes (short x, y), sides 30 bytes, sectors 26,
         things 10.
      b) UDMF: a TEXTMAP lump (maploader/udmf.cpp), optionally followed by
         ZNODES/GLNODES + ENDMAP. Objects are `type // id` header lines with
         a `{ ... }` body; properties are `key = value;`. Line specials use
         the modern (ZDoom/Hexen) number space for namespaces zdoom/hexen/
         mbf/boom (no xlat — xlat applies to binary MAPTYPE_DOOM maps only);
         the default namespace is hexen. Sector specials are a bitfield in the
         high bits (e.g. 0x400 = SECRET_MASK, p_lnspec.h) plus the low byte.
         Tilted planes are explicit: a sector with all four floorplane_a..d or
         ceilingplane_a..d properties. Sidedef `smoothlighting` is per-side
         fake contrast (spec: udmf_zdoom.txt).
  * MAPINFO: one lump, searched for a single map by name across ALL loaded
    WADs (g_dumpinfo.cpp: FindMap). ZMAPINFO (new syntax: `map MAP41 "t"`,
    `defaultmap {}`) and the old syntax (`map 1 {...}`) are both parsed.
    `defaultmap` keywords apply to every map in the file.
  * Feature signals (line specials + MAPINFO keywords + UDMF properties).
    UZDoom has NO FF-lump (3D floors) or POLYOBJ-lump readers (verified: no
    such lump-name strings anywhere in src/):
        portals:     57 Sector_SetPortal, 156 Line_SetPortal,
                     301 Line_QuickPortal (maploader/specials.cpp:312/91)
        sector_link: 107 Line_SetPortalTarget, 51 Sector_SetLink
        3D floors:   160 Sector_Set3DFloor, 50 ExtraFloor_LightOnly
        polyobjs:    1-9, 59, 86-93, 283 (Polyobj_*)
      (playsim/actionspecials.h)
  * CRITICAL (p_openmap.cpp MapLoader::LoadLevel): for BINARY maps these
    special-based features are only interpretable for BEHA (Hexen-format)
    maps. A block WITHOUT a BEHA lump is loaded as MAPTYPE_DOOM and its raw
    line specials are run through the game's xlat translator (xlat/doom.txt /
    heretic.txt / strife.txt, ...), so the raw numbers are CLASSIC specials,
    NOT ZDoom numbers (e.g. Heretic raw 107 is Stairs_BuildUpDoom, not
    Line_SetPortalTarget; DOOM2 raw 51 is a door). Such maps cannot carry any
    of the ZDoom rendering features above — the scanner reports n/a for their
    feature columns rather than a (wrong) count. Only 3D floors have a known
    raw number space for DOOM-format maps (xlat/base.txt); note 301 =
    Sector_Set3DFloor there but Line_QuickPortal in BEHA maps.

Dev-machine gate: runs locally on a machine with the WADs; output feeds
manifest.md. Not CI.
"""
import os
import re
import struct
import sys
from collections import Counter

# ZMAPINFO top-level section starters (any of these ends a map/defaultmap
# keyword capture; only map/defaultmap capture keywords).
SECTION_KEYWORDS = {
    "gameinfo", "defaultmap", "defaultskill", "clearskills",
    "clearautomapcolorsets", "clearepisodes", "clearexits",
    "clearintermissions", "skill", "map", "automapcolorset",
    "intermission", "doomednums", "spawnnums", "conversationids",
    "damagetype",
}

# Feature keywords (MAPINFO, map section or defaultmap).
MINFO_KEYWORDS = [
    "skybox", "sky1", "sky2", "sky3", "fakecontrast", "forcefakecontrast",
    "fogdensity", "fog", "sectorlink", "sector_link", "portal", "portals",
]

# UDMF object types that carry map geometry.
UDMF_TYPES = {"thing", "vertex", "sidedef", "linedef", "sector", "script"}

# UDMF namespaces whose line specials use the modern number space directly
# (no xlat). UDMF default namespace is hexen (specs/udmf.txt).
UDMF_MODERN_NS = {"zdoom", "hexen", "mbf", "boom", "hexen1", ""}

# Special numbers (UZDoom playsim/actionspecials.h — engine-verified).
# BEHA (Hexen-format) maps and UDMF (modern namespace): raw numbers are
# ZDoom specials.
SP_3DFLOOR = {160, 50}
SP_POLYOBJ = set(range(1, 10)) | {59, 86, 87, 88, 89, 90, 91, 92, 93, 283}
SP_PORTAL = {57, 156, 301}
SP_LINK = {107, 51}
# DOOM-format BINARY maps: specials are translated (xlat). Only 3D floors
# have a DOOM-format number space — xlat/base.txt maps these to
# Sector_Set3DFloor (included by doom.txt and heretic.txt; strife.txt has its
# own table). Note 301 = Sector_Set3DFloor here but Line_QuickPortal in
# BEHA maps.
SP_3DFLOOR_DOOM = {281, 289, 300, 301, 302, 303, 304, 305, 306, 332,
                  400, 401, 402, 403, 404, 405, 406, 407, 408, 413, 414,
                  415, 416, 417}
KNOWN_SPECS = {1: "Polyobj_StartLine", 5: "Polyobj_ExplicitLine",
               50: "ExtraFloor_LightOnly", 51: "Sector_SetLink",
               57: "Sector_SetPortal", 107: "Line_SetPortalTarget",
               156: "Line_SetPortal", 160: "Sector_Set3DFloor",
               181: "Plane_Align", 301: "Line_QuickPortal"}


def wad(path):
    try:
        with open(path, "rb") as f:
            d = f.read()
    except OSError as e:
        raise SystemExit(f"cannot read {path}: {e}") from e
    if d[0:4] not in (b"IWAD", b"WAD\x1a", b"PWAD"):
        raise SystemExit(f"not a WAD: {path}")
    nlumps, dir_off = struct.unpack_from("<II", d, 4)
    lumps = []
    for i in range(nlumps):
        off = dir_off + i * 16
        pos, size = struct.unpack_from("<II", d, off)
        name = d[off + 8:off + 16].rstrip(b"\0").decode("ascii", "replace")
        lumps.append((name, pos, size))
    return d, lumps


def map_blocks(lumps):
    """Marker lumps -> following lumps until the next marker or ENDMAP.
    Returns (blocks, phantoms) where blocks is a list of
    (marker_name, {name: (pos, size)}) and phantoms is the list of marker
    names whose block carried no map data."""
    mkr = [i for i, (n, p, s) in enumerate(lumps)
           if re.fullmatch(r"MAP\d+|E\dM\d", n)]
    out = []
    phantoms = []
    for k, mi in enumerate(mkr):
        nxt = mkr[k + 1] if k + 1 < len(mkr) else len(lumps)
        blk = {}
        for j in range(mi + 1, nxt):
            n, p, s = lumps[j]
            if n == "ENDMAP":
                break
            if n in blk:
                blk[n] = None  # duplicate -> flagged by caller
            else:
                blk[n] = (p, s)
        has_data = ("TEXTMAP" in blk or "LINEDEFS" in blk
                    or "THINGS" in blk or "SECTORS" in blk)
        if has_data:
            out.append((lumps[mi][0], blk))
        else:
            phantoms.append(lumps[mi][0])
    return out, phantoms


def parse_mapinfo(data):
    """Section-aware MAPINFO/ZMAPINFO parse.

    Returns (sections, default_kw, syntax) where sections maps
    key(str) -> (title, sorted keyword list), default_kw is the
    defaultmap keyword set, syntax is 'old'|'new'|'old+new'|'none'.
    """
    txt = data.decode("latin-1")
    # strip // line comments (kept simple: MAPINFO has no // in strings here)
    lines = [re.sub(r"//.*$", "", ln).strip() for ln in txt.splitlines()]

    sections = {}
    default_kw = set()
    cur = None            # (kind, key) where kind is 'map' or 'defaultmap'
    new = old = False
    for ln in lines:
        if not ln:
            continue
        head = ln.split(None, 1)[0].lower()
        if head == "map":
            m = re.match(r'map\s+(\S+)\s*(?:"([^"]*)")?', ln)
            if not m:
                cur = None
                continue
            key, title = m.group(1), m.group(2) or ""
            if key.isdigit():
                old = True
                cur = ("map", key)
            else:
                new = True
                cur = ("map", key)
            sections[key] = [title, []]
        elif head == "defaultmap":
            new = True
            cur = ("defaultmap", None)
            sections.setdefault("__default__", ["", []])
        elif head in SECTION_KEYWORDS:
            cur = None
        else:
            if cur is None:
                continue
            if cur[0] == "defaultmap":
                for kw in MINFO_KEYWORDS:
                    if re.search(rf"\b{kw}\b", ln, re.I):
                        default_kw.add(kw)
            else:
                for kw in MINFO_KEYWORDS:
                    if re.search(rf"\b{kw}\b", ln, re.I):
                        sections[cur[1]][1].append(kw)
    for key in sections:
        kws = sections[key][1]
        sections[key] = (sections[key][0], sorted(set(kws)))
    syntax = "none"
    if new and old:
        syntax = "old+new"
    elif new:
        syntax = "new"
    elif old:
        syntax = "old"
    return sections, default_kw, syntax


def linedefs(d, blk, beha):
    """Decode the block's LINEDEFS per the engine rule (BEHA -> 16B).
    Returns (count, specials Counter, bad_refs, nondiv) or None."""
    if blk.get("LINEDEFS") is None or blk.get("VERTEXES") is None \
            or blk.get("SIDEDEFS") is None:
        return None
    if beha:
        stride, spoff, spfmt, s1off, s2off = 16, 6, "<B", 12, 14
    else:
        stride, spoff, spfmt, s1off, s2off = 14, 6, "<H", 10, 12
    size = blk["LINEDEFS"][1]
    if size % stride:
        return (0, Counter(), 0, size % stride)
    vert = blk["VERTEXES"][1] // 4
    side = blk["SIDEDEFS"][1] // 30
    data = d[blk["LINEDEFS"][0]:blk["LINEDEFS"][0] + size]
    n = len(data) // stride
    specials = Counter()
    bad = 0
    for i in range(n):
        p = i * stride
        v1, v2 = struct.unpack_from("<HH", data, p)
        sp = struct.unpack_from(spfmt, data, p + spoff)[0]
        s1 = struct.unpack_from("<H", data, p + s1off)[0]
        s2 = struct.unpack_from("<H", data, p + s2off)[0]
        if not (v1 < vert and v2 < vert
                and (s1 < side or s1 == 0xFFFF)
                and (s2 < side or s2 == 0xFFFF)):
            bad += 1
        if sp:
            specials[sp] += 1
    return (n, specials, bad, 0)


def udmf(data):
    """Minimal UDMF inventory (maploader/udmf.cpp semantics).

    Returns a dict with: namespace, counts per object type, line_specials,
    sector_specials (raw values), n_planes (sectors with a full floorplane or
    ceilingplane equation), n_portal_sec (sectors with portal_floor_*/
    portal_ceil_* props), n_smooth (sidedefs with smoothlighting),
    n_nofc (sidedefs with nofakecontrast).

    Assumes engine-writer UDMF: an object is a `type // id` header line
    followed by a `{` line; a property is `key = value;`. Multi-line string
    values (e.g. script code) are consumed opaquely.
    """
    txt = data.decode("latin-1", "replace")
    lines = [ln.strip() for ln in txt.splitlines()]
    res = {
        "namespace": "",
        "counts": Counter(),
        "line_specials": Counter(),
        "sector_specials": Counter(),
        "n_planes": 0,
        "n_portal_sec": 0,
        "n_smooth": 0,
        "n_nofc": 0,
    }
    cur = None        # current object type
    in_obj = False
    n_fplane = 0      # floorplane_* props seen in the current sector
    n_cplane = 0      # ceilingplane_* props seen in the current sector
    sec_portal = False
    i = 0
    nlines = len(lines)
    while i < nlines:
        s = lines[i]
        i += 1
        if not s:
            continue
        if s == "{":
            if cur:
                in_obj = True
                n_fplane = n_cplane = 0
                sec_portal = False
            continue
        if s == "}":
            if in_obj and cur == "sector":
                if n_fplane >= 4 or n_cplane >= 4:
                    res["n_planes"] += 1
                if sec_portal:
                    res["n_portal_sec"] += 1
            in_obj = False
            cur = None
            continue
        if not in_obj:
            nm = re.match(r'^\s*namespace\s*=\s*"?([\w]*)"?', s)
            if nm:
                res["namespace"] = nm.group(1).lower()
                continue
            m = re.match(r"^(\w+)", s)
            if m and m.group(1).lower() in UDMF_TYPES:
                cur = m.group(1).lower()
                res["counts"][cur] += 1
            continue
        pm = re.match(r"^(\w+)\s*=\s*(.*?)\s*;?\s*(?://.*)?$", s)
        if not pm:
            continue
        key, val = pm.group(1).lower(), pm.group(2)
        if val.count('"') % 2:  # multi-line string: consume until it closes
            while i < nlines and val.count('"') % 2:
                val += " " + lines[i].rstrip()
                i += 1
        if cur == "linedef" and key == "special":
            try:
                v = int(val)
            except ValueError:
                continue
            if v:
                res["line_specials"][v] += 1
        elif cur == "sector":
            if key == "special":
                try:
                    v = int(val)
                except ValueError:
                    continue
                if v:
                    res["sector_specials"][v] += 1
            elif key.startswith("floorplane_"):
                n_fplane += 1
            elif key.startswith("ceilingplane_"):
                n_cplane += 1
            elif key.startswith(("portal_floor_", "portal_ceil_")):
                sec_portal = True
        elif cur == "sidedef":
            if val in ("true", "1"):
                if key == "smoothlighting":
                    res["n_smooth"] += 1
                elif key == "nofakecontrast":
                    res["n_nofc"] += 1
    return res


def fmt_cell(v, na="n/a"):
    """Feature-column cell: None renders as n/a (signal not available in
    this map format)."""
    return na if v is None else str(v)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for path in sys.argv[1:]:
        d, lumps = wad(path)
        blocks, phantoms = map_blocks(lumps)

        # MAPINFO: first lump named MAPINFO, else ZMAPINFO.
        mapi = None
        mapi_name = None
        for cand in ("MAPINFO", "ZMAPINFO"):
            for n, p, s in lumps:
                if n == cand:
                    mapi = d[p:p + s]
                    mapi_name = cand
                    break
            if mapi is not None:
                break
        sections, default_kw, syntax = ({}, set(), "none")
        if mapi is not None:
            sections, default_kw, syntax = parse_mapinfo(mapi)
        name_sections = {k for k in sections
                         if not re.fullmatch(r"\d+", k) and k != "__default__"}
        has_old = any(re.fullmatch(r"\d+", k) for k in sections
                      if k != "__default__")

        mapi_hdr = f"yes ({mapi_name})" if mapi is not None else "no"
        nsec_map = len([k for k in sections if k != "__default__"])
        print(f"\n=== {os.path.basename(path)} ===")
        print(f"lumps={len(lumps)} maps={len(blocks)} MAPINFO={mapi_hdr}"
              + (f" (syntax={syntax}, sections={nsec_map}, "
                 f"defaultmap-kw={','.join(sorted(default_kw)) or '-'})"
                 if mapi is not None else "")
              + (f"  phantoms={len(phantoms)}" + f" {phantoms}"
                 if phantoms else ""))

        rows = []
        seen_names = set()
        for idx, (name, blk) in enumerate(blocks, start=1):
            # engine rule: strnicmp(lumpname, "BEHA", 4) — the actual name
            # is "BEHAVIOR" (HEXEN, Pirates!)
            beha = any(n.startswith("BEHA") and blk[n] is not None
                       for n in blk)
            # duplicate map names: the engine loads the first occurrence
            # (p_openmap.cpp: CheckNumForName is a first-match lookup), so a
            # later copy is dead data.
            if name in seen_names:
                notes = ["DUPLICATE of earlier block (first occurrence wins)"]
            else:
                notes = []
                seen_names.add(name)
            is_udmf = blk.get("TEXTMAP") is not None
            if is_udmf and "LINEDEFS" in blk:
                notes.append("TEXTMAP+binary (engine uses UDMF)")

            specials = Counter()
            nlines = nt = ns = nsi = nv = 0
            nport = nlink = n3d = npo = nplane = nsmooth = None
            if is_udmf:
                u = udmf(d[blk["TEXTMAP"][0]:blk["TEXTMAP"][0]
                            + blk["TEXTMAP"][1]])
                nt = u["counts"].get("thing", 0)
                nlines = u["counts"].get("linedef", 0)
                ns = u["counts"].get("sector", 0)
                nsi = u["counts"].get("sidedef", 0)
                nv = u["counts"].get("vertex", 0)
                specials = u["line_specials"]
                modern = u["namespace"] in UDMF_MODERN_NS
                c = Counter(specials)
                if modern:
                    n3d = sum(v for k, v in c.items() if k in SP_3DFLOOR)
                    npo = sum(v for k, v in c.items() if k in SP_POLYOBJ)
                    nport = sum(v for k, v in c.items()
                                if k in SP_PORTAL)
                    nlink = sum(v for k, v in c.items() if k in SP_LINK)
                else:
                    n3d = sum(v for k, v in c.items()
                              if k in SP_3DFLOOR_DOOM)
                    npo = nport = nlink = None
                    notes.append(f"udmf ns={u['namespace'] or 'hexen?'} "
                                 "(classic specials)")
                nplane = u["n_planes"] or None
                nsmooth = u["n_smooth"] or None
                if u["n_portal_sec"]:
                    notes.append(f"{u['n_portal_sec']} sectors w/ portal "
                                 "props")
                if u["n_nofc"]:
                    notes.append(f"{u['n_nofc']} sides nofakecontrast")
                ssp = u["sector_specials"]
                if ssp:
                    low = {k & 0xFF: v for k, v in ssp.items()
                           if (k & 0xFF) != 0}
                    bits = sum(v for k, v in ssp.items() if k & ~0xFF)
                    bitsnote = ""
                    if bits:
                        m = Counter()
                        for k, v in ssp.items():
                            if k & 0x400:
                                m["secret"] += v
                            if k & 0x800:
                                m["friction"] += v
                            if k & 0x1000:
                                m["push"] += v
                            if k & 0x8000:
                                m["death"] += v
                            if k & 0x10000:
                                m["kill"] += v
                            if k & 0x300:
                                m["damage"] += v
                        bitsnote = " bits:" + ",".join(
                            f"{k}{v}" if v == 1 else f"{k}{v}x"
                            for k, v in m.items())
                    sp_note = (", ".join(f"{k & 0xFF}x{v}"
                                         for k, v in sorted(low.items())[:4])
                               if low else None)
                    if sp_note or bitsnote:
                        notes.append("sector special" + (f"s {sp_note}"
                                                         if sp_note else "")
                                     + bitsnote)
            else:
                ld = linedefs(d, blk, beha)
                if ld is None:
                    notes.append("no LINE/VERT/SIDE")
                else:
                    nlines, specials, bad, nondiv = ld
                    if nondiv:
                        notes.append(
                            f"LINEDEFS size {blk['LINEDEFS'][1]} not "
                            f"divisible by {16 if beha else 14}")
                    if bad:
                        notes.append(f"{bad} bad line refs")
                c = Counter(specials)
                if beha:
                    n3d = sum(v for k, v in c.items() if k in SP_3DFLOOR)
                    npo = sum(v for k, v in c.items() if k in SP_POLYOBJ)
                    nport = sum(v for k, v in c.items()
                                if k in SP_PORTAL)
                    nlink = sum(v for k, v in c.items() if k in SP_LINK)
                else:
                    n3d = sum(v for k, v in c.items()
                              if k in SP_3DFLOOR_DOOM)
                    npo = nport = nlink = None
                for n in ("THINGS", "SECTORS", "SIDEDEFS", "VERTEXES",
                          "BLOCKMAP"):
                    if blk.get(n) is not None and blk[n] is None:
                        notes.append(f"duplicate {n}")
                nt = blk["THINGS"][1] // 10 if blk.get("THINGS") else 0
                ns = blk["SECTORS"][1] // 26 if blk.get("SECTORS") else 0
                nsi = blk["SIDEDEFS"][1] // 30 if blk.get("SIDEDEFS") else 0
                nv = blk["VERTEXES"][1] // 4 if blk.get("VERTEXES") else 0

            # MAPINFO section for this map (full marker name = map name).
            kws = set(default_kw)
            if name in sections:
                kws |= set(sections[name][1])
            elif has_old:
                key = str(idx)
                if key in sections:
                    kws |= set(sections[key][1])
            elif name_sections:
                notes.append("no MAPI section")
            mapi_str = ",".join(sorted(kws)) or "-"

            sp_str = ",".join(
                f"{k}({KNOWN_SPECS[k]})x{v}" if k in KNOWN_SPECS else f"{k}x{v}"
                for k, v in specials.most_common(8)) or "-"

            rows.append((idx, name, beha, is_udmf, nt, nlines, ns, nsi, nv,
                         nport, nlink, n3d, npo, nplane, nsmooth, mapi_str,
                         sp_str, "; ".join(notes) or "-"))

        hdr = (f"{'pos':>3} {'map':>8} {'lay':>7} {'things':>6} {'lines':>5} "
               f"{'sec':>4} {'sides':>5} {'verts':>5} {'port':>4} {'link':>4} "
               f"{'3df':>3} {'poly':>4} {'plane':>5} {'fakec':>5}  mapinfo  "
               f"specials  notes")
        print(hdr)
        print("-" * len(hdr))
        for (i, name, beha, isudmf, nt, nl, nsec, nside, nv, nport, nlink,
             n3d, npo, nplane, nsmooth, mapi_str, sp_str, notes) in rows:
            lay = "udmf" if isudmf else ("beha16" if beha else "doom14")
            print(f"{i:>3} {name:>8} {lay:>7} {nt:>6} {nl:>5} {nsec:>4} "
                  f"{nside:>5} {nv:>5} {fmt_cell(nport):>4} "
                  f"{fmt_cell(nlink):>4} {fmt_cell(n3d):>3} "
                  f"{fmt_cell(npo):>4} {fmt_cell(nplane, '-'):>5} "
                  f"{fmt_cell(nsmooth, '-'):>5}  {mapi_str:22.22} "
                  f"{sp_str:44.44} {notes}")

        if sections:
            print(f"\nMAPINFO sections ({syntax}):")
            for key in sorted(sections,
                              key=lambda k: (int(k) if k.isdigit() else 0, k)):
                if key == "__default__":
                    continue
                title, kws = sections[key]
                kw_str = ",".join(kws) or "-"
                print(f"  {key:>10}  {title:28.28} {kw_str}")


if __name__ == "__main__":
    main()
