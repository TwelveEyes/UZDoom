# 07: Clusters + Vulkan levelmesh resources

**Map:** `../map.md`: Device requirements, Data model (Clusters, State buffers).
**Type:** task
**Status:** resolved
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

- [x] Feature gating verified on the reference GPU. A forced-missing-feature cvar or env override
      falls back cleanly.
- [x] Cluster stats for all acceptance maps logged (count, triangles per cluster distribution).
      Linear build.
- [x] Resources survive `vid_restart` and map changes. No validation-layer errors (`vk_debug 1`).
      (UZDoom has no `vid_restart`; see Comments.)
- [x] Classic path and `VkRaytrace` unaffected.

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
- 2026-10-13: resolved.
  - **Code.**
    - `src/common/rendering/hwrenderer/data/hw_levelmeshdata.h`: the std430 GPU records. The
      vertex and surface records moved here from the bake header. New: cluster, sector state,
      side state, plane xform, texture-table entry, and `LevelMeshUploadData`.
    - `src/rendering/hwrenderer/hw_levelmeshclusters.cpp`: the clustering.
    - `src/rendering/hwrenderer/hw_levelmeshresources.cpp`: initial state records, upload,
      residency, and `lm_stats`.
    - `src/common/rendering/vulkan/renderer/vk_levelmesh.{h,cpp}`: `VkLevelMesh`.
    - Hooks: `DFrameBuffer::SupportsLevelMesh / SetLevelMeshData / LevelMeshResident /
      PrintLevelMeshStats`, `VkMaterial::DeleteDescriptors`, `SetTextureFilterMode`,
      `VulkanRenderDevice::BeginFrame`, the maploader, `ClearLevelData`, and `RenderViewpoint`.
  - **Gating.**
    - No ZVulkan patch. The device builder adds `OptionalDescriptorIndexing()` and
      `OptionalExtension(VK_KHR_draw_indirect_count)`; ZVulkan already copies the five descriptor
      indexing feature bits when the extension is present.
    - `VkLevelMesh` checks the extensions, the volk entry point, `multiDrawIndirect`, the five
      feature bits, and a bindless capacity of at least 4096. The capacity is the minimum of the
      UAB limits minus 256, capped at 65536.
    - `LevelMeshBake_Create` now gates on `screen->SupportsLevelMesh(true)` instead of
      `IsVulkan()`. GL stays silent.
    - `vk_levelmesh_forcemissing "<name>,..."` (or `all`) fakes missing features. Verified: the
      forced run prints one line (`Levelmesh: this Vulkan device lacks ... (forced), ...;
      gl_uselevelmesh uses the classic renderer.`), skips the bake, and the next map with the
      cvar cleared bakes and uploads normally.
  - **Clusters (budget 256, `lm_cluster_budget`).**
    - Walls take the leaf of `side.segs[0]` (the seg they are emitted from); flats take their
      subsector.
    - Section hole fills (`SSECF_HOLE`) are subsectors outside the BSP. Without a fix, Hexen
      MAP01/10/27 and myhouse MAP01 lost 2–88 triangles. Their surfaces now go to the BSP leaf
      at their centre ("hole surfaces": 2/6/10/21).
    - The index buffer is rewritten in cluster order, and within a cluster by bucket. Records
      are 64 B: AABB, z from the load heights, index range, per-bucket index counts, bucket
      mask, sector range (vertex-anchor sectors) and surface range.
    - The sector→cluster CSR stays on the CPU. A chunked leaf can share a surface with the
      neighbouring cluster ("split surfaces": 2–3 on two maps).
    - Every map's cluster triangle total equals the bake's.
  - **Survey.** `tools/abtest/results/lm_bake_survey.{json,md}`, second table. Build 0.03–0.06
    µs/triangle (linear): myhouse MAP01 has 1.24 M triangles in 7541 clusters, built in 74 ms; SOS
    MAP32 has 504 k in 3557 clusters, 28 ms. p50 is 150–180 triangles per cluster.
    - Big maps have a tail of tiny clusters: the small siblings of over-budget subtrees (SOS
      MAP32 has 628 clusters under 32 triangles). Merging adjacent small siblings is a possible
      ticket 08 refinement if the per-cluster cost shows.
    - The bake columns are unchanged from ticket 06 (diffed).
  - **Resources.** Device-local SSBOs: vertices (also vertex buffer), indices (also index
    buffer), surfaces, clusters, cluster→sectors, cluster→surfaces, sector state, side state
    (side×tier), plane xform (sector×plane), and the texture table (8 B per texture id).
    - myhouse MAP01: 159 MB, uploaded in 28–40 ms. SOS MAP32: 64 MB.
    - CPU vertices and indices are freed after the upload; `lm_keepgeometry 1` keeps them for
      `lm_dumpobj`.
    - Staging ring: `HW_MAX_PIPELINE_BUFFERS` host-coherent slots with `STORAGE` usage, so a
      ticket 09 compute scatter can read them. `AllocUpload`, `QueueCopy`, and `FlushUploads`
      record the copies plus a barrier into the transfer command buffer.
    - Bindless:
      - Set 3 is a variable-count, partially bound, UAB sampled-image array (65536 on RADV).
        Samplers are a separate `sampler[NUMSAMPLERS]` array in set 2, indexed by clamp mode.
      - Each material gets a contiguous block, one slot per layer; slot 0 is the null texture.
      - The texture table maps the untranslated id to `{slot, numLayers}`, using the current
        animation frame at resolve time and `CTF_Upscale` as the classic path does.
    - Pipeline layout: 0 fixed, 1 HW buffers (classic), 2 `LevelMeshSet`, 3 textures, plus the
      classic push constants. All classic layouts are untouched.
  - **Descriptor writes happen only in `BeginFrame`.** ZVulkan does not enable
    `descriptorBindingUpdateUnusedWhilePending`, and the renderer submits mid-frame. Every write
    (slots, samplers, table) is therefore done at frame start, when `Update()` has waited for all
    fences. A texture first requested during a frame is bound from the next frame on.
    - `VkMaterial::DeleteDescriptors` (precache, filter change, cleanup, destruction) frees the
      material's slots and re-queues its table entries. Verified with `gl_texture_filter` changes
      and map changes; slot counts stay stable.
  - **Lifetime.**
    - Upload at level setup (`LevelMesh_UpdateResources` right after the bake). Release in
      `ClearLevelData`. Old objects go through `DrawDeleteList`; the pools are freed after their
      sets.
    - A per-frame residency check in `RenderViewpoint` re-bakes and re-uploads if the device copy
      was lost, or drops the path if the device can no longer run it.
    - **UZDoom has no `vid_restart`.** The framebuffer is created once at startup, so that path
      cannot be exercised. Verified instead: map changes, `vid_setsize`, filter changes, quit
      (map teardown).
  - **Validation.** `vk_debug 1` on all 26 maps through `lm_bake_survey.py --vkdebug`, plus map
    change, filter change and quit. No new VUIDs. Every run shows the same two pre-existing
    swapchain-recreate errors (`vkDestroySemaphore-05149`, `vkDestroySwapchainKHR-01282`), and
    myhouse MAP01 shows a pre-existing classic `COMBINED_IMAGE_SAMPLER` layout error (04150).
    All of these also appear with `gl_uselevelmesh 0`.
  - **Classic unaffected.** The 16 demo captures (DOOM2 MAP07, Pirates MAP43, SOS MAP12, Hexen
    MAP10 × 4 ticks) are pixel-identical to the ticket 06 build, with the cvar both 0 and 1.
    With `vk_raytrace 1`, DOOM2 MAP07 is pixel-identical with the cvar 0 vs 1. GL ignores the
    cvar.
  - **Finding for 08/11:** `LMSF_MASKED` is unreliable at bake time. `FTexture::Masked` starts
    true for patch and multipatch textures, and only becomes false when the pixels are loaded
    (`SmoothEdges`). Nearly every cluster is therefore "mixed-bucket". The bucket split must come
    from runtime material state (ticket 11) or be resolved after precache.
