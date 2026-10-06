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

**Status:** ready-for-agent

- [ ] Build replaces the `DoomLevelMesh` build at the existing level-load build site; clean, warning-free build on both backends.
- [ ] Raytracer renders the level via the adapter; the `SetLevelMesh` rebuild-on-mesh-pointer-change contract holds; `VkRaytrace` untouched.
- [ ] SOS_Boom MAP32 pool/IBO/record sizes within the spec §3 memory budget.
- [ ] Classic-vs-classic smoke A/B on one map, both backends: zero-diff (classic path unchanged).

**Note:** The raytracer adapter is the standing-constraint deliverable (spec §1): the raytracer must render the level via the adapter, not the `NullMesh` fallback cube.
