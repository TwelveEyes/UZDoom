# 09: Vulkan draw path (synchronous)

**Spec:** `.scratch/levelmesh-rendering/spec.md` §7 (Vulkan fit — LevelMeshSet, the levelmesh
pipeline, record buffers, the occlusion-query pool). Detail:
`.scratch/levelmesh-rendering/ticket-breakup/digest-07-08.md` (the Vulkan fit: the LevelMeshSet
descriptor set, the vertex format, the samplerBuffer record buffers, the VK_QUERY_OCCLUSION
pool + window-poly buffer, the unchanged frame build) + `digest-slices-proposal.md` (slice 09).

**What to build:** Port the finished GL33 levelmesh feature set (03–08: static + load-dynamic
draw, 3D floors/bands, portals/sector_link/skybox, OOB/ortho exposure, dither fragments) to
Vulkan on the SAME GLSL as GL33 — the levelmesh vertex shader + the `main.fp`
promoted-uniform adaptation, incl. the `DITHERTRANS` variant — one shared GLSL source compiled
for both backends. The new `LevelMeshSet` descriptor-set layout, one set per level: surface
records (64 B/surface), the sector-state ring as ONE large buffer of HW_MAX_PIPELINE_BUFFERS
× level slots with the active slot selected by a per-slot selection uniform, 3D-light state
(12 B/light), and the static tables (band table, per-texture glow table, sub-range table +
region records); plus the EXISTING `HWBufferSet` (binds as today) and the EXISTING
per-material texture sets (reused as-is). The new pipeline for the levelmesh vertex format
(32 bytes, 6 attributes: position3, dual-purpose slot2, u, lightmap UV2, surface index) —
purely additive: a new cache key, no pipeline-cache invalidation of the classic/raytracer/
postprocess layouts; no new render pass — levelmesh draws into the existing scene render pass.
Record buffers as UNIFORM TEXEL BUFFERS (`samplerBuffer`) — native on Vulkan, same shared GLSL
as GL33 (texelFetch at integer texel = byte offset / 16; record sizes are 16-byte multiples).
The occlusion-query plumbing matching ticket 06's protocol: a per-level `VK_QUERY_OCCLUSION`
pool of one query object per portal (allocated at level load, sized to the portal count,
released at teardown) recording the pass count, not binary (free, and useful for A/B
tooling/debug), a per-frame result buffer, and the window-poly dynamic buffer (one quad per
portal, static endpoints + the current heights of the two portal-adjacent sectors), updated
per frame during the portal-transform step — game thread, same as GL33 (10 moves it to the
worker); queries issued after the main-region pass for the PREDICTED N+1 candidate set,
gating both the portal view's draw AND its depth clear. The frame build is UNCHANGED from
03–08: game thread, inside the interpolation window, synchronous issue at the render-view site
— the worker steps collapse, per the GL33 pattern. A/B: Vulkan pair on the then-current
GL33-green map set (parity is per-backend — same-backend pairs only) — zero tolerance; in-game
Vulkan walk on myhouse MAP01 (portals + 3D floors + skybox).

**Blocked by:** 06 (portal/skybox feature coverage) + 08 (OOB/dither feature coverage) — the
Vulkan port must match GL33's final feature coverage or the final per-backend matrix carries a
Vulkan hole.

**Status:** ready-for-agent

- [ ] Vulkan A/B zero-tolerance on the then-current GL33-green map set, incl. portal, 3D-floor, OOB, and dither maps (same-backend pairs only; fresh level load per path).
- [ ] LevelMeshSet (surface records, sector-state ring as one large buffer with the per-slot selection uniform, 3D-light state, static tables) + the existing HWBufferSet + the existing per-material texture sets; one shared GLSL source on both backends (levelmesh VS + `main.fp` adaptation incl. the `DITHERTRANS` variant).
- [ ] New pipeline for the 32-byte 6-attribute vertex format is purely additive — the classic/raytracer/postprocess pipeline-cache keys are untouched, no invalidation; no new render pass (existing scene render pass).
- [ ] Record buffers as uniform texel buffers (`samplerBuffer`), native on Vulkan.
- [ ] `VK_QUERY_OCCLUSION` pool (per-level, one query object per portal, allocated at load and sized to the portal count, pass count not binary) + the window-poly buffer match ticket 06's protocol (predicted N+1 candidate set; gates both the portal view's draw and its depth clear; no spurious un-occlude).
- [ ] Frame build UNCHANGED: game thread, inside the interpolation window, synchronous issue at the render-view site (worker steps collapse, per the GL33 pattern).
- [ ] Classic + GL33 paths unchanged (cvar 0 = classic); in-game Vulkan walk on myhouse MAP01.

**Note:**
1. Parity is per-backend — same-backend A/B pairs only; the classic-GPU baseline is the reference, not a cross-backend comparison.
