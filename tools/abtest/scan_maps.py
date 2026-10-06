#!/usr/bin/env python3
"""
scan_maps.py — levelmesh A/B: map inventory + feature scan (dev-machine gate).

Pins the hexen/heretic/classic-ports rows of manifest.md to maps that actually
carry each required feature (portals / sector_link / skybox / fog / fake-contrast).

Usage:
    python3 scan_maps.py <WAD-or-PK3> [<WAD-or-PK3> ...]

Layouts, keywords and special numbers are NOT guessed — each was verified
against UZDoom's engine source and/or the WADs themselves:

  * WAD header: 12 bytes (magic, numlumps, dir_offset). Directory entry:
    16 bytes (pos u32, size u32, name[8] NUL-padded). Names are used in full;
    a map marker is a lump named exactly MAP##.. (any digit count) / E#M#.
  * PK3 = zip. The engine auto-mounts nested .wad/.pk3 entries found inside a
    PK3, so the scanner recurses into them (mount order = entry order) and
    scans each nested WAD as its own file. Entry names may be full paths.
  * A map block = marker lump, then map-data lumps until the next marker or an
    ENDMAP lump (ZDoom convention, used by UDMF packs). Besides the classic
    names, a zero-size lump immediately followed by a TEXTMAP lump is a marker
    — the UDMF convention, which also catches custom map names (e.g. 20PAM).
    A marker whose block carries no map data (e.g. a graphics patch happening
    to be named MAP03) is a phantom and is dropped from the inventory.
    Duplicate map names: the engine loads the first occurrence (p_openmap.cpp
    CheckNumForName is a first-match lookup); later copies are flagged.
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
  * MAPINFO: the first MAPINFO else ZMAPINFO else UMAPINFO lump of every
    mounted file is
    parsed and merged in mount order with engine semantics (g_mapinfo.cpp):
    later map sections win per name; `defaultmap` REPLACES the defaults,
    `adddefaultmap` ADDS to them; `defaultmap`/`adddefaultmap` keywords apply
    to every map. ZMAPINFO (new syntax: `map MAP41 "t"`,
    `map X lookup "HUSTR"`) and the old syntax (`map 1 {...}`, where the key
    is the map's position in that file's map list) are both parsed.
    `gameinfo`/`automap`/`DoomEdNums` etc. are skipped.
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
import contextlib
import io
import os
import re
import struct
import sys
import zipfile
from collections import Counter

WAD_MAGICS = (b"IWAD", b"WAD\x1a", b"PWAD")

# ZMAPINFO top-level section starters (any of these ends a map/defaultmap
# keyword capture; only map/defaultmap capture keywords).
SECTION_KEYWORDS = {
    "gameinfo", "defaultmap", "adddefaultmap", "defaultskill",
    "clearskills", "clearautomapcolorsets", "clearepisodes",
    "clearexits", "clearintermissions", "skill", "map", "automapcolorset",
    "automap", "intermission", "doomednums", "spawnnums",
    "conversationids", "damagetype",
}

# Feature keywords (MAPINFO, map section or defaultmap).
MINFO_KEYWORDS = [
    "skybox", "sky1", "sky2", "sky3", "skytexture", "fakecontrast",
    "forcefakecontrast", "fogdensity", "outsidefogdensity", "fog",
    "sectorlink", "sector_link", "portal", "portals",
]

# UDMF object types that carry map geometry.
UDMF_TYPES = {"thing", "vertex", "sidedef", "linedef", "sector", "script"}

# UDMF namespaces whose line specials use a raw number space (no xlat),
# per udmf.cpp:2492-2560: zdoom/dsda/eternity/vavoom and hexen set
# isTranslated=false. Everything else (doom/heretic/strife,
# zdoom_translated, unknown or missing namespaces like boom/mbf/hexen1)
# falls back to the game's base namespace and IS xlat-translated (in a
# Doom-family game that is the classic DOOM number space).
UDMF_MODERN_NS = {"zdoom", "dsda", "eternity", "vavoom", "hexen"}

# Special numbers (UZDoom playsim/actionspecials.h — engine-verified).
# BEHA (Hexen-format) maps and UDMF (modern namespace): raw numbers are
# ZDoom specials.
SP_3DFLOOR = {160, 50}
SP_POLYOBJ = set(range(1, 10)) | {59, 86, 87, 88, 89, 90, 91, 92, 93, 283}
SP_PORTAL = {57, 156, 301}
SP_LINK = {107, 51}
# Skybox signals (wadsrc/static/zscript/actors/shared/skies.zs +
# maploader/specials.cpp SpawnSkybox): a SkyViewpoint thing (9080; ZDoom's
# 4434) with no TID becomes the DEFAULT 3D skybox; SkyPicker (9081/4435)
# references one by TID; the EE-style skybox is a SkyCamCompat (9082/4436)
# thing + a Sector_SetPortal (57) line with args[1]==2 in its sector.
SKY_VIEW = {9080, 4434}
SKY_PICK = {9081, 4435}
SKY_CAM = {9082, 4436}
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
# xlat (classic DOOM) number space — names verified against
# wadsrc/static/xlat/base.txt. DOOM-format binary maps and UDMF
# classic-namespace maps use this space (1 = Door_Raise there, NOT
# Polyobj_StartLine; 181 = Plat_PerpetualRaiseLip, not Plane_Align).
KNOWN_SPECS_XLAT = {1: "Door_Raise", 2: "Door_Open", 3: "Door_Close",
                   16: "Door_CloseWaitOpen", 17: "Light_StrobeDoom",
                   48: "Scroll_Texture_Left", 52: "Exit_Normal",
                   62: "Plat_DownWaitUpStayLip",
                   73: "Ceiling_CrushAndRaise",
                   85: "Scroll_Texture_Right",
                   88: "Plat_DownWaitUpStayLip",
                   97: "Teleport", 109: "Door_Open",
                   117: "Door_Raise", 125: "Teleport",
                   126: "Teleport", 130: "Floor_RaiseToNearest",
                   181: "Plat_PerpetualRaiseLip", 242: "Transfer_Heights",
                   255: "Scroll_Texture_Offsets",
                   260: "TranslucentLine", 261: "Transfer_CeilingLight",
                   269: "Teleport_NoFog", 271: "Static_Init"}


def parse_wad_dir(data):
    nlumps, dir_off = struct.unpack_from("<II", data, 4)
    lumps = []
    for i in range(nlumps):
        off = dir_off + i * 16
        pos, size = struct.unpack_from("<II", data, off)
        name = data[off + 8:off + 16].rstrip(b"\0").decode("ascii", "replace")
        lumps.append((name, pos, size))
    return lumps


def open_archive_bytes(data, label, depth=0) -> list:
    """One archive -> list of mounted files in engine mount order. Each file
    is (label, kind, data, lumps, zf): kind='wad' has data=bytes and lumps
    (name, offset, size); kind='pk3' has data=None, zf=ZipFile and lumps
    (name, entry_index, size) — entry data is read lazily via lump_bytes().
    Nested .wad/.pk3 entries (auto-mounted by the engine) are appended in
    entry order, recursively (depth-limited)."""
    if data[0:4] in WAD_MAGICS:
        return [(label, "wad", data, parse_wad_dir(data), None)]
    zf = zipfile.ZipFile(io.BytesIO(data))
    infolist = zf.infolist()
    lumps = [(in_.filename, i, in_.file_size) for i, in_ in enumerate(infolist)
             if not in_.filename.endswith("/")]
    files = [(label, "pk3", None, lumps, zf)]
    if depth < 3:
        for in_ in infolist:
            n = in_.filename
            if n.endswith("/") or not n.lower().endswith((".wad", ".pk3")):
                continue
            with zf.open(in_) as fh:
                sub = fh.read()
            if sub[0:4] in WAD_MAGICS or sub[0:4] == b"PK\x03\x04":
                files.extend(open_archive_bytes(sub, f"{label}/{n}", depth + 1))
    return files


def open_archive(path):
    try:
        with open(path, "rb") as f:
            d = f.read()
    except OSError as e:
        raise SystemExit(f"cannot read {path}: {e}") from e
    if d[0:4] not in WAD_MAGICS and d[0:4] != b"PK\x03\x04":
        raise SystemExit(f"not a WAD or PK3: {path} (magic {d[0:4]!r})")
    return open_archive_bytes(d, os.path.basename(path))


def lump_bytes(file, loc):
    """Read the data of one lump entry (WAD offset or PK3 zip entry)."""
    key, size = loc
    _, kind, data, _, zf = file
    if kind == "wad":
        return data[key:key + size]
    with zf.open(zf.infolist()[key]) as fh:
        return fh.read()[:size]


def merge_mapinfo(files):
    """Merge every file's first MAPINFO (else ZMAPINFO) with engine semantics
    (g_mapinfo.cpp): later map sections win per name; `defaultmap` replaces
    the defaults, `adddefaultmap` adds to them.
    Returns (sections, defaults, syntax, source_labels)."""
    sections = {}
    defaults = set()
    syn_new = syn_old = False
    srcs = []
    for file in files:
        blob = None
        # Engine per-WAD priority (g_mapinfo.cpp:2764): MAPINFO > ZMAPINFO
        # > UMAPINFO; UMAPINFOs from different WADs accumulate.
        for cand in ("MAPINFO", "ZMAPINFO", "UMAPINFO"):
            for (n, key, size) in file[3]:
                if n == cand:
                    blob = lump_bytes(file, (key, size))
                    srcs.append(f"{file[0]}:{cand}")
                    break
            if blob is not None:
                break
        if blob is None:
            continue
        secs, def_kw, add_kw, syn = parse_mapinfo(blob)
        sections.update(secs)
        if def_kw is not None:
            defaults = set(def_kw)
        defaults |= add_kw
        if syn in ("new", "old+new"):
            syn_new = True
        if syn in ("old", "old+new"):
            syn_old = True
    if syn_new and syn_old:
        syntax = "old+new"
    elif syn_new:
        syntax = "new"
    elif syn_old:
        syntax = "old"
    else:
        syntax = "none"
    return sections, defaults, syntax, srcs


def map_blocks(lumps):
    """Marker lumps -> following lumps until the next marker or ENDMAP.
    A marker is: (a) a lump named exactly MAP##.. (any digit count) or E#M# —
    the classic map names; or (b) a zero-size lump immediately followed by a
    TEXTMAP lump — the UDMF convention, which also catches custom map names
    (e.g. 20PAM). ENDMAP is never a marker. Returns (blocks, phantoms) where
    blocks is a list of (marker_name, {name: (pos, size)}) and phantoms the
    marker names whose block carried no map data."""
    mkr = []
    for i, (n, _pos, s) in enumerate(lumps):
        if n == "ENDMAP":
            continue
        if re.fullmatch(r"MAP\d+|E\dM\d", n) or (
                s == 0 and i + 1 < len(lumps)
                and lumps[i + 1][0] == "TEXTMAP"):
            mkr.append(i)
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

    Returns (sections, default_kw, adddefault_kw, syntax) where sections maps
    key(str) -> (title, sorted keyword list), default_kw is the `defaultmap`
    keyword set or None when the lump has no defaultmap section (it REPLACES,
    g_mapinfo.cpp), adddefault_kw is the `adddefaultmap` set (it ADDS),
    syntax is 'old'|'new'|'old+new'|'none'. `map X lookup "HUSTR"` headers
    keep their map name as key with the lookup name as title.
    """
    txt = data.decode("latin-1")
    # strip // line comments (kept simple: MAPINFO has no // in strings here)
    lines = [re.sub(r"//.*$", "", ln).strip() for ln in txt.splitlines()]

    sections = {}
    default_kw = None
    add_kw = set()
    cur = None  # (kind, key) where kind is 'map', 'defaultmap', 'adddefault'
    new = old = False
    for ln in lines:
        if not ln:
            continue
        if ln == "}":
            cur = None  # section closed: unknown top-level sections must not
            continue    # leak keywords into the previous section
        head = ln.split(None, 1)[0].lower()
        if head == "map":
            m = re.match(r'map\s+(\S+)(?:\s+lookup\s+"([^"]*)")?'
                         r'(?:\s+"([^"]*)")?', ln)
            if not m:
                cur = None
                continue
            key = m.group(1)
            title = m.group(2) or m.group(3) or ""
            if key.isdigit():
                old = True
            else:
                new = True
            cur = ("map", key)
            sections.setdefault(key, [title, []])
        elif head == "defaultmap":
            new = True
            cur = ("defaultmap", None)
            if default_kw is None:
                default_kw = set()
        elif head == "adddefaultmap":
            new = True
            cur = ("adddefault", None)
        elif head in SECTION_KEYWORDS:
            cur = None
        else:
            if cur is None:
                continue
            if cur[0] == "map":
                for kw in MINFO_KEYWORDS:
                    if re.search(rf"\b{kw}\b", ln, re.I):
                        sections[cur[1]][1].append(kw)
            else:
                if cur[0] == "adddefault":
                    tgt = add_kw
                else:
                    if default_kw is None:  # defensive; set on section head
                        default_kw = set()
                    tgt = default_kw
                for kw in MINFO_KEYWORDS:
                    if re.search(rf"\b{kw}\b", ln, re.I):
                        tgt.add(kw)
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
    return sections, default_kw, add_kw, syntax


