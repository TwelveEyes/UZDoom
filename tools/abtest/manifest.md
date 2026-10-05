# A/B + baseline manifest (levelmesh)

Protocol: `.scratch/levelmesh-rendering/issues/09-verification-acceptance.md`.
Matrix: maps × {demo replay, static tick lists} × {gl33, vulkan} ×
{1080p, 320×200 clean, 21:9 stress} (+ gamma×glow sweep for tick lists).
This file tracks the pins; `scan_maps.py` output and the user's approval
fill the TBD sections during bring-up.

## WAD set (drop into `build/wads/`)

| slot | file (TBD at bring-up) | maps used |
|---|---|---|
| doom1 | `doom1.wad` (registered, for E3/E4) | E1M2 E1M8 E2M2 E3M7 E4M1 |
| doom2 | `doom2.wad` | MAP01 MAP07 MAP21 MAP23 MAP27 MAP30 MAP31 |
| hexen | `hexen.wad` (vanilla, verified: no required features) + a feature-rich WAD (TBD, user-supplied) | TBD (category-locked, see findings) |
| heretic | `heretic.wad` | TBD |
| tnt | `tnt.wad` | TBD |
| ritual | `ritual.wad` | TBD |
| rampage | `rampage.wad` | TBD |
| spectre | `spectre.wad` | TBD (fake-contrast reference) |
| sos_booM | user-named WAD (TBD at bring-up) | the slaughter-scale map (TBD) |

## Fixed maps (locked by ticket 09)

- **doom1**: E1M2, E1M8, E2M2, E3M7, E4M1
- **doom2**: MAP01, MAP07, MAP21, MAP23, MAP27, MAP30, MAP31
- **SOS_Boom**: one slaughter-scale map — name pinned at bring-up.

## Category-locked pins (verify-first: scan → propose → approve)

- **hexen** (4–6 maps): ≥ 2 line portals (BEHA specials 156/301), 1
  sector_link (BEHA special 107/51), 1 skybox, 1 fog (MAPINFO
  `fogdensity`), 1 fake contrast (MAPINFO `forcefakecontrast`).
  **BLOCKED** — verified across every WAD on hand (scan v3, 2026-10-04):
  none has line portals, sector_link, a 3D skybox, or `forcefakecontrast`.
  The only locked feature present anywhere is **fog → Pirates! MAP50/51/57**
  (a DOOM-family PWAD, not a hexen WAD — usable only if the user approves
  re-categorizing or designating Pirates! as the feature WAD). All five
  features require ZDoom format (BEHA layout and/or MAPINFO), so the pinning
  WAD must be ZDoom-format; classic as-distributed WADs cannot carry them
  (see Findings).
- **heretic** (1–2): skybox variant, portals — **impossible in classic
  HERETIC** (DOOM-format, no MAPI; specials translated via xlat/heretic.txt).
  The pin must come from a user-supplied WAD.
- **tnt** (1–2): 3D-floor density (achievable — classic WADs carry 3D floors
  via the xlat/base.txt numbers), portals (impossible in a classic WAD —
  see-through portals are Hexen-format-only). (file not yet on hand)
- **ritual** (1–2): 3D-floor density (achievable), fake contrast (requires
  MAPINFO — impossible in the as-distributed WAD). (file not on hand)
- **rampage** (1–2): portals (impossible in a classic WAD), fake contrast
  (requires MAPINFO). (file not yet on hand)
- **spectre** (1–2): fake contrast (reference implementation).
  (file not yet on hand — spectre is the fake-contrast reference, so this
  file is a hard requirement, not just a pin; if it is the as-distributed
  classic WAD it carries no MAPINFO and cannot demonstrate
  `forcefakecontrast` — the reference then has to come from the
  user-supplied ZDoom-format WAD)
- **Consequence for the ticket-09 category locks**: the *portals* category
  on rampage and *fake contrast* on ritual/rampage/spectre (and *skybox*
  + *portals* on heretic) cannot be satisfied by classic as-distributed
  WADs by construction. Flagged for re-confirmation at pin approval.

Verification at runtime: each perflog's `L` line (portal groups, line
portals, 3D floors, polyobjs) is cross-checked against this table and the
static scan; mismatches block the results.

## Findings (2026-10-04, `scan_maps.py` validated against engine + real WADs)

