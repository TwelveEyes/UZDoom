# 19: Dynamic lights (owner light lists), shadowmaps, SSAO normals, specular / PBR

**Map:** `../map.md`: Acceptance policy DC4.
**Type:** task
**Status:** implemented (2026-10-21); allowlist review pending (see Comments)
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

- 2026-10-21 (implementation): **diverged from froxels** (user decision 2026-10-21). Lights are
  selected by the classic's own reachability, the playsim touch lists (`side_t::dlist`,
  `FSection::dlist`), kept on the GPU as retained per-owner lists. Selection is therefore exact
  (no light leaks through walls, portal groups exact), every consumer reads one list by index, and
  there is no per-view GPU pass.
  - Data (`hw_levelmeshdata.h`): a persistent slot per `FDynamicLight` holding a
    `LevelMeshLightRecord` (20 words: `AddLightToList`'s four vec4, plus the refine radius); an owner
    table (owner = side index, or `numSides` + section index) pointing into a pool of lists
    `[portal group, slots ascending..., end]`; light info (header, 3D-floor group → side owner,
    portal displacement table); leaf → section. All are LevelMeshSet buffers, streamed dirty-only by
    `scatter.comp`. Records, owner table, pool and info are aliased into HW buffer set 1 bindings 5..8,
    so the classic pipelines read the same lists. The backend grows the slot and pool buffers keeping
    their contents (`VkLevelMesh::GrowLightBuffer`).
  - CPU (`hw_levelmeshlights.cpp`): `LinkLight` / `UnlinkLight` mark the owners they touch dirty and
    `ReleaseLight` frees the slot. `LevelMesh_UpdateLights`, called from `LevelMesh_UpdateState`,
    packs the records (memcmp against the uploaded mirror) and rebuilds the dirty lists in a
    power-of-two block pool. If the update is not uploaded, everything is sent again.
  - Consumers: levelmesh walls read their side (polyobject walls the polyobject's section; 3D-floor
    sides their group's side), flats their leaf's section, using a flat varying. Decals read their
    wall's list (`HWWall::LightOwner`). Sprites / particles / point-lit models and the HUD sprite are
    evaluated once in the vertex stage (`lmPointLight` = `GetDynSpriteLight`; output `vDynLight`).
    Per-pixel models use the section list (small) or an inline list in the frame's light buffer (big:
    `LevelMesh_InlineLightList`, the `BSPWalkCircle` set). The surface rules (DontLightMap, plane side,
    the additive clamp only when the classic list would hold an additive light) and the actor rules
    (`ShouldLightActor`, forced attenuation) are applied in `lmListNext`.
  - Cluster refine (`lightrefine.comp`): flats read a per-cluster list, the union of their sections'
    lists minus lights whose sphere misses the cluster box, with a section mask per entry. It is
    conservative (pixel-identical: `lm_lightrefine 0` vs 1 is identical on SOS MAP32 and all 14
    dynlighttest vantages). Thresholds: `LM_REFINE_*`.
  - Specular / PBR: `LMP_SPECULAR` / `LMP_PBR` programs; glossiness / specular level per texture
    entry. `lm_programs` default 31.
  - Debug cvars: `lm_lights` (0: negative control, no light on levelmesh surfaces, CPU gathers for
    actors / decals), `lm_lightrefine`. Perflog v10: `lmlt lmlr lmlo lmlp lmlix lmlms lmlc`, plus
    `lmlf` (refine GPU ms).
  - Tooling: every engine launch of the kit runs under headless gamescope (`gamescope_wrap.py`;
    `run_matrix.sh` adds `-r 1000 --immediate-flips`, since the nested display caps at 60 Hz).
    `abtest.py` no longer pins `r_dynlights 0` and supports manifest `extra_files` (`lights.pk3`).
    Test kits: `make_dynlighttest.py` (14 vantages: light types, reachability, 3D floors, line
    portals) and `make_pbrtest.py`.
  - DC4 narrowed (ADR 0002 amendment): (a) summation order, (b) sprite additive × 5.0, (c) sprite
    shadow test (nearest shadowmap texel).
- 2026-10-21 (verification; all runs headless, one at a time; Debug build unless noted):
  - Negative controls differ: dynlighttest MAP01 `--set-b lm_lights 0` (lights vanish), and
    `ab_manifest_gather_monsters` and pbrtest MAP01 likewise.
  - Static A/B, classic vs levelmesh: gather_monsters (DOOM2 MAP01 / MAP23, SOS MAP32, lights.pk3)
    pass; DOOM2 MAP01 with `gl_ssao 1` passes; dynlighttest passes 7 of 14 vantages, including
    MAP12, the light 8 units behind a solid wall (byte-identical, no leak); shadowmaps on: MAP12
    passes. pbrtest: MAP02/03 pass; `lm_programs 7` vs 31 on the levelmesh is identical or passes.
  - The 7 failing dynlighttest vantages and pbrtest MAP01 are texel-edge specks. With lights off,
    MAP14 / MAP22 / MAP32 already fail with the same pixels; on the other maps 87–100 % of the lit
    failing pixels also differ unlit (sub-pixel texture-coordinate rounding of the plane-anchored
    vertices, under tolerance until lighting brightens them). The rest are scattered single pixels
    of delta 3–5 (DC4a). Candidates are staged in `allowlists/pending/` (DYNLIGHTTEST_MAP01, 02, 04,
    11, 14, 22, 32 and PBRTEST_MAP01) for human review (proposed: DC2 for the texel-edge specks,
    DC4a for the rest).
  - `ab_manifest_lighttest.json` (no dynamic lights in it): demo mode fails 28 / 31 / 29 ticks.
    Ticket 10 recorded 31 / 31 / 30 for the same DC2 tier-edge / CEIL texel speckle. Static
    LIGHTTEST_MAP01 fails identically (68 clusters per tick) with `r_dynlights 0` on both paths, so
    this predates ticket 19.
  - Save / load: a levelmesh save of dynlighttest MAP01, loaded under classic and levelmesh, gives
    lights on both (max delta 5, same class). The automap (`togglemap`) is identical.
  - Demo mode needs a NDEBUG build: the kit's demos assert in `D_ReadUserInfoStrings`
    (`d_netinfo.cpp:851`) in Debug. `build-rel` (RelWithDebInfo) was used. Demo A/B
    (gather_monsters, lights.pk3) fails on the same ticks with lights off on both paths (DOOM2 MAP01
    2010/2100/2190 noclip out-of-map views, MAP23 510/570/1020/1260, SOS MAP32 8 ticks): these
    failures predate this ticket. With lights on, the only addition is SOS MAP32 tick 90: two clusters
    of delta 3 (DC4a). DOOM2 MAP01 demo with `gl_ssao 1` fails on the same three ticks.
  - Perf (RelWithDebInfo, vantage timedemos, uncapped, `gl_uselevelmesh 1`, lights.pk3; HEAD =
    `fadcf84` without levelmesh surface lights; p50 ms):

    | vantage | wall HEAD | wall 19 | lmd1 HEAD | lmd1 19 | lmgpu HEAD | lmgpu 19 | lmlms | lmlf | lights |
    |---|---|---|---|---|---|---|---|---|---|
    | DOOM2 MAP27 V1 | 1.367 | 1.622 | 0.119 | 0.272 | 0.315 | 0.470 | 0.002 | 0.000 | 115 |
    | DOOM2 MAP31 V1 | 1.007 | 1.011 | 0.156 | 0.186 | 0.326 | 0.343 | 0.002 | 0.000 | 76 |
    | SOS MAP32 V1 | 21.110 | 20.994 | 0.194 | 0.259 | 0.595 | 0.662 | 0.024 | 0.000 | 533 |
    | myhouse MAP01 V1 | 9.584 | 9.285 | 0.111 | 0.147 | 0.417 | 0.454 | 0.065 | 0.000 | 285 |

    The light pass CPU cost (`lmlms`) is ≤ 0.002 ms on the DOOM2 maps and 0.065 ms on myhouse
    (285 lights over 132k subsectors). The GPU increase is the per-pixel light shading the HEAD
    levelmesh did not do (classic reference wall: MAP27 0.996, MAP31 0.999, SOS 50.0, myhouse 7.17).
    Refine on SOS MAP32 (static, monsters): `lmlf` p50 0.06 ms; `lmd1` unchanged (0.450 vs 0.451),
    because no SOS cluster's union exceeds `LM_REFINE_MIN` there.
