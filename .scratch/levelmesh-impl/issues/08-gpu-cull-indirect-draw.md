# 08: GPU cull + indirect draw core (main view)

**Map:** `../map.md`: Frame model (GPU, per view), Shaders, Transitional strategy.
**Type:** task
**Status:** resolved
**Blocked by:** 03, 07

## What to build

The first real levelmesh draw, main view only, on Vulkan:

- **Culling compute** (two-phase, per ticket 05's findings):
  - per-view cluster visibility bitset (persistent across frames);
  - phase 1: frustum-test last frame's visible clusters, then write `VkDrawIndexedIndirectCommand`s
    per bucket (opaque, masked) plus atomic counts;
  - draw with `vkCmdDrawIndexedIndirectCount`;
  - Hi-Z build: a max-depth pyramid built from the scene depth (MSAA: see the findings below;
    sample 0 alone is not conservative);
  - phase 2: the remaining clusters, frustum plus Hi-Z;
  - update the bitset.
- **Render-pass integration.** Compute must run outside a render pass. Add an explicit
  `VkRenderState` seam to end and resume the scene render pass around the cull and Hi-Z dispatches.
  Keep the attachments loaded. No new render pass types.
- **Draw pipelines.**
  - Levelmesh vertex stage: plane-anchored z from the sector state (load values until ticket 09),
    `flat` surface index, normals.
  - Fragment: the shared `main.fp` via a levelmesh include. Interim lighting: the ticket-03
    fullbright override, or a simple sector light level until ticket 10.
  - Bindless texture fetch via the translation table.
  - Opaque pipeline without `discard`; masked pipeline with the `gl_mask_threshold` alpha test.
  - Back-face cull per draw-state bits; clip distances written (`gl_ClipDistance`) for whatever the
    render state enables.
- **Integration.**
  - `HWDrawInfo::RenderScene` calls a new `DFrameBuffer::DrawLevelMesh(di, state)` seam when the
    level uses the levelmesh. On success it skips drawing the classic plain/masked lists **except**
    surfaces the levelmesh does not own yet (sky, translucent, portal, polyobject, hack fills,
    3D-floor pieces until ticket 12). Ownership comes from the bake's side lists and flags, not
    from ad-hoc checks.
  - The classic walk still runs (transitional).
- **Perflog.** GPU timestamps per phase in new `r_perflog` columns, plus counts of clusters and
  triangles drawn.

### Ticket 05 findings to apply (research 05)

- **Draw count.** Under shading load, GPU cost follows the number of indirect draws, not the
  triangles: 2367 per-cluster draws cost 1.6 ms where one draw cost 1.0 ms. Keep the draws per
  bucket low. Measure Ironwail-style index compaction (one draw per bucket) against per-cluster
  commands.
- **Hi-Z role.** Hi-Z is the visibility source for tickets 10/17/18. It is not a frame-cost
  optimization: on the 7900 XTX a no-cull draw is as fast or faster. Keep a no-cull diagnostic
  mode for A/B and low-end comparisons. Report the GPU timings of both.
- **Hi-Z mips round down** (Vulkan mip sizes). The last row and column of an odd-sized level must
  also take the extra source texel. Ceil-halved sizes silently drop texels and cull visible
  geometry.
- **Depth barriers.** `VkImageTransition` only knows fragment-stage shader reads, and render
  passes do not update `VkTextureImage::Layout`. Use explicit barriers: attachment → shader-read
  for compute → back. Alternatively, extend the helper.
- **Pass split.** The prototype seam is `VkRenderState::SpikeBeginPass()`. It begins or continues
  the scene pass and re-applies the viewport/scissor. `EndRenderPass()` afterwards rebinds the
  classic state. The first levelmesh pass consumes the pending scene clear.
- **MSAA.** A conservative mip 0 (max over samples) costs 0.26 ms at 4× vs 0.03 ms without MSAA.
  Choose a policy: lower-resolution mip 0, a `VK_KHR_depth_stencil_resolve` MAX resolve, or no
  Hi-Z under MSAA.
- **Verification.** The spike's check is cheap and strong: two-phase output must match
  frustum-only output pixel for pixel, up to z-fight pixels, on a moving tour in the A/B capture
  mode (one sim tic per frame).

## Acceptance

- [x] DOOM2 static set (MAP01, MAP07, MAP31): Vulkan classic vs levelmesh A/B with the fullbright
      override passes under ADR 0002 (expected classes: DC1 / DC2 only).
- [x] Hi-Z phase 2 adds no visible pop-in: a demo A/B shows no DC3 clusters in the main geometry.
- [x] GPU timings logged on the stress vantages (informational; the walk still runs).
- [x] No validation errors; classic path unchanged.

## Comments

- 2026-10-13 (from 07): resources are in `VkLevelMesh` (`vulkan/renderer/vk_levelmesh.h`).
  - Use `GetPipelineLayout()`: sets 0 fixed, 1 HW buffers, 2 `LevelMeshSet` (bindings
    `ELevelMeshBinding`), 3 bindless textures, plus the classic push constants.
  - Vertex buffer: `LevelMeshVertex`, 36 B. Index buffer: cluster-ordered `uint32`.
  - Cluster record (`hw_levelmeshdata.h`): `bucketIndices[LMB_COUNT]` are consecutive index
    ranges from `firstIndex`.
  - The bake's `LMSF_MASKED` marks almost everything masked, because textures are unloaded at
    bake time (07 Comments). Until ticket 11, expect nearly every cluster to hit both buckets,
    or re-derive the bucket after precache.
  - Many tiny clusters on big maps (SOS MAP32: 628 under 32 triangles). Consider merging small
    siblings if the per-cluster cost shows.
  - Descriptor writes may only happen in `VkLevelMesh::BeginFrame` (no
    `UpdateUnusedWhilePending`).
- 2026-10-14: resolved.
  - **Code.**
    - `src/common/rendering/vulkan/renderer/vk_levelmeshrenderer.{h,cpp}` (new):
      `VkLevelMeshRenderer`. It owns the per-level visibility, counter, argument and compaction
      buffers, the Hi-Z image, levels and sets, the draw-pipeline cache (keyed on the render
      target state) and an 8-slot timestamp / counter readback ring that never blocks.
    - Shaders in `wadsrc/static/shaders/levelmesh/`:
      - `lm_data.glsl`: the std430 records, mirrored from `hw_levelmeshdata.h`.
      - `levelmesh.vp`: vertex pulling, plane-anchored z, the classic flat texture matrix, and
        the same outputs as `main.vp`.
      - `lm_frag.glsl`: redirects `main.fp`'s sampler names to the bindless table.
      - `cull.comp`, `hiz.comp`.
    - Seams:
      - `DFrameBuffer::DrawLevelMesh(LevelMeshView, FRenderState&)`.
      - `VkRenderState::GetExternalTarget / BeginExternalDraw / EndExternalDraw` plus the
        existing `EndRenderPass` for compute splits.
      - `HWDrawInfo::DrawLevelMesh`: called from `RenderScene`. It sets the classic fullbright
        wall state, then `HWDrawList::RemoveLevelMeshOwned` drops the owned walls and flats from
        the plain and masked lists, after a successful draw only.
    - The classic set-2 sampler declarations are under `#ifndef LEVELMESH` in the generated
      shader text. `uLevelMeshFlags` takes a former push-constant padding slot.
  - **Cull.**
    - Visibility is one `uint` per cluster in a single buffer, which is main view only. Ticket
      15 makes it per view.
    - Two-phase Hi-Z (`lm_cullmode 2`, the default):
      - Phase 1 frustum-tests last frame's visible set and draws it.
      - Hi-Z is built from that depth.
      - Phase 2 frustum + Hi-Z tests the rest, draws, and updates the visibility.
    - The frustum test rejects only on x/y and on "wholly behind the eye" (w ≤ 0), with no
      near/far rejection. The scene draws with depth clamp, so geometry in front of the near
      plane still rasterizes. Fixing this removed a 4-px frustum-mode seam on Pirates MAP43.
    - `lm_cullmode 0` (none) and `1` (frustum) are diagnostic modes.
    - Hi-Z mips round down, and odd edges take the extra texel. Under MSAA, mip 0 is the max
      over all samples. That policy costs 0.075 ms vs 0.060 ms at 1×; the spike's 0.26 ms did
      not reproduce.
    - Explicit depth barriers move the depth image attachment → read-only → attachment.
      `VkTextureImage::Layout` stays valid because the layout is restored.
    - A new Hi-Z image goes to `GENERAL` before the first cull, because every phase binds it.
  - **Draw.**
    - `lm_drawmode 0` (compaction, default): one `drawIndexedIndirect` per bucket and phase.
    - `lm_drawmode 1`: one command per cluster through `drawIndexedIndirectCount`.
    - Measured equal (research/08-gpu-timings.md), so compaction stays the default: no
      draw-count dependency.
    - The opaque pipeline has no alpha test; the masked pipeline uses `gl_mask_threshold`.
    - Back-face culling is on, with the front face counter-clockwise on screen. This was
      verified against `lm_backfacecull 0`; the first guess (clockwise) culled everything
      visible.
  - **Bake changes this ticket needed.**
    - The masked rule now follows `HWDrawInfo::AddWall` / `AddFlat`: two-sided mids,
      3D-floor sides with masked textures, and only 3D-floor or portal planes. This replaces the
      unreliable `isMasked()` everywhere (07 finding).
    - Ceilings are reversed to the classic winding.
    - `LMSF_TEXCLAMPY` follows the classic clamp-Y rule.
    - The texture table carries display size and layer flags.
  - **Ownership (wider than the spec's list).** On top of sky, translucent, portal, polyobject,
    hack-fill and 3D-floor pieces, the bake defers (`LMSF_DEFERRED`, whole side tier) the
    following to the classic path:
    - heightsecs, light bands, and sky-hack mid textures;
    - warped, canvas, normal-mapped and non-default-shader materials;
    - side or plane special / additive colours and texture manipulation.

    The colour test reads the effective values (`HasWallColorEffects`). `WALLF_EXTCOLOR` is
    useless here because `sector_t::CheckExColorFlag` sets it on every side at load; the first
    build deferred every wall. These cases are tested at bake time; runtime changes are ticket
    10/11.
  - **Verification.**
    - **Static set** (classic vs levelmesh, fullbright, Vulkan):
      - MAP07 is 30/30 byte-identical.
      - MAP01 and MAP31 are 30/30 pass (only sub-min-size specks).
      - Pirates MAP43 and Heretic E1M2 static also pass.
    - **Hi-Z vs frustum-only**, demo mode, both levelmesh, fullbright, using the new
      `abtest.py run --set-a/--set-b` pins:
      - DOOM2 MAP01: 87/87 identical.
      - MAP07: 85 identical + 2 pass. MAP31: 86 + 1. SOS MAP46: 85 + 2. Planisphere MAP01:
        82 + 5.
      - Pirates MAP43: 84 + 3 after the cull fix.
      - No DC3 anywhere.
      - Hi-Z is exercised: occlusions and phase-2 draws occur on every map (perflog
        `lmocc`, `lmcl2`).
    - **Validation:** `lm_bake_survey.py --vkdebug` on all 26 maps, plus `vk_debug` timedemos.
      Only the known pre-existing VUIDs appear; all also appear with the cvar off.
    - **Classic unchanged:**
      - Same-build determinism holds on 4 maps × 87 demo ticks.
      - Against the ticket-07 binary (`../UZDoom/build`, clang), DOOM2 MAP07 is 87/87
        byte-identical.
      - Pirates MAP43 differs in 48/87 ticks, through player view bob only: the weapon sprite
        and floor shift about 3 px while moving; sky and HUD are identical.
      - That reference is a dirty pre-commit build (`383+04575e3-m`, Makefiles), not a clean
        ticket-07 baseline. The user accepted this as-is (2026-10-14).
      - A GCC build of this tree differs from the clang reference by about 580 scattered
        float-noise pixels per frame. Compare like-for-like compilers.
    - **Timings:** research/08-gpu-timings.md, 11 vantages × classic / Hi-Z / frustum /
      per-cluster / none.
      - Levelmesh GPU time is 0.12–0.23 ms, roughly flat.
      - CPU `wall` p50 drops 2–2.4× on Planisphere and SOS MAP32 with the walk still running.
  - **Out of scope, seen in classic-vs-levelmesh demo A/B:**
    - Pirates MAP43: scrolling side offsets (ticket 11) and an upper wall drawn against the sky
      at tick 120 (moving sector or sky, 09/14).
    - SOS MAP46: animated textures (11).
    - Tour demos use static A/B for 08.
  - **Tooling.**
    - `abtest.py run --set-a/--set-b 'CVAR VALUE'` adds per-path pins.
    - `r_perflog v3` adds the `lm*` columns.
    - README notes: `vk_debug` is archived, so never `+set` it without a scratch `-config`.
      `run_matrix.sh --set` also persists into the user's ini.
  - **Known gaps for later tickets (amended):**
    - Cluster AABBs use load-time heights, so culling can drop visible geometry on moving
      sectors (09).
    - Partial plane deferral would double-draw (latent) (09/10).
    - Back-face culling is a global pipeline bit; mirrored views need the flipped front face
      (15).
    - Visibility is not per view (15).
    - `LMSF_DEFERRED` decisions are static (10/11).
