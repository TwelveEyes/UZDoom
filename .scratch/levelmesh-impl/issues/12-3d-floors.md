# 12: 3D floors

**Map:** `../map.md`: Data model, Shading resolve.
**Type:** task
**Status:** resolved
**Blocked by:** 10

## What to build

- **3D-floor flats.** Baked in ticket 06 from the classic flat builder and anchored to the model
  sector's planes; they move with the model through the sector state. Cull mode is double-sided
  where the classic draws both sides. Translucent and FF_FOG 3D planes go to ticket 16's
  translucent list.
- **3D-floor sides.** Walls emitted by the classic 3D-floor wall code (`HWWall::BuildFFBlock` and
  related, captured in the bake).
- **Light bands.** In the classic, walls inside 3D-floor light volumes are split per light layer
  (`hw_walls.cpp` split planes and `lightlist`). In the levelmesh:
  - the bake records band ranges per wall (split planes as plane refs into model sectors, plus a
    3D-light ref);
  - the cull emits one draw command per (wall cluster range, band) with the band index;
  - the vertex stage writes the split-plane clip distances;
  - the resolve pass shades per band.

  Choose band granularity so the band count does not multiply cluster draws; measure it on
  Pirates! and myhouse MAP01.
- **3D-light state buffer.** Built once at load: lightlist entries deduped by `F3DFloor` through a
  hash map, and the index is stable. Per frame, only `lightlevel` / colormap are refreshed, and
  dirty-only.

## Acceptance

- [x] DOOM2 MAP07 / MAP21 / MAP30 (3D floors + lift) and Pirates! MAP43 / MAP58: A/B passes, only
      DC1 / DC2. *With a carve-out:* everything ticket 12 changed adds only DC2 edge specks;
      the fails left are the same with every 3D surface on the classic path (`lm_3dfloors 0`)
      and are filed elsewhere (see Answer).
- [x] Band draw count and GPU time logged on the Pirates! and myhouse MAP01 stress vantages.
- [x] 3D-light refresh cost is O(changed). The old O(n²) dedup is gone.

## Comments

- 2026-10-13 (from 06): 3D-floor sides are baked as `RENDERWALL_FFBLOCK` surfaces with
  `LMSF_3DFLOOR`. Their `vRef` is a heuristic: the top-left vertex's anchor, because HWWall does
  not keep the rover. It fired 39k times on myhouse MAP01; verify it or pass the rover through
  HWMeshHelper.
  - Banded walls are flagged `LMSF_LIGHTLIST` with `lightList = -1`. The classic draws them
    once per light layer with split planes.
  - 3D-floor flats are baked double-sided on the target sector's triangulation. Translucent ones
    are in `TranslucentFlats`.
- 2026-10-14 (from 09): a moving 3D floor's control sector already streams its planes (the
  anchor planes are the model sector's), and its clusters get their z refreshed.
  - `RENDERWALL_FFBLOCK` sides have no `edgeTop` (`LevelMeshSurface`): a 3D-floor side clipped
    by a plane that later crosses it is not collapsed.
  - The classic clipping there is a min / max over the rover and the sector planes. Model it
    when 3D floors move into the mesh.
- 2026-10-15 (from 10): surfaces lit by a 3D light are deferred to classic (`LMSF_LIGHTLIST`):
  - walls whose classic light level or colormap differs from the sector's after
    `HWWall::Put3DWall` (`SectorLit` in the bake), on top of the banded walls (`w.lightlist`);
  - flats in sectors with 3D floors whose `P_GetPlaneLight` light is not the sector's own
    (or whose colormap differs), as `HWFlat::ProcessSector` does.
  - Pirates MAP43: 1087 -> 1718 deferred surfaces. The fullbright A/B had hidden it; with full
    lighting the pillars there were 40% too bright.
  - A wall whose 3D light equals the sector's light at bake stays on the levelmesh and follows
    the sector. The 3D-light state buffer here should replace both rules with a per-surface
    3D-light ref, resolved in `resolve.comp` (`SetColor` / `SetFog` with the 3D light's level
    and `CopyFrom3DLight` colormap).
