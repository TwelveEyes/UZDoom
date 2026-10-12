# Levelmesh implementation map: GPU-driven, Vulkan-only

This map is the source of truth for the effort. The tickets in `issues/` are work items that point
back into it. It replaces the old parity-first spec, map, and ADRs (deleted 2026-10-11). The reasons
are in `audit-2026-10-11.md`; this map implements that audit's **Option B**.

## Destination

On Vulkan, the 3D world is drawn from a static, whole-level **levelmesh** that is baked once at map
load. Visibility is decided **on the GPU**: a compute pass does per-view cluster culling (frustum plus
two-phase Hi-Z occlusion) and writes indirect draw commands. Materials are bound bindlessly, so each
view needs only a handful of draws. The per-frame CPU cost is O(changed state + small dynamic sets)
and does not depend on level size.

The classic BSP rasterizer stays in place:

- It is the **only** path on the GL backend.
- It is the fallback on Vulkan devices that lack the required features.
- It is the A/B reference.

The success measure is performance (§ Perf bars). Visual parity is gated by the delta-class policy
(§ Acceptance policy, ADR 0002), not by zero tolerance.

## ADRs

- `docs/adr/0001-gpu-driven-vulkan-levelmesh.md`: Vulkan-only GPU-driven pipeline; GL stays classic.
- `docs/adr/0002-delta-class-acceptance.md`: pixel A/B with tolerance plus reviewed delta classes.
- `docs/adr/0003-bake-by-classic-emitters.md`: geometry is baked by running the classic wall/flat
  emitters at load, so classic geometry logic is not reimplemented.

## Inputs

- `audit-2026-10-11.md`: why the previous design could not be fast, and the option analysis.
- `research/01-helion-levelmesh.md`, `research/02-consumer-inventory.md`, `research/03-portal-flow.md`:
  factual research kept from the old effort. 02 lists every consumer of classic per-frame visibility,
  which drives tickets 17–18. 03 is the classic portal flow, which drives ticket 15.

## Locked decisions (2026-10-11, user-approved)

- **Backend:** Vulkan only. GL keeps the classic path; a GL 4.6 port is out of scope.
- **Existing code:** keep the A/B capture mode, `tools/abtest.py`, and the `r_perflog` / perf kit.
  Remove the rest of the old levelmesh implementation (ticket 01).
- **Acceptance:** pixel A/B with a small tolerance plus documented, human-reviewed delta classes.
  Geometry and visibility must match exactly wherever they can be tested (ADR 0002).
- **Docs:** old spec, tickets, maps, and ADRs deleted; research and audit kept; `CONTEXT.md` rewritten.

## Architecture

### Device requirements and selection

Required:

- Vulkan 1.2 with `drawIndirectCount` and `multiDrawIndirect` (already required by ZVulkan).
- `shaderDrawParameters` (`gl_DrawID` / base instance).
- Descriptor indexing: `runtimeDescriptorArray`, `descriptorBindingPartiallyBound`,
  `descriptorBindingVariableDescriptorCount`, `shaderSampledImageArrayNonUniformIndexing`, and
  update-after-bind for sampled images.

Optional:

- `VK_EXT_conditional_rendering`, for portal gating (ticket 15; there is a fallback).

If anything required is missing, the classic path is used and a message is logged. ZVulkan is a git
subtree (`tools/update-subtrees.sh`). Request features through its builder API
(`VulkanDeviceBuilder::OptionalDescriptorIndexing()` exists; `drawIndirectCount` and
`shaderDrawParameters` are not enabled today). Any local ZVulkan patch is a subtree divergence and
must be flagged.

Selection: `gl_uselevelmesh` (existing Bool cvar, `CVAR_ARCHIVE | CVAR_GLOBALCONFIG`, default false)
is latched per map at level setup. It is ignored on the GL backend.

### Data model (ticket 06–07)

**Bake.**

- Walls and flats are produced at load by the **classic emitters**:
  - `HWWall::Process` runs through `HWWallDispatcher`'s existing mesh-helper mode
    (`HWMeshHelper`, `hw_walldispatcher.h:27-52`, currently unused). It collects `HWWall`s without a
    draw info.
  - The classic static flat triangulation (`hw_vertexbuilder.cpp` `CreateVerticesForSubsector` /
    `CreateIndexedSectorVertices`) supplies the flats.
- View-dependent outputs (portal walls, missing-texture fills, translucent walls) are recorded
  separately, not baked into the opaque mesh.
- The build is linear-time, and it runs only when the levelmesh path is selected on a capable device.

**Plane-anchored vertices.**

