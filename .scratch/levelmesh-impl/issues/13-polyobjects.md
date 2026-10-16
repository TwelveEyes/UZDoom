# 13: Polyobjects

**Map:** `../map.md`: Data model (State buffers: polyobject transforms).
**Type:** task
**Status:** resolved
**Blocked by:** 09

## What to build

- **Bake.** Bake each polyobject's walls in polyobject-local space at spawn, using the classic
  emitter on the polyobject's sidedefs, and anchor z to the planes of the sector containing them.
  Give each polyobject its own clusters.
- **Transform record.** Keep a per-polyobject transform (rotation + translation), uploaded
  dirty-only when the polyobject moves (`FPolyObj` move / rotate). The vertex stage applies it
  before plane anchoring.
- **Cluster bounds.** Recompute the polyobject cluster AABBs from the transform on move, so the cull
  stays exact.
- **Containing-sector changes.** Handle polyobjects that cross subsector boundaries. The classic
  splits polyobject segs per subsector (`PO_LinkToSubsectors`), but the levelmesh draws them whole,
  z-buffered. Check that lighting uses the classic's sector choice, and document any difference.

## Acceptance

- [x] HEXEN MAP01 / MAP10 / MAP27 (12 / 7 / 9 active polyobjects): A/B static + demo passes, only
      DC1 / DC2.
- [x] Moving doors and rotators show no pop-in or culling holes in the demo A/B.

## Comments
- 2026-10-14 (from 09): cluster xy boxes are still static. The 09 refresh only rewrites
  `bmin.z / bmax.z`, and `scatter.comp` only writes those two words per cluster. A polyobject
  that moves needs an xy refresh, or clusters of its own.
- 2026-10-16: claimed. Test kit: `tools/abtest/make_polytest.py` → `wads/polytest.wad` and
  `demos/polytest/POLYTEST_MAP01.lmp`; manifest `ab_manifest_polytest.json` (12 rooms, see the
  README). Baseline with polyobjects on the classic path on both sides: 155 / 216 ticks fail,
  all edge specks (at most 449 px, max delta 53; run `20261016T193509Z-ab`). Ticket 25 already
  removed the polyobject *origin* subsectors from the bake.

## Answer

**Bake.** `Baker::ProcessPolyobjects` (`hw_levelmeshbake.cpp`) runs every side of a polyobject
through `HWWall::Process`, making the call `HWDrawInfo::AddPolyobjs` makes for a mini-BSP seg:

- `Process(seg, currentsector, seg->backsector ? currentsector : nullptr)`;
- the seg's front / back sectors are the side's, from `FNodeBuilder::AddPolySegs`: the origin
  container. Two-sided polyobject walls take their heights and texture alignment from it,
  their light from the sector they are drawn in;
- the seg spans the whole side, at the spawn position. The classic's per-subsector cut only
  adds edge vertices.

Each baked polyobject:

- gets `LMSF_POLYOBJ` surfaces and clusters of its own, emitted after the BSP walk
  (`LevelMeshBake::PolyClusterStart`);
- after clustering, has its vertices made local (minus a whole-unit origin:
  `LocalizePolyobjects`).

