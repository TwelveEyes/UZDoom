# 10: Shading resolve (light chain)

**Map:** `../map.md`: Frame model (Shading resolve).
**Type:** task
**Status:** ready-for-agent
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

- [ ] Full-lighting A/B (fullbright override off) on the DOOM2 static set + demo, DOOM2 MAP23
      (light thinkers), MAP27 (fog), a fake-contrast map, and a glow map. Only DC1 / DC2.
- [ ] Cvar sweep: `r_lightmode` 0/1/2/8, `gl_fogmode` 0/1/2, `r_fakecontrast` 0/1/2,
      `r_extralight ±`, `r_distance_cull_type` 0/1/2, `gl_weaponlight` 0/1. Each passes A/B.
- [ ] Resolve cost logged per view (GPU timestamp) on the stress vantages.

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