def linedefs(file, blk, beha):
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
    data = lump_bytes(file, blk["LINEDEFS"])
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
    n_nofc (sidedefs with nofakecontrast), and skybox signals
    n_view/n_pick/n_cam (SkyViewpoint/SkyPicker/SkyCamCompat things) and
    n_skyline (Sector_SetPortal lines with arg1==2 — EE-style skybox setup).
    Note UDMF linedef args are 0-based (arg0..arg4, maploader/udmf.cpp).

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
        "n_view": 0,
        "n_pick": 0,
        "n_cam": 0,
        "n_skyline": 0,
    }
    cur = None        # current object type
    in_obj = False
    n_fplane = 0      # floorplane_* props seen in the current sector
    n_cplane = 0      # ceilingplane_* props seen in the current sector
    sec_portal = False
    ld_sp = None      # special of the current linedef
    ld_a1 = None      # arg1 of the current linedef
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
                ld_sp = ld_a1 = None
            continue
        if s == "}":
            if in_obj and cur == "sector":
                if n_fplane >= 4 or n_cplane >= 4:
                    res["n_planes"] += 1
                if sec_portal:
                    res["n_portal_sec"] += 1
            if in_obj and cur == "linedef" and ld_sp == 57 and ld_a1 == 2:
                res["n_skyline"] += 1
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
        if cur == "linedef":
            if key == "special":
                try:
                    v = int(val)
                except ValueError:
                    v = None
                if v:
                    res["line_specials"][v] += 1
                ld_sp = v
            elif key == "arg1":
                with contextlib.suppress(ValueError):
                    ld_a1 = int(val)
        elif cur == "thing" and key == "type":
            try:
                t = int(val)
            except ValueError:
                t = None
            if t in SKY_VIEW:
                res["n_view"] += 1
            elif t in SKY_PICK:
                res["n_pick"] += 1
            elif t in SKY_CAM:
                res["n_cam"] += 1
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
        elif cur == "sidedef" and val in ("true", "1"):
            if key == "smoothlighting":
                res["n_smooth"] += 1
            elif key == "nofakecontrast":
                res["n_nofc"] += 1
    return res


