# Levelmesh geometry data model

Type: grilling
Status: open
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
