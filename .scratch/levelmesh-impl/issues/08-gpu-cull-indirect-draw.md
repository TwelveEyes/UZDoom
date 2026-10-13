# 08: GPU cull + indirect draw core (main view)

**Map:** `../map.md`: Frame model (GPU, per view), Shaders, Transitional strategy.
**Type:** task
**Status:** ready-for-agent
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

- [ ] DOOM2 static set (MAP01, MAP07, MAP31): Vulkan classic vs levelmesh A/B with the fullbright
      override passes under ADR 0002 (expected classes: DC1 / DC2 only).
- [ ] Hi-Z phase 2 adds no visible pop-in: a demo A/B shows no DC3 clusters in the main geometry.
- [ ] GPU timings logged on the stress vantages (informational; the walk still runs).
- [ ] No validation errors; classic path unchanged.

## Comments
