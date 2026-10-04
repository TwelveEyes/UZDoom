# Levelmesh geometry data model

Type: grilling
Status: resolved
Blocked by: 01, 02

## Question

Lock the static levelmesh's data model — the spec's core section:

- Vertex attributes and layout: positions stored sector-relative or 2D+plane (per the locked warp
  direction), UVs, baked light, texture/state indices, sector indices, dynamic flags.
- Storage: one whole-level buffer set vs per-portal-region chunks; 32-bit indexing; memory budget for
  slaughter-scale levels.
- How the model extends (or replaces) the existing `DoomLevelMesh` builder in
  `src/rendering/hwrenderer/doom_levelmesh.*`.
- How fake flats are expressed (extra flat surface vs second pass).
- What `VkRaytrace` needs from the data (it is a co-consumer and must keep working — or specify the
  raytracer-side change the spec must plan for).
- Backend-neutrality is mandatory: the data layer serves both the Vulkan and GL33 draw paths.

End state: a spec-ready section with concrete structs/buffer layouts and a build-order note.

## Answer

Resolved 2026-10-03 (grilling, 3 rounds; every branch below was explicitly chosen).

### Locked decisions

1. **Warp model: baked XYZ, dynamic-only eval.** Static surfaces draw baked z/v directly; only
   surfaces carrying the dynamic flag evaluate `z` in the vertex shader from the sector state buffer
   (`z = dot(normal, xy) + D`, `V = z - plane.TexZ + vparam`). No per-vertex interpolation state.
2. **Storage: one whole-level vertex pool + per-region index buffers.** No per-portal vertex copies.
   32-bit indexing (see budget below).
3. **Class: new backend-neutral class; `DoomLevelMesh` demoted to the raytrace adapter.** The model
   lives in `src/common/rendering/` (naming tentative — the spec finalizes; suggest `FLevelMesh`) so both
   the GL33 and Vulkan draw paths consume it. `VkRaytrace` itself is unchanged; a small adapter in
   `hwrenderer/` feeds it the same load-time frozen geometry (positions + indices) that today's builder
   produces, keeping its "rebuild on mesh-pointer change" contract (`vk_raytrace.cpp:49-60`). Note:
   `DoomLevelMesh::IsControlSector` is disabled (returns `false`, `doom_levelmesh.cpp:382-386`), so the
   adapter exposes the full surface set — no control-sector skip to preserve.
4. **Fake flats = extra static surfaces.** Each 3D floor / flood surface (F3DFloor model sector) gets
   its own floor + ceiling fan in the same pool, plane refs pointing at the model sector (`heightsec`).
   No second pass, no runtime copies. 3D-floor *structure* is static data: line special 160
   (`Sector_Set3DFloor`) is `LS_NOP` at runtime (`p_lnspec.cpp:3707`); `P_Recalculate3DFloors` (called
   from `P_ChangeSector`) only recomputes heights/lights from control planes — sector movement never
   invalidates the pool.
5. **Dynamic lifecycle: flag flip + re-bake to static at rest.** A sector's first movement flips the
   dynamic flag on every surface referencing its planes and rewrites the vertices' dual-purpose slot
   (`(z,v)` → `(vparam,0)`) via sub-upload. When the movement completes and the sector rests at a NEW
   height, the CPU re-bakes: patches baked `(z,v)` from the settled final (non-interpolated) height and
   clears the flag. A surface is re-bake-eligible only when ALL planes it references have settled.
   Settled-but-dynamic surfaces (e.g. partner still moving) simply wait; there is no "once dynamic,
   always dynamic" state.
6. **Vertex format: 32-byte uniform layout with a dual-purpose 8-byte slot.** One attribute set for
   both static and dynamic surfaces; the surface record's dynamic flag selects the slot interpretation.
   Lightmap UVs are stored on every vertex (static; ignored when the surface has no lightmap).
7. **Unpegged/middle/lower windows: computed in the vertex shader.** The window clamps derive from the
   four evaluated planes (front floor/ceiling, back floor/ceiling) + per-surface static offsets
   (opening mask, unpegged flag, window offsets). No per-frame vertex upload exists anywhere in the
   model; the code path is uniform for static and dynamic walls.
