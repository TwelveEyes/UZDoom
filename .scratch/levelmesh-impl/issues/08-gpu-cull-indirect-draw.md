# 08: GPU cull + indirect draw core (main view)

**Map:** `../map.md`: Frame model (GPU, per view), Shaders, Transitional strategy.
**Type:** task
**Status:** needs-triage (pending the 05 gate)
**Blocked by:** 03, 07

## What to build

The first real levelmesh draw, main view only, on Vulkan:

- **Culling compute** (two-phase, per ticket 05's findings):
  - per-view cluster visibility bitset (persistent across frames);
  - phase 1: frustum-test last frame's visible clusters, then write `VkDrawIndexedIndirectCommand`s
    per bucket (opaque, masked) plus atomic counts;
  - draw with `vkCmdDrawIndexedIndirectCount`;
  - Hi-Z build: a depth pyramid built from the scene depth (handle MSAA depth: resolve with
    max/min, or build from sample 0, as chosen in ticket 05);
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

## Acceptance

- [ ] DOOM2 static set (MAP01, MAP07, MAP31): Vulkan classic vs levelmesh A/B with the fullbright
      override passes under ADR 0002 (expected classes: DC1 / DC2 only).
- [ ] Hi-Z phase 2 adds no visible pop-in: a demo A/B shows no DC3 clusters in the main geometry.
- [ ] GPU timings logged on the stress vantages (informational; the walk still runs).
- [ ] No validation errors; classic path unchanged.

## Comments