- `scan_maps.py` v4 is engine-verified, not heuristic:
  - WAD header 12 B; dir entry 16 B = `pos u32, size u32, name[8]` —
    **names are the full up-to-8 chars** (e.g. `BEHAVIOR`, `LINEDEFS`,
    `MAP45`, `MAPINFO`); earlier "4-char truncated" notes were wrong.
    Magic `IWAD`/`WAD\x1a`/`PWAD`. Maps = marker lump (full `MAP\d+` or
    `E#M#`) + generic lumps, ending at `ENDMAP` if present.
  - Linedef layout is chosen **per map block exactly like the engine**
    (`p_openmap.cpp` `P_OpenMapData`): a lump whose name starts with
    `BEHA` (the check is `strnicmp(name,"BEHA",4)`; the actual name is
    `BEHAVIOR`) in the block → `maplinedef2_t` 16 B (special `u8@6`,
    sides @12/14), else `maplinedef_t` 14 B (special `u16@6`, sides
    @10/12). Validated 100% clean (0 bad vertex/side refs) over all 176
    blocks of the six WADs on hand (DOOM2 32×14B, HEXEN 31×16B, HERETIC
    48×14B, STRIFE 34×14B, Pirates! 19×16B, antarc 12×UDMF).
  - **UDMF**: `TEXTMAP` in the block → UDMF inventory (object counts, line
    specials, sector-special bits, floor/ceiling plane equations,
    `portal_*` sector props, `smoothlighting`, `nofakecontrast`).
    Namespace matters: zdoom/hexen/mbf/boom/(default) = modern number
    space; classic = xlat numbers. `MAP03` appears twice in antarc —
    flagged; the engine loads the first occurrence (`CheckNumForName` is
    a first-match lookup).
  - MAPINFO: first `MAPINFO` else `ZMAPINFO` lump; new syntax
    (`map MAP41 "title"`, `defaultmap {}`) and old syntax (`map <N> {}`,
    N = position in the WAD's map list) both parse; `defaultmap` keywords
    apply to every map.
  - Map names are the full marker names (no reconstruction),
    cross-checked against the MAPINFO section names when present.
  - **Feature signals are line specials + MAPINFO keywords only.** UZDoom
    has no `FF*` (3D-floor) or `POLYOBJ` lump readers anywhere in `src/`
    (verified by grep). Special numbers are format-dependent (see below):
    BEHA maps — 3D floors 160/50, polyobjects 1–9/59/86–93/283, portals
    156/301/57, sector_link 107/51 (`playsim/actionspecials.h`); DOOM
    format — 3D floors 281/289/300–306/332/400–417 (`xlat/base.txt`).
- **How line specials are interpreted** (verified in source 2026-10-04;
  `MapLoader::LoadLevel` maploader.cpp:2928+, `FLevelLocals::TranslateLineDef`
  gamedata/p_xlat.cpp, `wadsrc/static/xlat/*`, FARG(xlat) d_main.cpp:248):
  - BEHA/BEHAVIOR lump in the map block → MAPTYPE_HEXEN, **no translator**:
    raw special numbers are the ZDoom/Hexen numbers.
  - No BEHA → MAPTYPE_DOOM, raw specials are **classic numbers run through
    the game's xlat translator** (doom.txt: Doom/Chex/Urban Brawl/Harmony;
    heretic.txt: Heretic — Hexen needs none since it's BEHA; strife.txt:
    Strife). Unmapped specials are **zeroed, never passed through**.
    So the same raw number means different things per format (301 =
    Line_QuickPortal in BEHA but Sector_Set3DFloor in DOOM format; Heretic
    107 = Stairs_BuildUpDoom, not Line_SetPortalTarget — the “portals” in
    Heretic/Hexen gameplay are classic teleport-style lines, not see-through
    portals). STRIFE is NOT a distinct format: no BEHA in STRIFE1.WAD → it
    loads as DOOM format with the strife.txt translator.
  - Consequence: line portals, sector_link and polyobjects can exist **only
    in BEHA maps**; 3D floors in both formats; skybox/fog/fake-contrast
    require MAPINFO, which no as-distributed classic WAD has. The scanner
    reports n/a for feature columns it cannot interpret per format.
- **Feature status of the WADs on hand** (scan v3, 2026-10-04):
  - HEXEN (31 maps, all BEHA): MAPI sky1/sky2(/3) keywords only — classic
    sky *texture designators*, not a 3D skybox. No line portals, no
    sector_link, no 3D floors, no polyobjects, no fog, no fakecontrast.
  - Pirates! (19 maps, all BEHA, ZMAPINFO): 3D floors (160/50) in all 19
    maps (605 lines), polyobjects in many, `fogdensity` in MAP50/51/57,
    `sky1` in all maps. No line portals, no sector_link, no fakecontrast.
  - DOOM2 (32), HERETIC (48 = 27 + 21 dev), STRIFE (34 = 22 + 12 dev):
    all DOOM-format (no BEHA, no MAPI) → cannot carry line portals,
    sector_link, polyobjects, skybox, fog or fake-contrast; also zero
    3D-floor hits via the base.txt numbers. The earlier scan-v2
    “findings” (STRIFE “2 line portals”, HERETIC E3M8/DOOM2 MAP31
    “sector_link”, STRIFE 3D-floors/polyobjects) were misreads of classic
    specials and are **retracted**.
  - antarc (11 live UDMF maps, `namespace = "zdoom"` → modern number
    space, + 1 trailing duplicate MAP03 block = dead data): zero pinned
    features — no line portals, no sector_link, no 3D floors, no
    polyobjects, no plane equations, no `nofakecontrast`; MAPINFO only
    has classic `sky1`/`sky2` designators. Usable as a plain UDMF smoke
    map, not as a feature WAD.
- **Still missing for the locked pins: line portals (≥2), sector_link, a 3D
  skybox, and `forcefakecontrast`** — absent from every WAD on hand and
  unreachable in classic as-distributed WADs by construction. They must
  come from user-supplied **ZDoom-format (BEHA + MAPINFO)** WADs (decision
  2026-10-04 held; see the category-lock consequence above). When they
  arrive, run `scan_maps.py` first; it flags blocks that fail the BEHA
  rule (size non-divisible / bad refs) and duplicate map names.
- DOOM2 verified: 32 contiguous maps (MAP01–32).

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