- 2026-10-16: progress, part B (3D-light state buffer).
  - Test map: `tools/abtest/make_3dfloortest.py` writes `wads/3dfloortest.wad` and its demo
    (`ab_manifest_3dfloortest.json`, 14 rooms). With everything 3D on the classic path, 24 of
    107 ticks fail on DC2 ceiling speckle only; compare runs with `ab_sheet.py failsets`.
  - `LevelMeshLight3D` records (8 words, `LMBIND_LIGHTS3D`), one per distinct lightlist entry,
    keyed at bake by (sector, lightsource model, lightsource FADEWALLS / FOG, p_lightlevel,
    caster FADEWALLS): the clipped FF_DYNAMIC copies are recreated by every
    P_Recalculate3DFloors, so no F3DFloor pointers. Hash map at bake, stable index.
    `LevelMesh_FillLight3D` finds the current entry by the key and copies its extra_colormap.
  - Every wall / flat of a sector with 3D floors carries `lightList` (the layer SplitWall /
    P_GetPlaneLight gives it) and `LMSF_LIGHT3DLEVEL` where the classic replaces the light
    level; `resolve.comp` applies `CopyFrom3DLight`. `SectorLit` and the LMSF_LIGHTLIST
    deferral of non-banded surfaces are gone; banded walls (`w.lightlist`) stay deferred
    (part D).
  - Refresh is O(changed): the lights reading a changed sector (`LightDepend` CSR) plus a
    sweep. A sector whose 3D-floor layout (its own planes and its 3D floors' model planes,
    `LayoutPlanes`) moved from the bake sets `LML3_DEFERRED` on its lights: the vertex stage
    hides their surfaces and the sector's tiers / planes go back to the classic draw lists
    until the layout is at the bake again (`lm_statelog` prints the switches). `lm_light3d 0`
    (level setup) defers every 3D-lit surface, for bisecting.
  - **Lift fix (found here, not 3D-specific):** HWWall::Process clamps an upper tier's
    bottom to the front floor where that lies above the back ceiling (and a lower tier's top
    to the front ceiling). The bake anchored those vertices to the plane that won at load, so
    a lift starting above its neighbour's ceiling dragged its upper tiers down with it into a
    wall sheet across the opening (Pirates MAP58 ticks 210-270, the lift ride flicker). Now
    anchored to the back plane with `LevelMeshSurface::edgeClamp` (max / min in the vertex
    stage), taken only when it reproduces every edge vertex bit-exactly at load.
  - A/B (full lighting): 3dfloortest unchanged fail set (speckle only); Pirates MAP58 26 -> 23
    fails (differing px 8.9M -> 1.3M); MAP43 8 -> 9 (tick 900: a 2x2 speck on a flat now drawn
    by the levelmesh, DC2); DOOM2 MAP01 3, movers unchanged (MAP30 19).
