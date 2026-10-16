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