- Every vertex stores `(x, y, zOffset, anchor)`. `anchor` selects one of the surface's plane refs,
  and z = `ZatPoint(plane, x, y) + zOffset`. The `v` coordinate is anchored the same way to the
  texture-top plane.
- There is **no static/dynamic lifecycle and no re-bake**: every surface always tracks its planes.
  The cost is about four FLOPs per vertex, and the plane is fetched anyway for lighting.

**Surfaces.** One record per wall piece / flat per subsector, as integer std430 SSBO records. Each
carries:

- plane refs (`sector << 1 | plane`), a light ref, and a side-state ref;
- the **untranslated** texture id, draw-state bits (masked, clamp, cull, material bucket), and flags
  (sky, 3D floor, dither, ...);
- baked static light ingredients (side light, tier light, fake-contrast rel pair).

**Clusters.**

- Spatial groups of surfaces, built from BSP subtrees and cut when a subtree reaches a target
  triangle budget (~64–256 triangles; ticket 07 tunes this).
- Each cluster record holds an xy AABB, a z-range, a triangle range in a cluster-ordered index
  buffer, and its bucket membership.
- The z-range is refreshed through sector→cluster back-links when a referenced plane moves
  (ticket 09).
- Clusters are **spatial only**. Portal topology is a separate concern (ticket 15).

**State buffers** (device-local SSBOs, updated **dirty-only** through a per-frame staging ring plus
copy commands; there is no full-frame streaming):

- sector state: planes, light ingredients, colormap, glow, flags;
- side state: per side × tier texture id, offsets, scales;
- plane xform: flat offsets, scale, rotation;
- polyobject transforms;
- the 3D-light state;
- the **texture translation table**: untranslated id → bindless slot, refreshed when animations
  advance. Frame animation and swaps never touch geometry.

### Frame model (end state, after ticket 20)

**CPU, game thread**, inside the `D_Render` interpolation window:

1. Flush dirty records (sector, side, plane, polyobject, 3D light, translation table, per-surface
   flags).
2. Build the views: main, then portal / mirror / skybox views (ticket 15), then camera textures.
3. Gather the small CPU sets from the frustum plus visibility feedback (ticket 17): actors
   (sprites, models, particles), decals, translucent level surfaces, hack fills.

**GPU, per view:**

1. **Cull phase 1.** Clusters visible to this view last frame, frustum-tested, write indirect
   commands per bucket.
2. Shading resolve for those surfaces.
3. Draw.
4. Build the Hi-Z pyramid from the resulting depth.
5. **Cull phase 2.** All other clusters, frustum plus Hi-Z, write commands; resolve; draw.
6. Update the view's cluster-visibility bitset, and the per-surface visibility bits used by
   feedback.
7. Copy the visibility bits into a host-readable ring. CPU consumers read them a pipeline depth
   later.

Then, in classic pass order, using the existing `FRenderState` machinery: models, decals, SSAO,
portals, then the translucent pass (`DrawSorted`, which includes translucent level surfaces and
sprites).

**Shading resolve.**

- A compute pass runs once per visible surface. It ports the classic per-surface light chain:
  effective light, `RescaleLightLevel`, `CalcRelLight`, `CalcLightLevel`, `CalcLightColor`,
  fog / cullcolor, desaturation, glow. Its output is a compact resolved record.
- The vertex shader reads that record once, via a `flat` surface index, and fragment code consumes
  it.
- Per-vertex light evaluation is forbidden.

**Shaders.** "Levelmesh" is a **vertex-stage variant axis** of the existing shader matrix, not an
effect. Fragment shaders are shared with the classic path through a common include. They are not
forked. Opaque and masked buckets are separate pipelines, and only the masked pipeline contains
`discard`.

**Threading.** Single-threaded issue on the game thread. There is no render worker; reconsider one
only if measurements show issue cost on the critical path.

**Raytracer.** `VkRaytrace` stays on the classic `DoomLevelMesh` builder (restored in ticket 01) and
must keep working in every ticket.

### Transitional strategy

Until ticket 20, the classic BSP walk keeps running on the levelmesh path. It draws whatever the
levelmesh does not own yet, and each ticket moves ownership over. Per-ticket GPU timings are
tracked, but the perf bars only apply after ticket 20 removes the walk from the frame. Ticket 05
(the spike) provides the early perf evidence: it is the **go/no-go gate**.

## Acceptance policy (ADR 0002)

- Pixel A/B pairs Vulkan classic against Vulkan levelmesh, with fresh level loads per path (the A/B
  capture mode and the synthetic clock are unchanged).
