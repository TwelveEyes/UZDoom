# Handoff A2 — structural fixes (flat z, sub-range table, actor list)

## Files changed
- `src/common/rendering/levelmesh.h` — new `LevelMeshTexRange` struct; comments
  updated for `LevelMeshSubRange` (now explicitly per-surface), the region record's
  `texRangeOffset`/`texRangeCount` and `actorOffset`/`actorCount` semantics, and the
  `FLevelMesh::subRanges`/`texRanges`/`actors` members.
- `src/common/rendering/levelmesh.cpp` — the three work items below. No existing
  function restructured or renamed; `EmitFlatFan`'s parameter changed from a
  precomputed `float z` to `const secplane_t &plane` (its only 4 call sites updated).

## Work item 1 — per-vertex flat z
`EmitFlatFan(subsector_t *sub, const secplane_t &plane, float lmuv[2],
uint32_t surfaceIndex)` now bakes `z = (float)plane.ZatPoint(x, y)` per fan vertex
instead of one reference-vertex z. Call sites:
- subsector floor fan  → `sec->floorplane`
- subsector ceiling fan → `sec->ceilingplane`
- 3D-floor model floor fan    → `model->floorplane`
- 3D-floor model ceiling fan  → `model->ceilingplane`
The now-unused `vertex_t *ref` and the four `float z = …ZatPoint(ref…)` lines were
removed from `BuildFlat`. `mesh.positions` (raytrace adapter) gets the same
per-vertex z. Horizontal planes are byte-identical to before; slanted planes
(3D-floor model sectors) are now correct.

## Work item 2 — per-(region, texture) sub-range table
New 32-byte record in the header (vertex 32 B / surface 64 B untouched):
```
struct LevelMeshTexRange
{
	uint32_t textureIndex;
	uint32_t iboOffset;     // start of the group's span in the region's IBO
	uint32_t iboCount;      // span length
	float aabbMin[2];
	float aabbMax[2];
	uint32_t firstSurface;  // first per-surface sub-range of the group
	uint32_t surfaceCount;  // per-surface sub-ranges in the group
};
```
- `FLevelMesh` gained `TArray<LevelMeshTexRange> texRanges` alongside the existing
  per-surface `TArray<LevelMeshSubRange> subRanges` (kept, build order, parallel to
  surfaces, world AABBs as before).
- Region record reference: `texRangeOffset`/`texRangeCount` now index a contiguous
  run in `texRanges` — one entry per distinct texture in the region, sorted by
  texture index (spec section 3 region record / section 5 `texRange` contract).
- `BuildSubRanges` rebuilt: per region it collects the region's surface indices,
  stable insertion-sorts them by `textureIndex` (build order preserved within a
  texture), then emits one `LevelMeshTexRange` per contiguous texture run with
  `iboOffset` = min surface iboOffset, `iboCount` = span to the max surface
  iboOffset+iboCount, AABB = union of the group's per-surface AABBs, and
  `firstSurface`/`surfaceCount` spanning the parallel per-surface records.
- Supporting fix: per-surface `LevelMeshSubRange::iboCount` was never set (memset 0);
  `EmitWallQuad` now sets 6 and `EmitFlatFan` sets `3*(n-2)` at emission — required
  for the group span.
- Note for later chunks: the region IBO itself is still laid out in build order
  (walls then flats), so a texture group's span is the union range over its
  surfaces; making each span an exact draw range (IBO re-layout per texture) is
  deferred to the backend/draw chunks (D/E). Band splits (section 6) will add
  further entries inside a texture group per the spec.

## Work item 3 — region actor list
`BuildActors` no longer stores `mo->tid`. `mesh.actors` is now a flat list of
subsector indices (into `FLevelLocals::subsectors`): for each region, all
subsectors whose sector maps to that region, in subsector-index order (the walk
is in order, so the list is sorted). `region.actorOffset`/`actorCount` bound each
region's run. The draw pass walks each listed subsector's live `sub.sprites` list
at draw time, so live actor movement/respawn is picked up without a rebuild.
Header comments on `LevelMeshRegion::actorOffset/actorCount` and
`FLevelMesh::actors` updated accordingly. (`a_sharedglobal.h` include kept in the
cpp; `AActor`/`DVisualThinker` are no longer referenced by this file.)

## Build result
`cmake --build build --config RelWithDebInfo --parallel 3` from repo root:
```
[ 77%] Building CXX object src/CMakeFiles/zdoom.dir/common/rendering/levelmesh.cpp.o
[ 78%] Building CXX object src/CMakeFiles/zdoom.dir/common/rendering/hwrenderer/data/hw_clock.cpp.o
[ 78%] Linking CXX executable ../uzdoom
[100%] Built target zdoom
```
Zero warnings from `levelmesh.cpp`/`levelmesh.h`; the only warnings in the tree
are the known pre-existing `a_dynlight.h:266` -Wdeprecated-anon-enum-enum-conversion
baseline in unrelated TUs.
