# 10: Shading resolve (light chain)

**Map:** `../map.md`: Frame model (Shading resolve).
**Type:** task
**Status:** resolved
**Blocked by:** 09

## What to build

A compute pass, run per view after each cull phase, over the visible surfaces only. The visible
set comes from the two-phase Hi-Z cull (ticket 05 decision: Hi-Z is kept as the visibility
source). It writes one
compact resolved shading record per surface:

- effective light, `RescaleLightLevel`, rel light (fake contrast: baked rel pair; smooth / classic
  selection; `r_fakecontrast`; `LEVEL3_FORCEFAKECONTRAST`), `CalcRelLight`, weapon / extra light,
  `CalcLightLevel` (all `ELightMode` branches), `CalcLightColor`;
- desaturation;
- fog density and color (`gl_fogmode`, outsidefog, fade tables, `r_distance_cull_type` / cullcolor,
  inkybox);
- the softlight parameters (lightfactor / lightdist);
- glow: sector glow plus texture glow fallback through a per-texture glow table refreshed for
  animated frames;
- fullbright and special colormap.

**Sources of truth.** C++ functions (`hw_lighting.cpp`, `hw_setcolor.cpp`, `p_sectors.cpp`
`GetLightLevel`, `hw_walls.cpp`) stay authoritative. The GLSL port cites them line by line. Per-view
and per-level inputs are passed as a uniform block.

The vertex stage reads the record through a `flat` surface index and forwards it as `flat`
varyings. Fragment code consumes it through the shared include. No per-vertex light math.

Remove the interim lighting from ticket 08. The fullbright override stays as a debug tool only.

## Acceptance

- [x] Full-lighting A/B (fullbright override off) on the DOOM2 static set + demo, DOOM2 MAP23
      (light thinkers), MAP27 (fog), a fake-contrast map, and a glow map. Only DC1 / DC2.
- [x] Cvar sweep: `r_lightmode` 0/1/2/8, `gl_fogmode` 0/1/2, `r_fakecontrast` 0/1/2,
      `r_extralight ±`, `r_distance_cull_type` 0/1/2, `gl_weaponlight` 0/1. Each passes A/B.
- [x] Resolve cost logged per view (GPU timestamp) on the stress vantages.

## Comments

- 2026-10-14 (from 08): the interim shading is the classic fullbright wall state, set in
  `HWDrawInfo::DrawLevelMesh` before the draw. The fragment stage is the shared `main.fp` (via
  `lm_frag.glsl`); per-surface data reaches it as the flat varying `vLMMaterial` (slot, layer
  count, sampler, texture-mode flags).
  - The bake defers to classic any side or plane with special, additive or texture-manipulation
    colours (`HasWallColorEffects`, plus the plane checks), along with heightsecs and light
    bands. These are decided at bake time. Moving them into the resolve, and dropping the
    deferral, belongs here or in 11.
  - Use the `WALLF_EXTCOLOR` hint carefully: the map loader sets it on every side
    (`sector_t::CheckExColorFlag`).
- 2026-10-14 (from 09): the sector record streams dirty-only.
  - `LevelMesh_FillSectorState` (`hw_levelmeshstate.cpp`) is the single packer. The stream
    memcmps whole records, so new light ingredients only need to be packed there; growing
    `LevelMeshSectorState` needs no other change (`scatter.comp` copies `LM_SECTOR_WORDS`
    words per record).
  - Already packed (20 words): planes, texZ, `lightlevel`, floor / ceiling light, the
    colormap (`lightColor`, `fadeColor`, and `colormap` = desaturation | blend factor << 8 |
    fog density << 16), and per-plane `glowColor` / `glowHeight`. No sector flags yet.
  - Light thinkers (`STAT_LIGHT`) are candidates every frame, so MAP23's lights reach the GPU
    on the next frame.
  - The light / colormap / glow setters (`SetLightLevel`, `ChangeLightLevel`, `SetGlowColor`,
    `SetGlowHeight`, `::SetColor`, `::SetFade`) call `LevelMesh_SectorChanged`, so ACS and
    the light specials arrive next frame. Direct field writes that bypass them only arrive
    through the round-robin sweep (`lm_statesweep`, default 512 sectors per frame). If a new
    ingredient has its own setter, hook it the same way.
  - `LevelMeshSurface` is 28 words now: `edgeTop` plus `reserved[3]` are free for per-surface
    shading data.