Only a polyobject inside one sector at bake is baked: its walls' planes and light come from
that sector (the classic's `currentsector`). Polyobjects stay classic when:

- they lie across sectors, or sit in a heightsec / 3D-floor sector, or have portals;
- one of their walls is translucent, a portal, fog or deferred.

**Transform.** `LevelMeshPolyobj` is an affine 2×3 (`m[4]`, `t[2]`), not rotation +
translation: `DPolyobjInterpolation` moves each vertex linearly between two tics, which is an
affine map of the local coordinates, not a rotation.

- `LevelMesh_FillPolyobj` fits it by least squares to the polyobject's current, interpolated
  vertices (exact for rigid moves and their interpolation; a collinear polyobject takes the
  rotation of its longest offset). At rest it snaps to the exact identity, so the baked
  vertices are reproduced bit for bit.
- The vertex stage applies it before plane anchoring (`LM_VertexXY`), so z follows the planes
  where the polyobject is now. The normal follows through the cofactor matrix.
- The polyobject index rides in the side record (`LevelMeshSideState::polyobj`, a former
  reserved word).
- The side record also carries the side's current fake-contrast pair
  (`LevelMeshSideState::fakeContrast`), read by `resolve.comp` for polyobject surfaces. The
  classic recomputes the contrast from the rotated line, so a rotating wall's light changes.
- The transform is streamed dirty-only through the scatter (new binding
  `LMBIND_POLYOBJS`); every baked polyobject is compared on every update.

**Cluster bounds.** The cluster update record is now a whole box (`LevelMeshClusterBox`, 8
words, replacing `LevelMeshClusterZ`). A moved polyobject's clusters get the image of their
local box under the transform, padded for rounding. Their z range is then taken over that
box from the anchor planes, as before.

**Containing-sector changes.** Every update checks whether a baked polyobject's subsector
links (`PO_LinkToSubsectors`, run in `D_Render` after interpolation) reach a sector other than
its bake sector.

- While they do, its side records are `LMTS_DEFERRED`: the vertex stage hides its surfaces,
  and the side tiers go back to the classic draw lists (`TierOwned`), which split it per
  subsector with each piece's sector, as before.
- This is the documented difference from the ticket text: the levelmesh does not draw a
  polyobject across sectors; it hands it to the classic (the same pattern as moving 3D
  floors). Lighting therefore always matches the classic's sector choice.

**Debug.**
- `lm_polyobjs 0` (level setup) leaves every polyobject classic.
- `lm_polybox 0` freezes the polyobject cluster boxes (the negative control).
- `lm_statelog 1` logs polyobjects that leave / re-enter their sector.
- `lm_bakestats` prints `polyobjects N, baked N (N wall surfaces); classic: by sector N, by
  wall N`.

**Results** (Vulkan, demo mode unless noted):

- **polytest** (`make_polytest.py`, 12 rooms):
  - classic vs levelmesh: 158 / 227 ticks fail, all edge specks (at most 391 px, max delta
    30). Before this ticket the same map, polyobjects classic, failed 155 / 216 at up to 449
    px / delta 53 (older demo). The only new diff seen while developing was a 1-px edge along
    the rotating bar's corner.
  - `lm_shading 0` on B shows the levelmesh drawing every baked polyobject. Room 4's box goes
    classic while it overlaps the inner sector and comes back; room 8's (3D light) stays
    classic.
  - culling: levelmesh vs levelmesh, `lm_cullmode 0` vs `1` / `2`: 0 fails.
    `lm_polybox 0` fails ticks 120–145 (room 1's box culled at its bake position).
- **HEXEN MAP01 / MAP10 / MAP27**:

  | map | polyobjects baked |
  |---|---|
  | MAP01 | 12 / 12 |
  | MAP10 | 6 / 7 (one classic by wall) |
  | MAP27 | 9 / 9 |

  - demo: fail sets identical to the post-ticket-25 run `20261016T185110Z-ab` (11 / 0 / 1;
    MAP01's are the sky, ticket 14). Run `20261016T204000Z-ab`.
  - static: MAP01 30 pass, MAP10 30 identical. MAP27 fails 30 / 30 on the same 382 px with
    `lm_polyobjs 0` (two 1-row specks, max delta 19): not polyobjects.
- **myhouse MAP01**: 77 / 169 baked (82 classic by sector, 10 by wall). Same 5 fails as
  before.
- **citadel MAP01**: 45 / 48 baked. One new failing tick (1200): a 2-px seam down the middle
  of a double door where two polyobject doors meet (DC2).

**Classic quirk noticed.** `HWWall::Process` sets `HWF_NOSPLITUPPER | HWF_NOSPLITLOWER` for
polyobject segs, then resets `flags = 0`. A polyobject line that the node builder split in its
origin container therefore gets edge splits at those (unmoved) seg vertices in the classic. The
bake forces `HWF_NOSPLIT` for polyobject walls instead. This was not seen in the test maps.

Runs:

| run | what |
|---|---|
| `20261016T203442Z-ab` | polytest A/B |
| `20261016T202938Z-ab` | cull mode 1 |
| `20261016T203111Z-ab` | cull mode 2 |
| `20261016T202735Z-ab` | `lm_polybox 0` |
| `20261016T204000Z-ab` | HEXEN demo |
| `20261016T205844Z-ab` | HEXEN static |
| `20261016T210414Z-ab` | MAP27 static, `lm_polyobjs 0` |
| `20261016T205046Z-ab` | myhouse MAP01 |
| `20261016T205346Z-ab` | citadel MAP01 |
