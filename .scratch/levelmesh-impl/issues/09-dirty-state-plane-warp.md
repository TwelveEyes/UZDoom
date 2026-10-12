# 09: Dirty state stream + plane warp (moving sectors)

**Map:** `../map.md`: Data model (State buffers, Plane-anchored vertices), Frame model (CPU step 1).
**Type:** task
**Status:** needs-triage (pending the 05 gate)
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
