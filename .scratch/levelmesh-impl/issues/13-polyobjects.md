# 13: Polyobjects

**Map:** `../map.md`: Data model (State buffers: polyobject transforms).
**Type:** task
**Status:** ready-for-agent
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

- [ ] HEXEN MAP01 / MAP10 / MAP27 (12 / 7 / 9 active polyobjects): A/B static + demo passes, only
      DC1 / DC2.
- [ ] Moving doors and rotators show no pop-in or culling holes in the demo A/B.

## Comments
- 2026-10-14 (from 09): cluster xy boxes are still static. The 09 refresh only rewrites
  `bmin.z / bmax.z`, and `scatter.comp` only writes those two words per cluster. A polyobject
  that moves needs an xy refresh, or clusters of its own.
