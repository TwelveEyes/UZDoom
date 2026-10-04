# Portal & sector_link rendering in the levelmesh world

Type: grilling
Status: open
Blocked by: 03, 04

## Question

Lock how the levelmesh path renders through portals at full parity (classic-path behavior documented in
ticket 03's findings):

- Region model: how the level partitions into renderable regions — one mesh per portal-connected component,
  per sector_link target, or something else?
- Per-portal view-transform handling on the GPU (target-region transform in the shader/descriptor state vs
  separate draws).
- Fake flats and sky through portals.
- 3D floors inside portal targets.
- Recursion/re-entry limits and draw order/batching across regions.

End state: a spec-ready section with the region model and the per-frame portal render sequence.
