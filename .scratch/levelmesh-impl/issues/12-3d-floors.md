# 12: 3D floors

**Map:** `../map.md`: Data model, Shading resolve.
**Type:** task
**Status:** claimed
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

- [ ] DOOM2 MAP07 / MAP21 / MAP30 (3D floors + lift) and Pirates! MAP43 / MAP58: A/B passes, only
      DC1 / DC2.
- [ ] Band draw count and GPU time logged on the Pirates! and myhouse MAP01 stress vantages.
- [ ] 3D-light refresh cost is O(changed). The old O(n²) dedup is gone.

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
