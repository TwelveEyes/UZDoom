# 22: Remaining material paths: lightmaps, user shaders, canvases, colors

**Map:** `../map.md`: Data model (texture table), Shaders.
**Type:** task
**Status:** ready-for-agent
**Blocked by:** 11

## What to build

Ticket 11 left these on the classic path (`LevelMesh_MaterialProgram` returns -1, or the bake /
state stream sets `LMSF_DEFERRED` / `LMTS_DEFERRED`). Ticket 20 can only retire the BSP walk once
they draw through the levelmesh:

- **Lightmaps** (split from 11, user decision 2026-10-15). The bake already records
  `LevelMeshSurface::lightmapPage`; add the per-vertex lightmap UVs, bind the lightmap texture
  array in the levelmesh set, and feed the classic lightmap term in `main.fp`. Needs a test map:
  bake one with ZDRay (for example `lighttest.wad`) or get a lightmapped release map; run with
  `-enablelightmaps`.
- **User hardware shaders** (GLDEFS `material` / `hardwareshader`): a levelmesh program per user
  shader (the program index already selects `defaultshaders[]`; user shaders need their own
  slots and the custom-uniform / texture bindings).
- **Canvases** (camera textures, `SWCanvas`) and **paletted** materials.
- **Side / plane colors and texture manipulation** (`uObjectColor`, `uObjectColor2`, gradients,
  `uAddColor`, `uTextureAddColor`, ...): into the resolve record or the side / plane records, so
  the runtime fallback for them can go (see ticket 10's comment on 11).

Specular / PBR belong to ticket 19 (they only differ with dynamic lights).

## Acceptance

- [ ] A lightmapped map passes A/B.
- [ ] A map with a user hardware shader, a camera texture and side / plane colors passes A/B
      with none of them deferred (`lm_statelog`, survey `deferred` column).
