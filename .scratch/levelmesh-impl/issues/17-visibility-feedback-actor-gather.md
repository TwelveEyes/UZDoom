# 17: Visibility feedback + actor/decal gather

**Map:** `../map.md`: Frame model (GPU step 7; CPU step 3), Acceptance policy DC3;
`../research/02-consumer-inventory.md`.
**Type:** task
**Status:** needs-triage (pending the 05 gate)
**Blocked by:** 15

## What to build

Replace the BSP walk's role as the source of "what is visible" for CPU consumers:

- **Readback ring.** Each view's per-cluster visibility bitset (and per-surface bits where
  consumers need them) is copied to a host-visible ring of `HW_MAX_PIPELINE_BUFFERS` slots and read
  when its fence signals. Latency = the pipeline depth. Document the exact frame latency (feeds
  DC3).
- **Actor gather.** This replaces `RenderThings` / `RenderParticles` being called per visited
  subsector.
  - Per view, walk the actors (and particles) of subsectors in **visible-or-adjacent** clusters
    from the newest available readback, intersected with the current frustum.
  - Feed survivors into the existing `HWSprite` / model paths, unchanged. The `GLDL_*` lists and
    `DrawSorted` stay as they are.
  - Over-inclusion is harmless (z-buffered). Under-inclusion produces a DC3 pop-in on newly revealed
    areas. Including adjacent clusters (the cluster adjacency graph from the bake) must keep this
    bounded; measure it.
- **Decal gather.** Decals of visible sides (same source) go to the existing decal draw.
- **Dynamic-light list inputs** for sprites and models stay classic (ticket 19 covers walls / flats).
- **Shared order.** Portal views (ticket 15) use the same gather with their own bitsets.

## Acceptance

- [ ] Demo A/B with monsters on DOOM2 MAP01 / MAP23 and a slaughter map: actor pixels pass, apart
      from DC3 clusters. The DC3 count and its duration in frames are measured and reported.
- [ ] Gather CPU time scales with actors in view, not with level size (stress vantages).

## Comments
