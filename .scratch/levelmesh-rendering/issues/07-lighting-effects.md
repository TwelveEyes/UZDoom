# Lighting & visual effects on static geometry

Type: grilling
Status: open
Blocked by: 02, 04

## Question

Lock the lighting and per-sector visual-effect model for the levelmesh path at full parity with the classic
path's look:

- Baked per-vertex light vs per-sector dynamic light (doors, light switches, runtime lightlevel changes).
- The classic per-segment light interpolation semantics (LINE_PASSLITE and friends) and how they map onto
  the mesh (vertex-baked vs shader-computed).
- Sector effects: glow, pulse, RIP, animated flats, scrolling, colormap (fire) — and how each maps to
  shader state / per-sector attributes.
- Point/dynamic lights and lightmap support (`GLightmapping`).
- Sky and sky-portal rendering in the mesh world (`hw_sky.cpp`, `hw_skyportal.cpp`).

End state: a spec-ready shading-model section (what is baked, what is per-sector state, what is a shader
variant) plus a parity checklist against the classic path.