- 2026-10-15: resolved.
  - **Code.**
    - `wadsrc/static/shaders/levelmesh/resolve.comp` (new): one workgroup per cluster after
      each cull phase. A cluster the phase draws (`drawn[]`, a new cull-set binding written by
      `cull.comp`) resolves its surfaces into `LMBIND_RESOLVED` (`LevelMeshResolved`, 12
      words per surface): light color + desaturation, fog color / mode / density, soft light
      level, light factor / distance, and whether `HWWall` keeps the glow. Every function in
      it names its C++ source: `side_t::GetLightLevel`, `CalcRelLight`, the light / colormap
      selection of `HWWall::Process` / `PutWall` / `RenderTexturedWall` and `HWFlat::
      ProcessSector` / `Process` / `PutFlat` / `DrawFlat`, `SetColor`, `SetFog`,
      `SetShaderLight`, `CalcLightLevel`, `CalcLightColor`, `GetFogDensity`, and the push
      constants `VkRenderState::FillPushConstants` derives. The call order follows the
      classic: walls SetFog then SetColor (`RenderTexturedWall`), flats SetColor then SetFog
      (`DrawFlat`), which decides whether the Korshun bright-fog soft light survives.
    - `src/rendering/hwrenderer/hw_levelmeshshade.cpp` (new): `LevelMesh_FillShading` fills
      the per-view `LevelMeshShading` block (a storage buffer rather than the spec's uniform
      block: the tables are 5 KB) (light mode, level / cvar flags, fake contrast,
      fog mode, cull fog, extra light, level fog densities, outside fog) plus tables for what
      the GPU must not recompute: `RescaleLightLevel`, the dark-mode
      `RoundHalfEven(192 - (192 - l) * 1.87f)` (`CalcDarkLightLevel`, split out of
      `CalcLightLevel`), the black-fog `distfogtable` (`GetDistFogTable`) and `l / 255.f`.
      The light-mode values are static_asserted against `ELightMode`.
    - Backend: `LMBIND_SHADING` holds the block, updated with `vkCmdUpdateBuffer` in the draw
      command buffer only when it changed (memcmp). `VkLevelMeshRenderer::Resolve` runs after
      each cull (phase 1, phase 2 or single), between a vertex-read -> compute-write and a
      compute-write -> vertex-read barrier. GPU timestamps around it: perflog v5 `lmr1` /
      `lmr2`.
    - Vertex stage: `levelmesh.vp` reads `lmResolved[surface]` once per vertex (no light
      math) and forwards it as flat varyings at locations 10-14 (the generated varyings end
      at 9); `lm_frag.glsl` maps `uLightLevel`, `uFogDensity`, `uLightFactor`, `uLightDist`,
      `uFogEnabled`, `uFogColor`, `uDesaturationFactor`, `uGlowTopColor` / `uGlowBottomColor`
      onto them, so `main.fp` is unchanged. Glow distances use the front sector's streamed
      planes as `SetGlowPlanes` does. The interim light from 08 is gone; `lm_shading 0`
      (debug) draws with the render state's fullbright light instead.
    - Sector record (24 words): the raw glow fields became `sector_t::GetWallGlow`'s output
      (`glowTop` / `glowBottom`, rgb + height), so sector glow and the texture-glow fallback
      of the current animation frame are resolved on the CPU in `LevelMesh_FillSectorState`.
      This replaces the spec's per-texture GPU glow table: same result, one call per dirty
      sector. When `GetWallGlow` returns false (no `HWF_GLOW`; the floor's sector glow
      overwrites the ceiling's result) both heights are packed as 0. Sectors whose plane texture glows without auto-glow (the glow follows the
      animation frame) are candidates every frame (`LevelMeshStateStream::AnimatedGlow`).
    - Bake: `sideFlags` holds `LMSD_*` (the `GetLightLevel` inputs of the surface's tier)
      instead of raw `WALLF_*`; `LMSF_NOSECTORFOG` marks `RENDERWALL_M2SNF`;
      `LMSF_TEXFULLBRIGHT` marks fullbright flat textures (static until 11).
  - **Two classic findings.**
    - **`RescaleLightLevel` depended on compile flags.** `hw_walls.cpp` / `hw_flats.cpp`
      build with `-ffast-math`, which turned `int((l / 255.0) * 255.0)` into a reciprocal
      multiply: 24 levels (33, 37, ... 196, ... 244) came out one lower than in a strict
      translation unit. User decision (2026-10-15): the classic now computes it in integers
      (`l * (255 - e) / 255 + e`, `hw_lighting.h`), which is exact and the same with every
      compiler. Classic output changes by +1 light level on those 24 levels.
    - **3D-light-lit surfaces.** In a sector with 3D floors, `HWWall::Put3DWall` and
      `HWFlat::ProcessSector` take the light level / colormap of the 3D light covering the
      surface (light transfer), not the sector's. The fullbright A/B hid this (Pirates MAP43
      pillars were 40% too bright). The bake now defers those surfaces (`LMSF_LIGHTLIST`):
      walls whose classic light / colormap differ from the sector's (`SectorLit`), flats
      whose `P_GetPlaneLight` light is not the sector's own (with the classic's
      absolute-floor-light exception for sourceless lights). Pirates MAP43: 1087 -> 1718
      deferred surfaces of 19291. Ticket 12 takes them over.
  - **Verification** (Vulkan, clang RelWithDebInfo, full lighting, `r_dynlights 0`).
    - **Harness fix:** `abtest.py` now fails a run whose stdout reports a levelmesh fallback
      (`Levelmesh: ... failed`). A resolve shader that did not compile had made a
      classic-vs-levelmesh pair pass as 83/83 identical.
    - **New map:** `tools/abtest/make_lighttest.py` generates `wads/lighttest.wad` (10 rooms:
      colored light + desaturation, fade colors, sector fog density, sector glow, auto and
      `Texture` GLDEFS glow, a fullbright flat, light 255 + glow, side lights absolute / per
      tier / nofakecontrast / smoothlighting / lightfog, absolute and relative plane lights,
      diagonal walls). MAP02 adds level fog density, outside fog, smoothlighting,
      forcefakecontrast; MAP03 a fade table. `ab_manifest_lighttest.json` replays
      `demos/lighttest/` (scripted tours, 83 captures each, with pistol shots so weapon
      flashes reach captures 30 and 185: `make_demo.py` step `attack@T`).
    - **Lighttest A/B:** MAP01 / MAP02 / MAP03 have 31 / 31 / 30 failing ticks, the same
      ticks fail with `--fullbright`, and the full-lighting diff (about 64k px per run, mostly
      deltas 1-8) equals the fullbright one. Every fail is a one-pixel-tall horizontal line at
      a tier or ceiling edge, or texel speckle on the high-contrast CEIL1_2 flat: geometry /
      sampling (DC2), not shading. Before the integer `RescaleLightLevel` the lit runs showed
      about 1M extra differing px (the off-by-one levels).
    - **Cvar sweep** (both paths, all three lighttest maps): `gl_lightmode` 0 / 1 / 2,
      `gl_maplightmode` 3 / 4, `gl_fogmode` 0 / 1 / 2, `r_fakecontrast` 0 / 2,
      `r_extralight` +24 / -24, `r_distance_cull_type` 1 / 2, `gl_weaponlight` 0; after the
      review also `gl_maplightmode` 0 (LinearStandard) / 1 (DoomBright) and `gl_weaponlight`
      0 vs 8 with the weapon flashes. Every combination stays at the geometry baseline
      (62-70k differing px, no new failing ticks; local `tools/abtest/logs/lm10_sweep.txt`,
      gitignored). The pass rule is "no tick fails that fullbright does not fail, and the
      differing-pixel count stays at the fullbright level"; the remaining fails are
      unallowlisted DC2 edges. Build mode (lightmode 5) is not reachable (MAPINFO accepts
      0-4, `gl_maplightmode` -1..4); its fog-off light parameters are stale in the classic and
      default to (1, 0) here. A special colormap (invulnerability) is the fullbright-scene
      path (`LMSH_FULLBRIGHT`) and was not run.
    - **Tour demos, full lighting vs fullbright** (failing tick sets): DOOM2 MAP01 / MAP07 /
      MAP23 / MAP27, Hexen MAP01 / MAP10, Heretic E1M2, SOS MAP32, Pirates MAP43, planisf
      MAP01: no tick fails only with full lighting (Pirates MAP43 after the 3D-light deferral;
      planisf MAP01 swaps one tick, 1320 lit vs 1380 fullbright, both a one-pixel-wide edge
      speck of 4-8 px). The remaining fails are the
      known classes (animated textures 11, sky 14, OOB 18). DOOM2 MAP23 (light thinkers) and
      MAP27 fail fewer ticks lit than fullbright.
    - **Movers (09):** classic vs levelmesh, full lighting: MAP01 / MAP07 pass, MAP30 fails
      on its animated textures (11). Culling holes (cull 0 vs Hi-Z, budget 16): 80/80
      identical on all three.
    - **Static:** DOOM2 MAP07 30/30 identical, MAP01 / Pirates MAP43 30/30 pass.
    - **Validation:** `lm_bake_survey.py --vkdebug`: only the known VUIDs.
  - **Measurements** (perflog v5, vantage set, tics >= 350, `logs/lm10_*`):
    - Resolve GPU time p50: phase 1 0.008-0.017 ms, phase 2 0.002-0.004 ms (myhouse MAP01
      0.015-0.017 / 0.004). p95 within 0.001 ms of p50. Levelmesh total GPU p50 0.14-0.24 ms.
    - Memory: 48 bytes per surface for the resolved records (myhouse MAP01 device total
      166.7 -> 187.1 MB).
    - CPU: `LevelMesh_FillShading` is a 5 KB block per view (tables included); the upload
      happens only when it changes.
  - **Code review follow-ups** (standards + spec review): glow result semantics; the floor
    absolute-light exception above; a write-after-read barrier on `drawn[]` across draws (the
    next draw's first barrier now includes compute reads); the shading block is re-uploaded
    when the `LMBIND_SHADING` buffer changes, not only per level; the unused `LMSD_NOSIDE`
    went away; stale comments. Rechecked: lighttest MAP01-03 (unchanged), Pirates MAP43
    (12 fails, none lit-only), DOOM2 MAP23 / Hexen MAP01 (unchanged), `--vkdebug` survey.
  - **Known limits:**
    - `SectorLit` and the flat colormap test compare values at bake, not light sources: a
      3D light that equals its sector at load is treated as the sector's (12).
    - Side lights (`side_t::Light`, `TierLights`, light flags) are baked; runtime changes
      (ACS `SetLineLight`-style scripts) need the side state stream (11).
    - Side / plane special colors, additive colors, texture manipulation, non-default
      material shaders: still deferred to classic at bake (11).
    - Fullbright flat textures and texture glow follow the baked texture; plane texture
      changes and animation are 11.
    - 3D-light-lit surfaces are deferred (12). A wall whose 3D light equals its sector's
      light at bake stays on the levelmesh and follows the sector, not the 3D light.
    - Dynamic lights stay off in the A/B (19).
