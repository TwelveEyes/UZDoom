# 11: Runtime texture state + material buckets

**Map:** `../map.md`: Data model (State buffers: side state, plane xform, translation table), Shaders.
**Type:** task
**Status:** claimed
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
- 2026-10-15 (from 10): the shading resolve exists (`resolve.comp`, `LevelMeshResolved`).
  - It covers light, fog, desaturation and glow. Side / plane special colors, additive colors,
    gradients and texture manipulation are still deferred at bake (`HasWallColorEffects` and
    the plane checks in `hw_levelmeshbake.cpp`); moving them into the resolve means
    `uObjectColor` / `uObjectColor2` / `uAddColor` / `uTextureAddColor` / ... as more flat
    varyings, and the gradient planes in the vertex stage (locations 10-15 are used; the
    resolved record has 4 reserved words).
  - Side lights (`Light`, `TierLights`, the light flags as `LMSD_*`) are baked into the surface
    record. A side state stream that tracks them would let the resolve follow scripted side
    light changes.
  - `LMSF_TEXFULLBRIGHT` (fullbright flat) is baked from the load-time frame. The classic tests
    the current animation frame; with an animated translation table, test the table entry
    instead (a per-texture fullbright bit).
  - Texture glow is resolved per sector on the CPU (`sector_t::GetWallGlow` in
    `LevelMesh_FillSectorState`); sectors whose plane texture glows without auto-glow are
    candidates every frame (`LevelMeshStateStream::AnimatedGlow`, built at load). A plane
    texture change must refresh that list and mark the sector changed.
- 2026-10-15: progress, part A committed (7a73f37303): animation table, side / plane-xform
  record streams, wall UV deltas (details go into the resolution).
  - Demo A/B (full lighting, fail ticks before -> after): movers MAP30 64 -> 19, DOOM2 MAP01
    20 -> 3, MAP23 9 -> 3, MAP27 8 -> 2, Hexen MAP10 18 -> 12, Heretic E1M2 9 -> 3, SOS MAP32
    49 -> 7, planisf MAP01 44 -> 15, Pirates MAP43 12 -> 8, SOS MAP46 55 -> 16 (HEAD binary
    from a worktree: every remaining fail also fails there). MAP30 static 30/30 pass.
  - **Finding for triage (not 11):** large regions the classic renders black (nothing drawn)
    where the levelmesh draws geometry: movers MAP30 ticks 300/305/360/365 (stairs room),
    SOS MAP46 210/660/2280 (vertical strips), Hexen MAP01 270/300, SOS MAP32 120/2040,
    Hexen MAP10 30/1770. Independent of the state stream (`lm_stateupdate 0` changes
    nothing). Remaining: Pirates MAP43 120/630 upper walls against the sky (14), MAP30 tour
    spectre fuzz, sub-min specks.
- 2026-10-15: part B committed (e1be6f0642): texture swaps through the streamed records,
  runtime classic fallback (LMTS_DEFERRED), switch preload, `make_texturetest.py` map + demo.
- 2026-10-15: part C (material buckets): buckets are alpha class x material program
  (default, warp1, warp2: levelmesh variants of the classic material shaders, per-texture
  shader speed in the texture table, timer push constant); specular / PBR / paletted / user
  shaders / canvases stay classic. Runtime fallback also for reflective / portal planes and
  side / plane colors set at runtime (hooks in the color setters, Sector_SetPlaneReflection).
  Capture fix: `ShaderStartTime` is restamped on the capture clock (the warp timer base
  depended on load time). Pirates MAP49 with levelmesh warps 63 -> 3 fails (= with warps on
  the classic path), MAP43 48 -> 8 (reflective water was drawn opaque), every other set
  unchanged, determinism 87/87, vk_debug survey only the known VUIDs.
