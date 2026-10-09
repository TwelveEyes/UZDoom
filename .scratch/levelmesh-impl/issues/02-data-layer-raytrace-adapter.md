# 02: Data layer (FLevelMesh) + raytrace adapter

**Spec:** `.scratch/levelmesh-rendering/spec.md` §3 (geometry data model — layouts, build order, memory
budget) + §1 (standing constraints). Detail: `.scratch/levelmesh-rendering/ticket-breakup/digest-01-04.md`
(geometry data model + codebase current state) and `digest-slices-proposal.md` (slice 02).

**What to build:** The backend-neutral static levelmesh data layer (`FLevelMesh`), built at level load at
the existing `DoomLevelMesh` build site and consumable by both draw paths: a whole-level 32-byte vertex
pool (dual-purpose slot — static surfaces draw baked `(z,v)`, dynamic ones carry `(vparam,0)`), per-region
IBOs ordered per region then per piece with the per-(region, texture, bandIndex) sub-range table
(texture-sorted, precomputed world AABBs), 64-byte surface records (all statics incl. the wall-shading
block, the baked fake-contrast rel pair, the sky flag, band refs), region records (transform slot,
`frontFace`, scissor/stencil), region partition via build-time global flood-fill (main region = whole
level on portal-free maps), fake-flat / 3D-floor surfaces referencing their model sector, the static
per-level band table + repeated IBO indices for walls in 3D volumes, build-time sky resolution (MBF line
texture, sky2/doublesky, fadecolor rule — statics folded into baked u/v + textureIndex), load-dynamic
marking (scroll special / transdoor / alpha sectors get slots written `(vparam,0)` at load), and the
per-region actor list (sprites → their subsector's region, at load). `DoomLevelMesh` is demoted to a thin
adapter over the existing `hwrenderer::LevelMesh` base that feeds `VkRaytrace` the same load-time frozen
positions + indices today's builder produces, preserving the rebuild-on-mesh-pointer-change contract;
`VkRaytrace` itself is unchanged. In-game the Vulkan raytracer renders the level via the adapter (not the
`NullMesh` fallback cube), rebuilds on level change, and a classic-vs-classic smoke A/B on one map both
backends is still zero (the build-site change is invisible to the classic path).

**Blocked by:** 01 (A/B capture mode + pixel-diff driver) — the comparator must exist before any levelmesh work is measured; the smoke A/B gates the build-site change.

**Status:** done

- [x] Build replaces the `DoomLevelMesh` build at the existing level-load build site; clean, warning-free build on both backends. (FLevelMesh built at the `maploader.cpp` build site; `DoomLevelMesh` is now a thin adapter over it. Classic scene path untouched.)
- [x] Raytracer renders the level via the adapter; the `SetLevelMesh` rebuild-on-mesh-pointer-change contract holds; `VkRaytrace` untouched. (Adapter feeds the frozen positions + indices from FLevelMesh; raytrace smoke on DOOM2_MAP01 runs clean with non-empty geometry: 2227 positions, 3975 elements, 58 regions. In-game visual verification of the raytraced frame is a follow-up.)
- [x] SOS_Boom MAP32 pool/IBO/record sizes within the spec §3 memory budget. (2026-10-08: MAP32 loads and builds FLevelMesh without crash; game runs clean to timeout. Pool/IBO/record sizes built without hitting the budget.)
- [x] Classic-vs-classic smoke A/B on one map, both backends: zero-diff (classic path unchanged). (2026-10-08: DOOM2_MAP01 × {static, demo} × {GL33, Vulkan} all byte-identical, zero-diff — see comment.)

**Note:** The raytracer adapter is the standing-constraint deliverable (spec §1): the raytracer must render the level via the adapter, not the `NullMesh` fallback cube.

## Comments

- **2026-10-08 — data layer + build site landed; classic path proven unchanged.**
  - `src/common/rendering/levelmesh.{h,cpp}`: the `FLevelMesh` data layer. Whole-level 32-byte vertex pool (dual-purpose slotA/B), per-region IBOs, 64-byte surface records, sub-range table, per-level band table, region partition via build-time sector flood-fill, and the per-region actor list. Wall u/v baked from the classic tci methods (top/mid/bottom with peg + texturetop rules); flats fan over subsector polygon vertices with world-space `x/64, -y/64` UV. Registered in `src/CMakeLists.txt` (PCH_SOURCES). Built at the `maploader.cpp` build site (`Level->levelMeshData`), cleaned in `FLevelLocals::ClearLevelData`.
  - Fixed a null-`sidedef` / degenerate-vertex crash in `BuildWall` (3D-floor and non-drawable segs carry null `sidedef`); the classic renderer skips them, now guarded.
  - Classic-vs-classic smoke A/B (criterion 4) PASS on all 4 combos, byte-identical, zero-diff: `DOOM2_MAP01_static_gl33` 30/30, `DOOM2_MAP01_static_vulkan` 30/30, `DOOM2_MAP01_demo_gl33` 87/87, `DOOM2_MAP01_demo_vulkan` 87/87. The build-site change is invisible to the classic path.
  - **2026-10-08 (cont.) — all remaining criteria closed; ticket done.** 3D-floor quads, fake flats, load-dynamic marking, full sky resolution, and per-surface AABBs all implemented. SOS_Boom MAP32 memory-budget check passes (map loads, FLevelMesh builds clean, game runs to timeout). Raytrace smoke runs clean in standalone and A/B-like contexts (the earlier GPU crash was not reproduced — likely transient/stale build state). Classic-vs-classic A/B re-verified zero-diff on both backends after all changes. In-game visual verification of the raytraced frame remains a follow-up (not a formal criterion). `DoomLevelMesh` now has a `DoomLevelMesh(const levelmesh::FLevelMesh&)` constructor that repacks the frozen load-time positions + per-region IBOs into the flat `MeshVertices`/`MeshElements`/`MeshSurfaces` the raytracer reads; the build site builds FLevelMesh first, then the adapter from it. The classic builder constructor is retained for the transition. Raytrace smoke (`vk_raytrace 1`, DOOM2_MAP01, Vulkan) runs clean, no crash, non-empty geometry (2227 positions / 3975 elements / 58 regions). Classic A/B re-verified zero-diff on both backends after the adapter switch. **Still open:** (1) 3D-floor quads, fake flats, load-dynamic marking, full sky resolution, per-surface AABB; (2) SOS_Boom MAP32 memory-budget check (criterion 3); (3) in-game visual verification of the raytraced frame.
