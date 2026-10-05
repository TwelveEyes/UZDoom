# A/B + baseline manifest (levelmesh)

Protocol: `.scratch/levelmesh-rendering/issues/09-verification-acceptance.md`.
Matrix: maps × {demo replay, static tick lists} × {gl33, vulkan} ×
{1080p, 320×200 clean, 21:9 stress} (+ gamma×glow sweep for tick lists).
This file tracks the pins; `scan_maps.py` output and the user's approval
fill the TBD sections during bring-up.

Scope note (2026-10-04, user): the classic-ports row (TNT/Ritual/Rampage/
Spectre) and doom1.wad are **dropped** from the matrix; the acceptance set
is the WADs in `tools/abtest/wads/` below.

## WAD set (drop into `build/wads/`)

| slot | file | maps used |
|---|---|---|
| doom2 | `DOOM2.WAD` | MAP01 MAP07 MAP21 MAP23 MAP27 MAP30 MAP31 |
| hexen | `HEXEN.WAD` (vanilla; scanner-verified: no feature pins) | 2–3 maps, TBD at bring-up (vanilla BEHA coverage) |
| heretic | `HERETIC.WAD` (vanilla; scanner-verified: no feature pins) | 1–2 maps, TBD at bring-up |
| myhouse | `myhouse.pk3` (user-provided; UDMF ns=zdoom; 14 nested WADs, 2 live blocks) | MAP01 20PAM |
| pirates | `Pirates!.wad` (BEHA + ZMAPINFO) | MAP50 MAP51 MAP57 (fog); MAP43 MAP49 MAP54 MAP58 (polyobjects + 3D floors) |
| sos_boom | `SOS_Boom.wad` (user-provided; Summer of Slaughter by TH1RT3EN) | MAP32 (slaughter pin); MAP12 MAP45 MAP46 (secondary scale) |
| planisf | `planisf2.wad` (user-provided; 1 map) | MAP01 (37,265 lines) |
| strife (extra) | `STRIFE1.WAD` | not in the matrix — smoke/format coverage only |

## Fixed maps (locked by ticket 09)

- **doom2**: MAP01, MAP07, MAP21, MAP23, MAP27, MAP30, MAP31
- **SOS_Boom**: MAP32 (61,623 lines / 9,907 sectors / 76,458 verts) + MAP12/45/46 secondary
- **myhouse**: MAP01, 20PAM
- **Pirates!**: MAP50, MAP51, MAP57, MAP43, MAP49, MAP54, MAP58
- **planisf2**: MAP01
- **hexen/heretic**: picks at bring-up from the scanned maps (vanilla
  coverage, no feature pins; the original category locks are re-categorized
  onto myhouse + Pirates!)

## Category pins (lock resolution 2026-10-04, user: re-categorize)

- **portals** → myhouse MAP01 (508, incl. 98× Sector_SetPortal) + 20PAM (60× Line_SetPortal)
- **sector_link** → myhouse 20PAM (7× special 107)
- **skybox** → myhouse MAP01 (5× TID-less SkyViewpoint = default skybox + 50 SkyPickers);
  Pirates! MAP50/58 also carry SkyViewpoints
- **fake contrast** → myhouse MAP01 (4804 nofakecontrast sides) + 20PAM (42)
- **fog** → Pirates! MAP50/51/57 (`fogdensity`)

The classic-format WADs (DOOM2/HEXEN/HERETIC/STRIFE/SOS_Boom/planisf2) carry
**no feature pins** by construction (line portals/sector_link/polyobjects need
BEHA layout; skybox/fog/fake-contrast need MAPINFO/UMAPINFO or UDMF) —
scanner-verified. Per-WAD feature table: ticket 09 "WAD inventory".

Verification at runtime: each perflog's `L` line (portal groups, line
portals, 3D floors, polyobjs) is cross-checked against this table and the
static scan; mismatches block the results.

## Findings (2026-10-04, `scan_maps.py` v7, validated against engine + real WADs)

- WAD header 12 B; dir entry 16 B = `pos u32, size u32, name[8]` — **names
  are the full up-to-8 chars** (e.g. `BEHAVIOR`, `LINEDEFS`, `MAP45`);
  magics `IWAD`/`WAD\x1a`/`PWAD`. A map block = marker lump (full `MAP\d+`
  or `E#M#`; UDMF custom names via a zero-size lump immediately before a
  TEXTMAP) + map-data lumps, ending at `ENDMAP` if present. Phantom markers
  (no map data) are dropped; duplicate map names are flagged — the engine
  loads the first occurrence (`CheckNumForName` first-match).
