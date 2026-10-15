# 11: Runtime texture state + material buckets

**Map:** `../map.md`: Data model (State buffers: side state, plane xform, translation table), Shaders.
**Type:** task
**Status:** ready-for-agent
**Blocked by:** 10

## What to build

- **Frame animation.**
  - Keep a per-frame texture translation table (untranslated id → bindless slot) that follows the
    animator (`ANIMATED` / `ANIMDEFS`, warping textures' frame ids), and upload it dirty-only.
  - Surface records keep untranslated ids.
  - Note: commercial IWADs have no `ANIMDEFS`; their animation uses the `ANIMATED` defaults.
- **Texture swaps.** Switches (`p_switch.cpp`), `side->SetTexture`, floor / ceiling texture changes
  (`a_floor.cpp`, `a_plats.cpp`, `a_ceiling.cpp`), and ACS / FS `SetLineTexture` / `ChangeFloor` /
  `ChangeCeiling` all write the side-state or sector record. Bindless means no regrouping.
  - A swap that changes the **draw bucket** (opaque ↔ masked, or a different material shader)
    rewrites that surface's bucket bits. The cull writes commands per bucket from per-surface bits.
  - If a swap introduces a case the bake did not anticipate (for example a mid texture appearing
    where none was baked), document it and route the surface to a fallback (the classic draw of that
    side) until resolved.
- **Wall UV state.** Side x/y offsets and scales per tier, including scrollers (`DScroller` side
  scroll with `DWallScrollInterpolation`), `Line_SetTextureOffset`, and `Line_SetTextureScale`.
  The vertex stage applies them as deltas against the baked u/v.
- **Flat xform.** Plane offsets, scale, and rotation (`planes[].xform`), including sector scrollers
  with interpolation.
- **Material shader buckets.** Default, warp / warp2, user hardware shaders, and brightmap / glow /
  normal / specular / PBR layer variants. Each is a levelmesh vertex-stage variant of the existing
  `FShaderManager` key, composed the way the classic composes effects with material shaders. Layers
  are bindless (per-layer slots in the material's record).
- **Lightmaps.** Lightmap page + UV from the bake, with the lightmap texture array bound in the
  levelmesh set.

## Acceptance

- [ ] DOOM2 MAP30 start view (scrolling wall): no residual diff beyond DC1 / DC2.
- [ ] Scripted switch + floor-texture-change test (a demo or tick list that triggers them): A/B
      passes.
- [ ] A WARP texture map and a brightmapped map render through their material variants and pass
      A/B.
- [ ] A lightmapped map passes A/B.

## Comments

- 2026-10-13 (from 07):
  - The texture table (`LevelMeshTextureEntry {slot, numLayers}`, per untranslated id) is
    resolved once at upload, with the animation frame current at that time. Refreshing it on
    animation is this ticket's job: re-queue the ids (`mPending`) and they are re-resolved at the
    next `BeginFrame`.
  - All descriptor writes are deferred to frame start, because ZVulkan does not enable
    `descriptorBindingUpdateUnusedWhilePending`. Enabling it would be a subtree divergence;
    only consider it if the one-frame latency shows.
  - Indexed (palette) materials bind their base layer only.
  - `LMSF_MASKED` from the bake is unreliable (textures unloaded at bake time; see 07
    Comments). Bucket membership has to come from the material state here.
- 2026-10-14 (from 08): the classic-vs-levelmesh demo A/B shows this ticket's runtime state.
  - Pirates MAP43: scrolling skull pillars, with side offsets moving in x and y.
  - SOS MAP46: the animated blood texture. Static A/B passes until the animation advances, at
    tick 10.
  - Non-default material shaders (warp, specular/PBR, user shaders, canvases) are deferred to
    classic by the bake (`NeedsClassicMaterial`). The levelmesh builds only the default material
    program (`VkShaderManager::GetLevelMesh`).
  - The masked bucket now follows the classic list rule (two-sided mids, masked 3D-floor sides,
    3D-floor and portal planes), not `isMasked()`.
- 2026-10-14 (from 09):
  - **Clamp-Y is height-dependent in the classic** (`HWWall::CheckTexturePosition`).
    `lm_frag.glsl` (`LM_SAMPLER`) now drops the baked Y clamp per fragment when v leaves
    [0, 1]. Side offsets and scales feed the same rule, so streaming side state may need it
    too (CLAMPX for skybox sectors and `M2SNF`).
  - **Reuse the state stream for side state and plane xforms:**
    - `VkLevelMesh::UpdateState` plus `scatter.comp`: one staging allocation, one dispatch,
      recorded in the transfer commands.
    - `LevelMesh_UpdateState`: candidates, a CPU mirror, and a memcmp.
    - `DWallScrollInterpolation` / `DSectorScrollInterpolation` are in the interpolator list,
      like the plane interpolations `FInterpolator::ForEachMovedSector` already walks.
  - **Animated textures that block moving-sector A/B** until this ticket:
    - DOOM2 MAP30: the face wall, slime floors and green walls (`ab_manifest_movers.json`,
      MAP30 fails from tick 5).
    - DOOM2 MAP01: `FWATER` in the tour demo.