- Each pixel diff must either fall within the tolerance (defined in ticket 03) or be explained by a
  **delta class**. Each class is reviewed by a human and recorded per map in the allowlist JSON.
  New classes need an ADR 0002 amendment.
- Initial delta classes:
  - **DC1 z-fight:** coplanar or near-coplanar surfaces, where either winner is legitimate.
  - **DC2 float z:** plane-anchored GPU z versus classic CPU z rounding, at sub-pixel edges.
  - **DC3 feedback latency:** CPU consumers of GPU visibility (actor gather on newly revealed
    clusters, exposure, dither) lag by at most the readback depth. This must be bounded and
    measured.
  - **DC4 light selection:** clustered light binning versus per-surface lists at edges
    (ticket 19).
  - **DC5 visibility precision:** exact depth visibility versus the classic clipper's
    approximations. This affects game-state sets (exposure / dither eligibility), not pixels.
- Game state (exposure flags, savegames) is compared by set-diff, with DC5 differences classified.

## Perf bars (set in ticket 02; placeholders until then)

- **No regression:** levelmesh p50 and p95 total frame time ≤ classic × 1.05 on every acceptance
  map, measured on Vulkan.
- **Payoff:** on the stress vantage set (large maps, open vistas, portal-dense, 3D-floor-dense),
  levelmesh p95 ≤ 50 % of classic p95. The levelmesh's own render-CPU window must not grow with
  level size.
- Measured on real views (ticket 02 fixes the camera problem in the old baseline), on the reference
  machine, and on one low-end GPU when available.

## Standing constraints (every ticket)

- Classic path unchanged and selectable; GL backend unaffected.
- `VkRaytrace` keeps working.
- Nothing is built or uploaded for the levelmesh when it is not selected; no load-time regression on
  the classic path.
- A clean build is the per-change smoke test (user preference); A/B runs on request or at ticket
  acceptance. Test runs are launched unsandboxed on the real GPU (a sandbox silently falls back to
  llvmpipe).
- This is fork work: `CONTRIBUTING.md` prohibits upstreaming AI-generated code. Flag this before any
  commit or PR.

## Tickets

| # | Ticket | Type | Blocked by |
|---|---|---|---|
| 01 | Tear down the old levelmesh implementation | task | — |
| 02 | Perf baseline v2: real views, stress vantages, bars | task | — |
| 03 | A/B tooling v2: tolerance + delta classes, Vulkan pairs | task | 01 |
| 04 | Prior-art survey: VKDoom / GZDoom levelmesh, HWMeshHelper | research | — |
| 05 | Spike: GPU-driven culling go/no-go | prototype | 02, 04 |
| 06 | Bake via classic emitters + plane-anchored surfaces | task | 01, 05 |
| 07 | Clusters + Vulkan levelmesh resources | task | 06 |
| 08 | GPU cull + indirect draw core (main view) | task | 03, 07 |
| 09 | Dirty state stream + plane warp (moving sectors) | task | 08 |
| 10 | Shading resolve (light chain) | task | 09 |
| 11 | Runtime texture state + material buckets | task | 10 |
| 12 | 3D floors | task | 10 |
| 13 | Polyobjects | task | 09 |
| 14 | Sky | task | 10 |
| 15 | Portals, mirrors, skyboxes, horizons (multi-view) | task | 12, 14 |
| 16 | Translucent surfaces, fog boundaries, hack fills | task | 10 |
| 17 | Visibility feedback + actor/decal gather | task | 15 |
| 18 | Game-logic feedback: exposure, dither, OOB/ortho/radar | task | 17 |
| 19 | Dynamic lights (clustered forward+), shadowmaps, SSAO normals | task | 10 |
| 20 | Retire the BSP walk from the levelmesh frame | task | 11, 13, 16, 18, 19 |
| 21 | Integrate: matrix, perf bars, soak, PROVEN, default flip | task | 03, 20 |

Critical path: 02/04 → 05 → 06 → 07 → 08 → 09 → 10 → 12/14 → 15 → 17 → 18 → 20 → 21.
Tickets 01–05 are `ready-for-agent`. Tickets 06–21 are `needs-triage` until the ticket 05 gate
resolves; the spike's findings may change them, and a no-go stops the effort for a re-plan.

## Working the frontier

A ticket can be picked up once every ticket in its `Blocked by` list is resolved; the lowest number
wins. Only one agent works on this repo at a time. Resolution notes go under `## Comments` in the
ticket file, and decisions that change this map are written back here, under Decisions log.

## Decisions log

- 2026-10-11: map created from the audit's Option B (user-approved decisions above).
