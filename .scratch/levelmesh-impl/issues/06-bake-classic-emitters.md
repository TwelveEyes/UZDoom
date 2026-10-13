# 06: Bake via classic emitters + plane-anchored surfaces

**Map:** `../map.md`: Data model (Bake, Plane-anchored vertices, Surfaces). ADR 0003.
**Type:** task
**Status:** ready-for-agent
**Blocked by:** 01, 05

## What to build

A backend-neutral bake module under `src/rendering/hwrenderer/` (it needs game types). It runs at
level setup **only** when `gl_uselevelmesh` is on and the active backend supports the levelmesh
path.

- **Walls.** For every non-polyobject seg, run `HWWall::Process` through
  `HWWallDispatcher(Level, &meshHelper, lightmode)` (mesh-helper mode, `hw_walldispatcher.h:27-52`).
  - Capture the resulting `HWWall`s: texture, flags, `tcs`, `ztop`/`zbottom`, the gl_seamless
    vertex splits from `MakeVertices`, the lightmap coords, and the light inputs.
  - View-dependent and unbakeable outputs go to **side lists**, not the mesh: `mh->translucent`
    (ticket 16), `mh->portals` (ticket 15), and `mh->upper` / `mh->lower` missing-texture fills
    (ticket 16).
  - Add the small hooks the capture needs (for example, a path that produces vertices without
    `screen->mVertexData`). Do not copy classic logic.
- **Flats.** Use the classic static flat builder (`hw_vertexbuilder.cpp`
  `CreateVerticesForSubsector` / `CreateIndexedSectorVertices`) for every subsector and plane,
  3D-floor planes included (ticket 12 consumes them). Keep the classic triangulation exactly.
- **Plane anchoring.** For every vertex, determine which plane defines its z (front/back
  floor/ceiling, or a 3D-floor model plane) and store `(x, y, zOffset, anchor)` so that
  `ZatPoint(plane) + zOffset` reproduces the baked z **exactly** at load. Anchor the `v` coordinate
  to the texture-top plane in the same way. Assert the round trip at bake time (debug builds) and
  count the mismatches (release).
- **Surface records** (std430 layout, integer fields):
  - plane refs and light ref, side-state ref (`side × tier`, for ticket 11), untranslated texture
    id;
  - draw-state bits: masked, clamp, cull mode (back-face for walls; floors from above, ceilings
    from below; 3D-floor flats double-sided), material bucket;
  - flags (sky, 3D floor, dither-eligible, ...);
  - baked static light ingredients (side light, tier light, fake-contrast rel pair, side light
    flags);
  - lightmap page and UV.
- **Normals.** Per surface (wall direction, plane normal), stored in or derived from the record.
- **Performance and memory.** Linear time and memory: no per-region scans and no O(n²) dedup. Free
  the CPU-side copies after upload, except what the CPU consumers need (AABBs, back-links).

## Acceptance

- [ ] Bake round-trip: 0 z / v mismatches on all 26 acceptance maps (report the per-map count).
- [ ] Bake time logged per map. Linear scaling shown across DOOM2 MAP01 → myhouse MAP01. No bake
      when the cvar is off or on GL.
- [ ] Side lists populated and counted (translucent, portal, missing-texture).
- [ ] Debug dump: the CPU-rasterized wireframe of the baked mesh (or an OBJ export) matches the
      classic geometry on DOOM2 MAP01 / MAP07 by visual check.
- [ ] Clean build; classic path unchanged.

## Notes

Check ticket 04's findings on `HWMeshHelper`'s original use. Polyobject walls are excluded here
(ticket 13). `isculled` and sky-hack branches in `HWWall::Process` must be checked for view
dependence.

## Comments