def skybox(file, blk, beha):
    """Skybox signals in a BINARY map block: SkyViewpoint/SkyPicker/
    SkyCamCompat things and, in BEHA-layout blocks only, Sector_SetPortal
    (57, u8@6) lines with args[1]==2 (u8@8). Thing records are read exactly
    like the engine (maploader.cpp:3014-3018 branches on HasBehavior):
    BEHA blocks -> mapthinghexen_t 20 B (type i16@10); classic ->
    mapthing_t 10 B (type i16@6). DOOM-format (14 B) maps cannot carry
    skyboxes — the xlat never maps 57.
    Returns (view, pick, cam, sky_lines) or None when no THINGS/LINEDEFS."""
    if blk.get("THINGS") is None and blk.get("LINEDEFS") is None:
        return None
    view = pick = cam = sky = 0
    if blk.get("THINGS") is not None:
        d = lump_bytes(file, blk["THINGS"])
        if beha:
            for i in range(len(d) // 20):
                t = struct.unpack_from("<h", d, i * 20 + 10)[0]
                if t in SKY_VIEW:
                    view += 1
                elif t in SKY_PICK:
                    pick += 1
                elif t in SKY_CAM:
                    cam += 1
        else:
            for i in range(len(d) // 10):
                t = struct.unpack_from("<h", d, i * 10 + 6)[0]
                if t in SKY_VIEW:
                    view += 1
                elif t in SKY_PICK:
                    pick += 1
                elif t in SKY_CAM:
                    cam += 1
    if beha and blk.get("LINEDEFS") is not None:
        d = lump_bytes(file, blk["LINEDEFS"])
        for i in range(len(d) // 16):
            p = i * 16
            if d[p + 6] == 57 and d[p + 8] == 2:
                sky += 1
    return (view, pick, cam, sky)


def fmt_cell(v, na="n/a"):
    """Feature-column cell: None renders as n/a (signal not available in
    this map format)."""
    return na if v is None else str(v)


def scan_file(file, sections, defaults, syntax, has_old, name_sections,
              show_header):
    """Inventory one mounted file's map blocks; returns row list."""
    label, kind, data, lumps, zf = file
    blocks, phantoms = map_blocks(lumps)
    if show_header or not blocks:
        print(f"\n--- {label} ({kind}, {len(lumps)} lumps) ---")
    if not blocks:
        if kind == "pk3":
            print("no map blocks in top-level entries")
        else:
            print("no map blocks")
        return
    ph = f"  phantoms={len(phantoms)} {phantoms}" if phantoms else ""
    print(f"maps={len(blocks)}{ph}")

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
            is_udmf = blk.get("TEXTMAP") is not None and \
                blk["TEXTMAP"] is not None
            if is_udmf and blk.get("LINEDEFS") is not None:
                notes.append("TEXTMAP+binary (engine uses UDMF)")

            specials = Counter()
            nlines = nt = ns = nsi = nv = 0
            nport = nlink = n3d = npo = nplane = nsmooth = nfake = None
            modspace = False  # raw modern/BEHA number space (vs xlat)
            if is_udmf:
                u = udmf(lump_bytes(file, blk["TEXTMAP"]))
                nview, npick, ncam, nskyl = (u["n_view"], u["n_pick"],
                                             u["n_cam"], u["n_skyline"])
                nt = u["counts"].get("thing", 0)
                nlines = u["counts"].get("linedef", 0)
                ns = u["counts"].get("sector", 0)
                nsi = u["counts"].get("sidedef", 0)
                nv = u["counts"].get("vertex", 0)
                specials = u["line_specials"]
                modern = u["namespace"] in UDMF_MODERN_NS
                modspace = modern
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
                nfake = u["n_nofc"] or None
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
                ld = linedefs(file, blk, beha)
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
                sb = skybox(file, blk, beha)
                nview, npick, ncam, nskyl = sb if sb else (0, 0, 0, 0)
                c = Counter(specials)
                modspace = beha
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
                # BEHA blocks: 20 B mapthinghexen_t (engine LoadThings2);
                # classic: 10 B mapthing_t (LoadThings).
                tstride = 20 if beha else 10
                nt = blk["THINGS"][1] // tstride if blk.get("THINGS") else 0
                ns = blk["SECTORS"][1] // 26 if blk.get("SECTORS") else 0
                nsi = blk["SIDEDEFS"][1] // 30 if blk.get("SIDEDEFS") else 0
                nv = blk["VERTEXES"][1] // 4 if blk.get("VERTEXES") else 0
                # nofakecontrast is a UDMF sidedef property; binary maps
                # cannot carry it.
                nfake = None
            if nview or npick or ncam or nskyl:
                nsky = nview + nskyl
                bits = [f"{nview} view"]
                if npick:
                    bits.append(f"{npick} pick")
                if ncam:
                    bits.append(f"{ncam} cam")
                if nskyl:
                    bits.append(f"{nskyl} ee-line")
                notes.append("skybox: " + "/".join(bits))
            else:
                nsky = None

            # MAPINFO section for this map (full marker name = map name).
            kws = set(defaults)
            if name in sections:
                kws |= set(sections[name][1])
            elif has_old:
                key = str(idx)
                if key in sections:
                    kws |= set(sections[key][1])
            elif name_sections:
                notes.append("no MAPI section")
            mapi_str = ",".join(sorted(kws)) or "-"

            labels = KNOWN_SPECS if modspace else KNOWN_SPECS_XLAT
            sp_str = ",".join(
                f"{k}({labels[k]})x{v}" if k in labels else f"{k}x{v}"
                for k, v in specials.most_common(8)) or "-"

            rows.append((idx, name, beha, is_udmf, nt, nlines, ns, nsi, nv,
                         nport, nlink, n3d, npo, nplane, nsky, nsmooth,
                         nfake, mapi_str, sp_str, "; ".join(notes) or "-"))

    hdr = (f"{'pos':>3} {'map':>8} {'lay':>7} {'things':>6} {'lines':>5} "
           f"{'sec':>4} {'sides':>5} {'verts':>5} {'port':>4} {'link':>4} "
           f"{'3df':>3} {'poly':>4} {'plane':>5} {'sky':>4} {'smth':>5} "
           f"{'fakec':>5}  mapinfo  specials  notes")
    print(hdr)
    print("-" * len(hdr))
    for (i, name, beha, isudmf, nt, nl, nsec, nside, nv, nport, nlink,
         n3d, npo, nplane, nsky, nsmooth, nfake, mapi_str, sp_str,
         notes) in rows:
        lay = "udmf" if isudmf else ("beha16" if beha else "doom14")
        print(f"{i:>3} {name:>8} {lay:>7} {nt:>6} {nl:>5} {nsec:>4} "
              f"{nside:>5} {nv:>5} {fmt_cell(nport):>4} "
              f"{fmt_cell(nlink):>4} {fmt_cell(n3d):>3} "
              f"{fmt_cell(npo):>4} {fmt_cell(nplane, '-'):>5} "
              f"{fmt_cell(nsky, '-'):>4} {fmt_cell(nsmooth, '-'):>5} "
              f"{fmt_cell(nfake, '-'):>5}  "
              f"{mapi_str:22.22} {sp_str:44.44} {notes}")


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for path in sys.argv[1:]:
        files = open_archive(path)
        sections, defaults, syntax, mapi_srcs = merge_mapinfo(files)
        name_sections = {k for k in sections
                         if not re.fullmatch(r"\d+", k)}
        has_old = any(re.fullmatch(r"\d+", k) for k in sections)

        if len(files) == 1:
            label, kind, data, lumps, zf = files[0]
            desc = f"WAD: {len(lumps)} lumps" if kind == "wad" \
                else f"zip: {len(lumps)} entries"
        else:
            desc = (f"zip: {len(files[0][3])} entries, "
                    f"{len(files) - 1} nested archive(s) mounted")
        mapi_hdr = ("yes (" + ", ".join(mapi_srcs) + ")"
                    if mapi_srcs else "no")
        nsec_map = len(sections)
        print(f"\n=== {os.path.basename(path)} ===  {desc}")
        print(f"MAPINFO={mapi_hdr}"
              + (f" (syntax={syntax}, sections={nsec_map}, "
                 f"defaultmap-kw={','.join(sorted(defaults)) or '-'})"
                 if mapi_srcs else ""))

        for file in files:
            scan_file(file, sections, defaults, syntax, has_old,
                      name_sections, show_header=len(files) > 1)

        if sections:
            print(f"\nMAPINFO sections ({syntax}):")
            for key in sorted(sections,
                              key=lambda k: (int(k) if k.isdigit() else 0, k)):
                title, kws = sections[key]
                kw_str = ",".join(kws) or "-"
                print(f"  {key:>10}  {title:28.28} {kw_str}")


if __name__ == "__main__":
    main()
