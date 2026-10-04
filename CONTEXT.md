# UZDoom Levelmesh Rendering

Domain glossary for the overhaul of UZDoom's level rasterization: replacing the per-frame BSP-traversal
rasterizer with a static GPU levelmesh (Helion-style). Terms are defined as they are locked by the
`.scratch/levelmesh-rendering/` effort; do not reuse them for unrelated concepts.

## Language

**Levelmesh path**:
The planned primary rasterization pipeline. The level is drawn as a static z-buffered mesh built once at
map load; per-frame CPU work is minimal, and visibility is resolved by the depth buffer.
_Avoid_: GPU renderer, new renderer, mesh renderer

**DoomLevelMesh**:
The existing class (`src/rendering/hwrenderer/doom_levelmesh.*`) that already builds whole-level geometry
at map load and is consumed only by the Vulkan raytracer. It is the seed of the levelmesh path, not the
levelmesh path itself.
_Avoid_: levelmesh (the path), static mesh

**Classic path**:
The legacy per-frame front-to-back BSP-traversal rasterizer (`hw_bsp.cpp` → drawsegs → batches). Retained
as a selectable fallback and the A/B reference until the levelmesh path is proven.
_Avoid_: legacy renderer, BSP renderer, old path

**Dynamic sector**:
A sector whose state (floor/ceiling heights or planes, light, textures) can change at runtime — doors,
lifts, 3D floor models. Topology is static in Doom: no runtime re-triangulation is ever needed.
_Avoid_: moving sector, animated sector, dynamic geometry

**Sector state buffer**:
The small per-sector dynamic attribute buffer (planes, light, texture/state indices) streamed to the GPU on
change and evaluated in the vertex shader to warp static geometry.
_Avoid_: instance buffer, dynamic buffer (too generic)

**Full parity**:
The levelmesh path reproduces every visual behavior of the classic path: portals/sector_link, fake flats,
sky portals, dynamic lights, and all sector effects. It is the acceptance bar, not a stretch goal.
_Avoid_: feature-complete, equivalent

**Proven**:
The state in which the levelmesh path has passed the acceptance bar (parity on the acceptance map list,
the frame-time bar, and the deterministic A/B tool). It is the gate for making levelmesh the default.
_Avoid_: stable, done

**Level vertex pool**:
The single whole-level 32-byte vertex store built once at map load, ordered per region then per piece
(subsector fan, line quad). Write-once except for re-bake patches; the first-movement flag flip rewrites
the in-vertex dual-purpose slot.
_Avoid_: VBO (backend-specific), mesh buffer

**Dual-purpose slot**:
The 8-byte `(slotA, slotB)` pair in every vertex: `(baked z, baked v)` for static surfaces,
`(vparam, 0)` for dynamic surfaces. The surface record's dynamic flag selects the interpretation.
_Avoid_: generic slot, variant data

**Surface record**:
The 48-byte CPU-writable SSBO record per wall quad / per subsector fan: mutable texture index and
dynamic flag; static plane refs, light ref, region index, and opening/unpegged/window parameters.
Reached from vertices through a per-vertex uint32 surface index.
_Avoid_: surface struct (too generic), surface attributes

**Plane ref**:
A `(sector index, floor|ceiling)` pointer into the sector state buffer. Includes refs to control /
3D-floor model sectors — that is how fake-flat surfaces move.
_Avoid_: plane pointer, height reference

**Re-bake**:
Restoring a dynamic surface's static vertices to the new resting height and clearing its dynamic flag
once movement completes. A surface is re-bake-eligible only when every plane it references has settled.
_Avoid_: static bake, geometry update

**Fake-flat surface**:
An extra static flat surface in the vertex pool referencing a 3D-floor model sector's (`heightsec`)
plane — the levelmesh's expression of `hw_FakeFlat`. No second pass, no runtime copies.
_Avoid_: fake flat (the classic `hw_FakeFlat` runtime mechanism), 3D-floor plane

**Portal region**:
A self-contained vertex + index block drawn under a region view transform, front-face flag, and
scissor/stencil — the levelmesh's unit of portal rendering. Ticket 06 defines the semantics; 04 locked
the storage container (transform, front-face, and per-texture IBO sub-range slots).
_Avoid_: portal mesh, region chunk
