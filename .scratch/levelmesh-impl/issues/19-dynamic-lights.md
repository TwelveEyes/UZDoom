# 19: Dynamic lights (clustered forward+), shadowmaps, SSAO normals

**Map:** `../map.md`: Acceptance policy DC4.
**Type:** task
**Status:** wontfix
**Blocked by:** 10

## What to build

The classic path builds a per-surface light list (`HWWall::SetupLights`, flats likewise) and sets
`uLightIndex` per draw. That cannot work with batched indirect draws. Instead:

- **Gather.** Per frame, collect the visible `FDynamicLight`s (point / spot; additive, subtractive,
  attenuated; shadowmap index) in the units `main.fp` expects.
- **Binning.** A compute pass bins lights into view-space clusters (froxels: tile grid × log depth
  slices), per view (portal views included), into an SSBO light-index list.
- **Fragment side.** The levelmesh fragment path looks up its froxel and iterates that froxel's
  lights. The per-light math (`lightContribution`, shadowmap sampling, material variants) is shared
  with the classic code through the common include. Sprites, models, and decals keep classic lists.
- **Selection parity.** The classic culls per surface: radius, plane side, portal group. Match the
  plane-side and portal-group rules in the fragment test where cheap; classify the rest under DC4.
- **Shadowmaps.** The existing `hw_shadowmap` (LevelAABBTree) is unchanged. Bind it in the
  levelmesh set.
- **SSAO normals.** Write correct normals to the G-buffer pass (`gl_ssao`) from the per-surface
  normals.
- **Re-enable lights in A/B.** The `r_dynlights 0` pin in `tools/abtest.py` is removed for levelmesh
  maps once this lands.

## Acceptance

- [ ] DOOM2 MAP01 with `lights.pk3`, plus a dynlight-heavy map: A/B passes, only DC1 / DC2 / DC4.
- [ ] Light count scaling (GPU time vs visible lights) measured on a stress vantage.
- [ ] SSAO on: A/B passes.

## Comments
- 2026-10-15 (from 10): the levelmesh fragment stage takes its light, fog and glow from flat
  varyings (`lm_frag.glsl` maps the `u*` names); `uLightIndex` stays -1 in
  `VkLevelMeshRenderer::DrawPhase`. Vertex output locations 10-15 are taken (generated
  varyings end at 9); a froxel lookup needs no new varyings.
- 2026-10-15 (from 11): specular / PBR materials (normal + specular / metallic / roughness / AO
  layers) still go to the classic path (`LevelMesh_MaterialProgram` returns -1). Add them as
  levelmesh material programs here: they only differ from the default with dynamic lights.
  The program index selects `defaultshaders[]` (`VkShaderManager::GetLevelMesh`); bucket =
  program * 2 + masked (`LM_Bucket`), so new programs extend `ELevelMeshProgram` and
  `LMB_DRAWN`.
- 2026-10-20 (wontfix): superseded by .scratch/helion-render (ADR 0004).