- PK3 = zip; the engine auto-mounts nested `.wad`/`.pk3` entries (mount
  order = entry order), so the scanner recurses into them. myhouse.pk3:
  14 nested WADs, 2 live map blocks (MAP01, 20PAM).
- Linedef layout is chosen **per map block exactly like the engine**
  (`p_openmap.cpp` `P_OpenMapData`): a lump whose name starts with `BEHA`
  (the actual name is `BEHAVIOR`) → `maplinedef2_t` 16 B (special `u8@6`,
  sides @12/14); else `maplinedef_t` 14 B (special `u16@6`, sides @10/12).
- **UDMF namespace semantics (udmf.cpp:2492-2560)**: the raw number space
  is `zdoom/dsda/eternity/vavoom/hexen` only; everything else
  (`doom`/`heretic`/`strife`, `zdoom_translated`, unknown or missing,
  **including `boom`/`mbf`/`hexen1`**) falls back to the game's base
  namespace and IS xlat-translated (in a Doom-family game that is the
  classic DOOM number space). Label dicts are dual: BEHA names vs xlat
  names, selected per map's effective namespace (xlat names verified
  against `wadsrc/static/xlat/base.txt` — e.g. 1 = Door_Raise, 181 =
  Plat_PerpetualRaiseLip in the DOOM space).
- MAPINFO/ZMAPINFO/UMAPINFO: the first `MAPINFO` else `ZMAPINFO` else
  `UMAPINFO` lump of every mounted file is parsed (g_mapinfo.cpp:2764);
  sections merge in mount order, later map sections win per name,
  `defaultmap` REPLACES the defaults, `adddefaultmap` ADDS to them.
  Feature keywords include `skytexture` (UMAPINFO sky key → SkyPic1,
  umapinfo.cpp:215), `fogdensity`, `outsidefogdensity`,
  `forcefakecontrast`.
- Line specials are format-dependent (engine-verified): BEHA maps — no
  translator, raw numbers are the ZDoom/Hexen numbers (3D floors 160/50,
  polyobjects 1–9/59/86–93/283, portals 156/301/57, sector_link 107/51);
  DOOM format — raw numbers run through the game's xlat translator
  (`xlat/base.txt`; 3D floors 281/289/300–306/332/400–417). Specials
  outside the table are **zeroed on load** (p_xlat.cpp:123-126) — SOS_Boom's
  custom 4-digit specials (12184–25688, packed `K*1024+offset`) are all
  of that kind. STRIFE has no BEHA → loads as DOOM format with the
  strife.txt translator.
- **Skybox signal** (wadsrc/.../actors/shared/skies.zs + `SpawnSkybox`):
  SkyViewpoint (9080; ZDoom 4434) **with no TID becomes the default 3D
  skybox**; SkyPicker (9081/4435) references one by TID; EE-style skybox =
  SkyCamCompat (9082/4436) + a `Sector_SetPortal` (57) line with
  `args[1]==2` in its sector. Binary skybox requires BEHA layout (the xlat
  never maps 57).
- UZDoom has no `FF*` (3D-floor) or `POLYOBJ` lump readers anywhere in
  `src/` (verified by grep) — 3D floors/polyobjects exist only via line
  specials.

## Resolutions

| name | size | notes |
|---|---|---|
| 1080p | 1920×1080 | vsync off (baseline.cfg) |
| 320x200 | 320×200 | clean aspect |
| 21x9 | 3440×1440 | stress widescreen |

## Reference machine

TBD at bring-up: OS/kernel, CPU, GPU + driver, build type.
Recorded facts from 2026-10-04 (dev box, likely the reference machine):
Ryzen 9 5900X-class, Radeon RX 7900 XT-class, 32 GB RAM, Linux
7.2.9-arch1-1 (arch), UZDoom build RelWithDebInfo.

## gl_backend values

TBD at bring-up: the exact `gl_backend` cvar values for GL 3.3 core and
Vulkan (confirm with `stat`/console at first launch).