8. **Surface records in a CPU-writable SSBO, reached by a per-vertex uint32 index.** One record per
   wall quad / per subsector fan (today's `Surface` granularity). Runtime texture swaps and flag flips
   are single-record sub-uploads; the VS does one SSBO fetch per vertex.
9. **Pool ordering: per region, then per piece.** Main region block first, then each portal region's
   block; within a block, subsector fans and line quads are contiguous ranges. A region is a
   self-contained vertex+index block (independent allocation/streaming later); re-bake = one sub-upload
   per (region, sector) pair.

### Buffer layouts (spec-ready)

**Vertex — 32 bytes, stride 32:**

```text
offset  type    field
0       float   x
4       float   y
8       float   slotA    // static: baked z  |  dynamic: vparam
12      float   slotB    // static: baked v  |  dynamic: 0
16      float   u
20      float   lmu      // lightmap UV (always stored)
24      float   lmv
28      uint32  surfaceIndex
```

`vparam` semantics: `V = evaluatedZ - plane.TexZ + vparam` (Doom wall/flat scale is always 1; wall
`vparam` folds in the unpegged/window offsets that do not track a plane).

**Surface record — 48 bytes, one per wall quad / subsector fan (SSBO, CPU-writable):**

```text
offset  type     field
0       uint32   textureIndex   // MUTABLE: ACS texture swaps (single-record sub-upload)
4       uint32   regionIndex
8       uint32   planeRef[4]    // each: sectorIndex<<1 | planeBit (0=floor, 1=ceiling)
                                //   flat: [0]=own floor, [1]=own ceiling
                                //   wall: [0]=front.floor [1]=front.ceil [2]=back.floor [3]=back.ceil
32      uint32   lightRef       // sectorIndex<<2 | slot (0=floor, 1=ceiling, 2=wall); static,
                                // resolved through heightsec transfer-light relationships at build
36      uint32   flags          // bit0 dynamic (MUTABLE) | bit1 sky | bit2 hasLightmap
                                // bits4-6 openingMask (upper/lower/middle) | bit7 unpeggedLower
40      float    vparam[4]      // [0] lower unpegged offset (rel. front floor), [1] middle window
                                // offset, [2] upper window offset, [3] reserved
```

**Region record — container locked here, semantics owned by 06:**

```text
uint32  vertexOffset, vertexCount     // contiguous block in the pool
uint32  iboOffset, iboCount           // into the region's index buffer
uint32  texRangeOffset, texRangeCount // into the per-texture sub-range table:
                                      //   { textureIndex, iboOffset, iboCount } per (region, texture)
transform                             // 06: remapped viewpoint (rotation/translation/angle diff/z rebase)
frontFace                             // CCW | CW — MUST be inverted for mirrored portal regions
                                      // (negative-determinant transform flips winding)
scissor/stencil state                 // 06
```

**Sector state buffer — requirements only; layout and streaming owned by 05.** Indexed by
`sector_t` index. Per sector: floor + ceiling plane (normal, D, `TexZ`), light (floor/ceiling/wall
slots), texture indices per plane. Plane slots are consumed only by dynamic-flagged surfaces (plus
dynamic wall surfaces for window math); light/texture by all surfaces. 05 must also define the
dirty-sector stream and a **"settled" event** (movement completion at the final non-interpolated
height) — that event is 04's re-bake trigger, and the first-movement flag flip must be ordered
against the sector's first state-buffer upload (both on the render-thread update path).

### Memory budget (order-of-magnitude; 09/10 validates at true scale)

- Typical 50k-line level: ~1M vertices ≈ 32 MB pool, ~10 MB IBO, ~7-15 MB surface records.
- Slaughter-class (200k lines, several 3D-floor layers): ~3-5M vertices ≈ 100-160 MB pool, ~20 MB
  IBO, ~30-55 MB surface records. 32-bit index ceiling: 4.29B vertices = 137 GB of 32-byte data —
  32-bit indexing is safe by orders of magnitude. 64-bit indexing is not available in GL33 core and
  has no Vulkan core path, so it is ruled out as non-portable.

### Build order (map load; replaces/augments the `DoomLevelMesh` build at `maploader.cpp:3251`)

1. Resolve plane references for all sectors — including control / 3D-floor model sectors — and
   transfer-light relationships into `lightRef`.
2. Enumerate surfaces: line quads (upper/lower/middle, both sides, incl. 3D-floor boundaries),
   subsector floor/ceiling fans (degenerate triangles skipped — parity with the existing builder),
   extra fake-flat + flood surfaces per 3D-floor model.
3. Assign regions (06), then emit per region: contiguous per-piece vertices + per-texture-sorted
   indices.
4. Fill the surface record SSBO.
5. Initialize the sector state buffer (initial planes/lights/textures — owned by 05).
6. VkRaytrace snapshot: build the load-time frozen geometry (positions + indices) for the adapter;
   raytracer behavior unchanged.

**Runtime mutation paths** (all defined over the write-once pool; timing owned by 05's render-thread
update protocol):

- First movement of a sector: set `dynamic` on every referencing surface; rewrite dual-purpose slots
  over the affected per-(region, sector) vertex ranges.
- Per tick: stream the state buffer for dirty sectors (05).
- Settled (05 event): re-bake — patch baked `(z,v)`, clear `dynamic` (only when all referenced planes
  have settled).
- Texture swap (ACS): single surface-record sub-upload.
- Rebuild: level load (new `FLevelLocals`). Sector movement never rebuilds (structure static, see
  decision 4).

### Handoffs to dependent tickets

- **05 (sector state):** owns state-buffer layout/streaming, dirty-sector set, "settled" event, and
  the 02 graduates (per-frame exposure pass, dither hook). Contract from 04: per-sector
  (planes, light, texture) indexed by sector index; re-bake consumes the settled event; flag flip
  ordered against first upload.
- **06 (region model):** fills the region record (transform, `frontFace`, scissor/stencil) and assigns
  regions; mirrored portal regions must set `frontFace = CW`.
- **07 (lighting/sky):** consumes `lightRef` → state-buffer light slots; `sky` flag on surface
  records. Sky surfaces are geometrically ordinary (z from plane); sky texturing is 07's.
- **08 (draw organization):** consumes the per-region per-texture IBO sub-range table, the vertex
  format, and surface records; a draw = one (region, texture) sub-range; transparency is a texture
  property; path-selection cvar/ZWidget entry is 08's.
- **11 (game-logic feedback):** no data-model requirements beyond 05's.

### Decision provenance

Round 1: warp = baked-XYZ/dynamic-only-eval (user overrode the eval-all recommendation); storage =
pool + per-region IBO; class = new backend-neutral class + raytrace adapter; fake flats = extra
static surfaces. Round 2: rest policy = **re-bake to static at rest** (user overrode the
"stays dynamic" recommendation); vertex format = 32B uniform dual-purpose slot; unpegged windows =
shader-computed; surface records = SSBO + per-vertex index. Round 3: pool ordering = per region,
then per piece.
