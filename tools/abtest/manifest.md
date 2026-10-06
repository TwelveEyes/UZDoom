# A/B + baseline manifest (levelmesh)

Protocol: `.scratch/levelmesh-rendering/issues/09-verification-acceptance.md`.
Matrix: maps × {demo replay, static tick lists} × {gl33, vulkan} @ 2560×1440
host-native (+ gamma×glow sweep for tick lists). The resolution axis was
dropped 2026-10-05 (user decision; see Resolutions below).
This file tracks the pins; `scan_maps.py` output and the user's approval
fill the TBD sections during bring-up.

Scope note (2026-10-04, user): the classic-ports row (TNT/Ritual/Rampage/
Spectre) and doom1.wad are **dropped** from the matrix; the acceptance set
is the WADs in `tools/abtest/wads/` below.

## WAD set (drop into `build/wads/`)

| slot | file | maps used |
|---|---|---|
| doom2 | `DOOM2.WAD` | MAP01 MAP07 MAP21 MAP23 MAP27 MAP30 MAP31 |
| hexen | `HEXEN.WAD` (vanilla; scanner-verified: no feature pins; note the 170 vanilla polyobject lines) | MAP01 MAP10 MAP27 |
| heretic | `HERETIC.WAD` (vanilla; scanner-verified: no feature pins) | E1M2 E5M6 |
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
- **hexen**: MAP01 (1,770 lines / 400 sec; densest 80-teleports 80×42,
  polyobj 1×12), MAP10 (2,030 lines / 337 sec; mixed special family), MAP27
  (2,180 lines / 368 sec — largest of the three; 80×38, 62×9) — vanilla
  BEHA-format coverage (pinned 2026-10-04, phase 1, from scan data)
- **heretic**: E1M2 (1,128 lines / 247 sec; door density), E5M6 (1,975 lines
  / 416 sec — most sectors in the WAD; plat density) — vanilla DOOM-format
  coverage (pinned 2026-10-04, phase 1, from scan data)

## Category pins (lock resolution 2026-10-04, user: re-categorize)

- **portals** → myhouse MAP01 (508, incl. 98× Sector_SetPortal) + 20PAM (60× Line_SetPortal)
- **sector_link** → myhouse 20PAM (7× special 107)
- **skybox** → myhouse MAP01 (5× TID-less SkyViewpoint = default skybox + 50 SkyPickers)
  (Pirates! MAP50/58 were previously listed here — scanner v8 proved that a
  false positive: the old skybox() read the BEHA thing type at the classic
  offset, i.e. the y coordinate; ground truth has zero sky actors there)
- **fake contrast** → myhouse MAP01 (4804 nofakecontrast sides) + 20PAM (42)
- **fog** → Pirates! MAP50/51/57 (`fogdensity`)

The classic-format WADs (DOOM2/HEXEN/HERETIC/STRIFE/SOS_Boom/planisf2) carry
**no category pins** by construction (line portals/sector_link need BEHA
layout; skybox/fog/fake-contrast need MAPINFO/UMAPINFO or UDMF) —
scanner-verified. Per-WAD feature table: ticket 09 "WAD inventory". One
exception to "no polyobjects": vanilla HEXEN carries 170 polyobject lines
(special 1 = Polyobj_StartLine, playsim/actionspecials.h:25) spread over all
31 maps — that is format coverage for the L-line cross-check, not a category
pin.

Verification at runtime: each perflog's `L` line (portal groups, line
portals, 3D floors, polyobjs) is cross-checked against this table and the
static scan; mismatches block the results.

## Findings (2026-10-05, `scan_maps.py` v8 corrections, engine-verified)

- **Binary THINGS layout branches like the engine** (maploader.cpp:3014-3018,
  `HasBehavior`): BEHA blocks → `mapthinghexen_t` 20 B (x@2, y@4, type i16@10);
  classic → `mapthing_t` 10 B (x@0, y@2, type i16@6). v7 read type at the
  classic offset of a 10 B record for all binary maps, which made BEHA
  "skybox" counts read y coordinates (the Pirates! MAP50/58 false positive
  above) and doubled BEHA thing counts. v8 reads per layout in `skybox()`
  and `scan_file` (nt = size/20 for BEHA). BEHA line/sector/sidedef strides
  were re-verified against the engine's L lines and data: 16 B lines, 26 B
  sectors (`mapsector_t`, doomdata.h:232), 30 B sidedefs (`mapsidedef_t`,
  doomdata.h:79) — all as v7 had them.