- 2026-10-16: progress, part C (3D-floor sides and planes on the levelmesh).
  - **3D-floor groups:** a side's 3D-floor sides, and a sector's opaque 3D-floor planes, are
    drawn by the levelmesh or the classic path together (like a side tier). Their surfaces
    have `sideRef = numsides * 3 + group`; the group's record is streamed as a side record
    after the side tiers (`LevelMesh_FillFFGroup`, `LevelMeshStateStream::FFGroups`) and is
    `LMTS_DEFERRED` while one of its watches (`LevelMeshFFWatch`) differs from the bake:
    the front / back sectors' planes and their 3D floors' model planes, the model plane
    textures (planes), the side's tiers and the 3D floors' master side mid tiers (sides:
    texture, offsets, scales; colors). Candidates: groups watching a moved sector, a changed
    plane texture or side tier, and a sweep. `HWDrawList::RemoveLevelMeshOwned` drops
    `RENDERWALL_FFBLOCK` walls and `SSRF_RENDER3DPLANES` flats of owned groups.
    So a moving 3D floor is drawn classic while away from its bake position (as the 3D
    lights already were); `edgeTop` for FFBLOCK sides and the vRef heuristic only matter at
    the bake position.
  - **Planes:** baked once per side `HWFlat::ProcessSector` draws them from (its range picks
    at the sector's center, `FF_INVERTPLANES` / `FF_BOTHPLANES`), one-sided with the floor /
    ceiling winding: the levelmesh culls back faces, and `LMSF_TWOSIDED` drew planes from
    sides the classic does not (inside a 3D floor). Lit by `P_GetPlaneLight` at the plane
    (from below for the bottom), always its level, keeping the sector's fade color
    (`resolve.comp`). Deferred at bake: sectors whose picks differ in a fullbright scene (fog
    3D floors skipped), `FF_FIX` / `FF_FADEWALLS` (colormap carried between 3D planes).
  - **Sides:** light from the upper tier's side light (`HWWall::Process`), texture id the
    untranslated side upper / lower or master mid that matches (animation follows).
  - FFBLOCK sides no longer share `sideRef` with their side's mid tier, which had deferred
    every mid texture of a side with 3D-floor sides.
  - Pirates MAP43: 660 -> 0 deferred surfaces, MAP58 -> 0 (391 / 1298 groups, none deferred at
    bake). A/B: MAP43 9 -> 8 fails, MAP58 23 (same set), DOOM2 MAP01 3, movers unchanged.
    3dfloortest 24 -> 44: the 20 new fails are 1-row edge specks where a 3D plane cuts a
    wall (4-8 px, delta 3-17, DC2); `lm_3dfloors 1` (sides only) adds none. Room 5's moving
    3D floor switches its groups to classic and back (`lm_statelog`).
- 2026-10-16: progress, part D (light bands).
  - **Design change from the ticket text:** no per-band draw commands or clip distances.
    While the levelmesh draws a sector its 3D-floor layout is at the bake (a moved layout
    defers its lights and groups), so the split planes are fixed: the bake cuts every
    banded wall (`HWWall::lightlist`) into one surface per light layer
    (`Baker::BakeBands`), the part between the layer's plane and the next one's (the front
    floor for the last), as `HWWall::RenderTexturedWall`'s loop clips it. Cut vertices lie
    on the layer plane, anchored to the sector / model plane it is a copy of (offset 0,
    `LMV_SIDE`); u, v, lightmap uv interpolated. Each band takes the layer's 3D light, with
    its light level where the layer has a caster. Bands are ordinary surfaces in the wall's
    cluster: no extra draws, no extra pipeline state; `LMSF_LIGHTLIST` now only marks a
    banded wall whose layer plane matches no sector plane (left classic, counted).
  - `lm_3dfloors` bit 4 (bands) for bisecting; default 7.
  - 3dfloortest: both banded walls (room 3) cut into 4 bands, fail set unchanged (44); new
    differing pixels are isolated single pixels along the sloped cut (clip plane vs edge,
    DC2, below the 4 px cluster minimum).
  - myhouse MAP01: 173 banded walls -> 308 band surfaces, none left classic. Its demo A/B
    fails 11 ticks (up to 1.37M px) identically with `lm_3dfloors 0`: not 3D-floor related
    (see 24). Pirates MAP43 / 49 / 54 / 58 have no banded walls.
  - **Band cost** (`run_matrix.sh --demos demos/vantage --tics 1050`, tics 105-1050, logs
    `tools/abtest/logs/lm12x{0,3,7}_*`; `lm_3dfloors` 0 = all 3D classic, 3 = sides +
    planes, 7 = + bands). Band draw count: 0 extra (bands share the cluster draws).

    | vantage | 3D on levelmesh | wall p50 / p95 | lmgpu p50 / p95 | lmtri1 p50 |
    |---|---|---|---|---|
    | MYHOUSE_MAP01_V1 | none | 7.48 / 10.71 | 0.239 / 0.248 | 54220 |
    | MYHOUSE_MAP01_V1 | sides + planes | 7.48 / 10.85 | 0.239 / 0.248 | 56558 |
    | MYHOUSE_MAP01_V1 | + bands | 7.86 / 11.76 | 0.243 / 0.251 | 56558 |
    | MYHOUSE_MAP01_V2 | none | 6.94 / 10.13 | 0.242 / 0.251 | 69750 |
    | MYHOUSE_MAP01_V2 | sides + planes | 8.28 / 12.73 | 0.241 / 0.250 | 70298 |
    | MYHOUSE_MAP01_V2 | + bands | 7.69 / 11.16 | 0.243 / 0.250 | 70298 |
    | PIRATES_MAP49_V1 | none | 3.53 / 4.40 | 0.144 / 0.148 | 19204 |
    | PIRATES_MAP49_V1 | + bands | 3.63 / 4.31 | 0.143 / 0.147 | 19694 |
    | PIRATES_MAP54_V1 | none | 6.26 / 7.12 | 0.203 / 0.205 | 25447 |
    | PIRATES_MAP54_V1 | + bands | 6.44 / 8.04 | 0.207 / 0.209 | 30850 |

    Levelmesh GPU time is flat within 0.004 ms; `wall` is CPU-bound by the classic walk,
    which still runs (ticket 20), and moves by up to 1.3 ms between runs of the same view.

## Answer

Resolved 2026-10-16 (commits 11709ceb6c, b6c04fde8e, 58a714c17c; test map 959adeac02).

- **3D-light state buffer (B):** `LevelMeshLight3D` records, one per distinct lightlist entry
  keyed by value (not `F3DFloor` pointers, which `P_Recalculate3DFloors` recreates), hash map
  at bake, stable index. Refresh is O(changed) (`LightDepend` CSR + sweep). Every surface of a
  sector with 3D floors carries its layer (`lightList`, `LMSF_LIGHT3DLEVEL`); `resolve.comp`
  applies it.
- **3D-floor sides and planes (C):** drawn through 3D-floor groups (per side / per sector),
  streamed as side records after the tiers and deferred to the classic draw lists while a
  watched plane, plane texture or side tier differs from the bake. Planes are baked
  one-sided per side `HWFlat::ProcessSector` draws them from.
- **Light bands (D):** banded walls cut into one surface per light layer at bake, cut
  vertices anchored to the layer's plane. Deviation from "What to build": no per-band draw
  commands / clip distances, since the 3D-floor layout is at the bake whenever the levelmesh
  draws the sector. Band draws: 0 extra; GPU time flat (table in the part D comment).
- **Not 3D-specific, found here:** the upper / lower tier edge clamp (`edgeClamp`) fixed the
  Pirates MAP58 lift-ride flicker.
- **Acceptance A/B** (demo, Vulkan, full lighting; fails before ticket 12 -> after):
  - 3dfloortest (new): 24 -> 44. The 20 new ones are 1-row specks where a 3D plane cuts a
    wall (DC2). With sides only (`lm_3dfloors 1`) there are no new fails.
  - DOOM2 MAP07: 0 -> 0. MAP21: 7 -> 7, same ticks. MAP30: 10 -> 10, same ticks (no 3D
    floors; sparse top-of-view specks). Movers MAP30: 19 -> 19 (23).
  - Pirates MAP43: 8 -> 8 (ticks 120 / 630: upper walls against the sky, 14; the rest are
    under 60 px). MAP58: 26 -> 23. The remaining 23 are far islands / ships the levelmesh
    draws where the classic shows sky, identical with `lm_3dfloors 0` (added to 23).
  - myhouse MAP01: 11 fails, identical with `lm_3dfloors 0`: sky / portal regions (new 24).
- **Known limits:** a 3D floor that comes to rest away from its bake position stays classic
  until it returns; translucent / fog 3D floors stay classic (16); `FF_FIX` /
  `FF_FADEWALLS` sectors' 3D planes stay classic.
- **Debug:** `lm_light3d 0`, `lm_3dfloors` (1 sides, 2 planes, 4 bands), `lm_statelog`
  (group / layout switches).
