# 07: Clusters + Vulkan levelmesh resources

**Map:** `../map.md`: Device requirements, Data model (Clusters, State buffers).
**Type:** task
**Status:** needs-triage (pending the 05 gate)
**Blocked by:** 06

## What to build

- **Feature gating.** Request the required Vulkan 1.2 features through ZVulkan's
  `VulkanDeviceBuilder`: descriptor indexing, `drawIndirectCount`, `shaderDrawParameters`
  (`multiDrawIndirect` is already required). Expose a `SupportsLevelMesh()` capability. If ZVulkan
  needs changes, keep them minimal and flag them as subtree divergence in Comments. On capability
  failure, `gl_uselevelmesh` silently uses classic, with one console message.
- **Clustering.**
  - Walk the BSP and cut subtrees into clusters at the triangle budget chosen in ticket 05.
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

## Comments