- `make_demo.py`'s UDMF reader handles both header styles (bare `type` line
  + separate `{` as in SLADE output, and `type {` / `type // id` on one
  line); a thing position may be `position = x, y[, z]` or separate x/y.
  (myhouse MAP01's 55 MB TEXTMAP parses in ~1 s.)

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

## Demo set (synthesized, 2026-10-05)

`demos/*.lmp` — 26 deterministic camera demos generated by
`make_demo.py` (one per fixed map; names `<WAD>_<MAP>.lmp`, lowercase
`E#M#` → `E1M2` style for Heretic). Not hand-played: a seeded travel-to-
spiral path computed from each map's real geometry (line midpoints +
player start) and the engine's exact tic model
(`vel += cmd/8192` in the view basis, `vel *= 29/32`, `pos += vel`), so
the camera flies the whole map, turns are capped by K·(0.85/0.9) of max
speed, and replay is frame-identical across code changes (fixed
`demo_version 114`, seed `0xa1b2c3d4`, `demo_compression`, `-nomonsters`)
except where a map's own scripted events diverge the simulation —
detectable from the `L` line (map name change) in the perflog.
All demos are `tics=2600` (350 warmup + 2100 window). Generated with
`noclip2` (CHT_NOCLIP2 = 50) so the camera is never blocked by geometry.

Regenerate one: `python3 tools/abtest/make_demo.py --iwad build/wads/HEXEN.WAD
--map MAP10 --out tools/abtest/demos/HEXEN_MAP10.lmp --tics 2600`
(see the README step 2 for the full batch).

Replay-verified 2026-10-05 on the current build (RelWithDebInfo,
640×400, `-timedemo`): every demo loads, plays the full 2600 tics, and
exits 255 (normal timedemo termination, NOT a crash). Spot-checked:
HEXEN_MAP10 `L` line = sectors 337 / lines 2030 / polyobjs 7 (matches
the pins above); MYHOUSE_MAP01 `L` line = lines 165,922 / sectors 35,853
/ lineportals 410 / portalgroups 89 / ffloors 9,812 (the L-line
portal split 410+89 vs the static 508 is a cross-check item for
phase 4).

## Resolutions (axis dropped 2026-10-05, user decision)

| name | size | notes |
|---|---|---|
| host-native | 2560×1440 | reference machine's DP-1 panel (vsync off per baseline.cfg); a 1920×1080 panel is also connected (virtual desktop 4480×1440). Every run pins `-width 2560 -height 1440`.

Rationale: the calibration run on DOOM2_MAP01 (GL33, 2600 tics, window
tics 350–2450) showed per-frame cost identical at 320×200, 1920×1080, and
3440×1440 — med ≈ 0.235 ms, p95 ≈ 0.26 ms, max ≈ 6.6 ms; a 2.4× pixel
difference with zero cost difference. Hypothesis: timedemo is
CPU-bound (scene graph / draw-call side) at that scene size, so GPU
fill-rate differences are hidden; the axis was dropped rather than
verified on the large maps (myhouse MAP01 / planisf2 MAP01), since the
user preferred a single fixed host-native resolution for the baseline.

## Reference machine

Confirmed 2026-10-04 (bring-up ran on the dev box — it IS the reference machine):
- OS: Void Linux, kernel 7.2.9_1, Wayland session (wayland-1) — display is
  available in-session, so the engine can be launched from the agent
- CPU: AMD Ryzen 9 5900X, 12C/24T, max ~4.95 GHz
- GPU: AMD Navi 31 (PCI 1002:744c, RX 7900 class), amdgpu kernel driver,
  Mesa userspace (libvulkan_radeon)
- RAM: 31 GiB
- Build: RelWithDebInfo (this tree)

## Backend cvar (resolved 2026-10-04, from source)

The cvar is **`vid_preferbackend`** (v_video.cpp:85) — NOT `gl_backend`
(that name does not exist anywhere in `src/`):

- `0` = OpenGL 3.3 (`BACKEND_OPENGL`)
- `1` = Vulkan (`BACKEND_VULKAN`)

Enum at v_video.h:70-73; the cvar handler prints "Selecting Vulkan
backend..." / "Selecting OpenGL backend...". Switching backends requires a
fresh launch (all perf runs are fresh launches anyway).
