# 09: Dirty state stream + plane warp (moving sectors)

**Map:** `../map.md`: Data model (State buffers, Plane-anchored vertices), Frame model (CPU step 1).
**Type:** task
**Status:** ready-for-agent
**Blocked by:** 08

## What to build

- **Dirty tracking for sector state** (planes, `TexZ`, light ingredients, colormap, glow, flags).
  - Inside the `D_Render` interpolation window (after `DoInterpolations`), compare each candidate
    sector's packed record with its last-uploaded copy and upload only the changes.
  - Candidates: sectors touched by interpolation (the interpolator's list), light thinkers, and
    explicit change hooks where they are cheap. Otherwise use a full compare (a full compare is
    O(sectors) of memcmp, which is acceptable only if ticket 02 / 05 numbers say so; measure it).
  - Non-interpolated frames upload raw tick values.
- **Restore after interpolation.** `RestoreInterpolations` moves planes back. The next frame's
  compare catches that, so no extra coupling is needed. Uploaded records must be the interpolated
  values used for that frame.
- **Cluster z-range refresh.** When a sector's planes change, recompute the z-ranges of its
  back-linked clusters and upload them dirty-only. The cull must stay conservative mid-frame.
  - Ticket 05 findings:
    - Upload through a host-visible update list plus **one compute scatter** dispatch. Do not use
      a `VkBufferCopy` region per record: those cost 0.16–0.30 ms of GPU time per frame on Pirates
      MAP49, where two moving planes of big sectors fan out to 508–966 clusters.
    - Expect that fan-out. With the scatter, 1796 dirty clusters cost 0.006 ms GPU and 2000
      moving planes about 0.1 ms CPU.
    - The moving planes per frame are exactly the interpolator's `DSectorPlaneInterpolation`
      list.
- **Warp everywhere.** Doors, lifts, crushers, scrollers' plane `TexZ`, transdoor (z−1 floors, from
  the bake), and sector_link-moved sectors all move through the same plane-anchored path.
- Measure the upload volume and CPU time per frame in `r_perflog`.

## Acceptance

- [ ] DOOM2 MAP01 doors and MAP07 lift, on ticks with a door open or a lift mid-transit, plus demo
      replay: A/B passes (fullbright override until ticket 10). Expected classes DC1 / DC2.
- [ ] Per-frame upload bytes logged; a static scene uploads ~0 bytes.
- [ ] No culling holes on fast movers (a scripted crusher and lift test map, or the DOOM2 MAP30
      lifts).

## Comments

- 2026-10-13 (from 06): the shader must evaluate `(d + nx*x + ny*y) * negiC + zOffset` in exactly
  the order of `LM_PlaneZ` (`hw_levelmeshbake.h`). Use no fma contraction, or bit-exactness breaks.
  - `LevelMeshSurface::planeRefs[4]` overflows on 0.1–0.4 % of surfaces (2474 on myhouse MAP01).
    Derive dirty sets from the vertex anchors (plane → clusters built at upload), not from
    `planeRefs`.
  - About 3.4k vertices round-trip within 1 ulp but not bit-exactly (myhouse MAP01 most of them).
    If A/B shows seams there, switch to a formula that is exact at rest.
