# 07: Clusters + Vulkan levelmesh resources

**Map:** `../map.md`: Device requirements, Data model (Clusters, State buffers).
**Type:** task
**Status:** ready-for-agent
**Blocked by:** 06

## What to build

- **Feature gating.** Request what is required through ZVulkan's `VulkanDeviceBuilder`:
  - descriptor indexing;
  - `VK_KHR_draw_indirect_count` via `OptionalExtension` (ticket 05 used volk's
    `vkCmdDrawIndexedIndirectCountKHR` with no ZVulkan patch; ZVulkan has no Vulkan 1.2 core
    feature struct);
  - `multiDrawIndirect`, which is already required.

  Carry the surface index as a `flat` vertex attribute, so `shaderDrawParameters` is not needed. Expose a `SupportsLevelMesh()` capability. If ZVulkan
  needs changes, keep them minimal and flag them as subtree divergence in Comments. On capability
  failure, `gl_uselevelmesh` silently uses classic, with one console message.
- **Clustering.**
  - Walk the BSP and cut subtrees into clusters at a **256-triangle** budget (ticket 05). A leaf
    over budget is split into chunks.
  - Assign walls by the seg's subsector and flats by subsector.
  - Order the index buffer by cluster.
  - Each cluster record holds: xy AABB, z-range from load heights, index range, bucket mask, and a
    sector back-link range.
  - Also build the sector → cluster back-link table (ticket 09 uses it to refresh z-ranges).
- **Vulkan resources (new `VkLevelMesh` module under `vulkan/renderer/`).**
  - Device-local buffers: vertices, indices, surfaces, clusters, sector-state, side-state,
    plane-xform, texture-translation table.
  - A per-frame staging ring (`HW_MAX_PIPELINE_BUFFERS` slots) for dirty uploads, with copy
    commands recorded before the scene render pass.
  - A bindless texture table: a variable-count, partially bound sampled-image array, with each
    `FMaterial` layer allocated a slot on demand. It must coexist with
    `VkDescriptorSetManager`'s existing per-material sets.
  - A new `LevelMeshSet` descriptor-set layout and pipeline layout, purely additive (the classic
    layouts are untouched).
- **Lifetime.** Created at level setup and destroyed at level teardown and on device reset /
  `vid_restart`. Nothing is created when the path is off.
- **Diagnostics.** A memory and stats console command: clusters, triangles, buffer sizes, bindless
  slot count.

## Acceptance

- [ ] Feature gating verified on the reference GPU. A forced-missing-feature cvar or env override
      falls back cleanly.
- [ ] Cluster stats for all acceptance maps logged (count, triangles per cluster distribution).
      Linear build.
- [ ] Resources survive `vid_restart` and map changes. No validation-layer errors (`vk_debug 1`).
- [ ] Classic path and `VkRaytrace` unaffected.

## Notes from ticket 05

- The spike's cluster build (`spike-05-gpucull`: `src/rendering/hwrenderer/spike_mesh.cpp`) took
  36 ms for SOS MAP32 (487k triangles) and 86–90 ms for myhouse MAP01 (1.18 M), unoptimized.
- Dirty uploads: use a host-visible update list plus a compute scatter (ticket 09), not per-record
  `VkBufferCopy` regions. The staging ring here should allow for that.

## Comments

- 2026-10-13 (from 06): the bake is `LevelMeshBake` (`hw_levelmeshbake.h`). Vertex 36 B, surface
  96 B (std430). Gate it on the capability check instead of `IsVulkan()` in `LevelMeshBake_Create`.
  Free `Vertices` / `Indices` after upload; keep `Surfaces` (or a slim CPU copy) and the side lists.
  myhouse MAP01 has 2.4M vertices / 1.24M triangles, so plan the buffer sizes for it.
