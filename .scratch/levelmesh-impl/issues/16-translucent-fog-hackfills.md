# 16: Translucent surfaces, fog boundaries, hack fills

**Map:** `../map.md`: Frame model (CPU step 3, translucent pass).
**Type:** task
**Status:** needs-triage (pending the 05 gate)
**Blocked by:** 10

## What to build

These are the small, order-dependent or view-dependent sets that stay CPU-driven:

- **Translucent level surfaces.** Translucent walls, translucent / additive flats, translucent 3D
  planes, and FF_FOG planes all come from the bake's translucent side list.
  - Each frame: frustum-test them on the CPU, then build `HWWall` / `HWFlat` entries from the
    static data (or re-run the classic emitter for just those sides). Feed them into the existing
    `GLDL_TRANSLUCENT` / `GLDL_TRANSLUCENTBORDER` lists, so `DrawSorted` sorts them with sprites
    exactly as the classic does.
  - Their vertices track planes the same way.
  - Occlusion: with visibility feedback (ticket 17), skip surfaces in clusters that were not
    visible.
- **Fog boundaries.** The classic draws fog-boundary walls between differently fogged sectors with
  the `fogboundary` effect. Bake them into a side list and draw them in the classic order.
- **Missing-texture / deep-water fills.** These are the classic `SSRF_PLANEHACK` /
  `SSRF_FLOODHACK` hacks (`hw_renderhacks.cpp`), from the bake's `mh->upper` / `mh->lower` lists.
  They depend on the view and on the walk's visibility. Decide here (a design note in Comments):
  - Option (a): run the classic hack routines for the candidate sides that are frustum- and
    feedback-visible.
  - Option (b): reproduce their effect with stencil / depth techniques on the GPU.
  - Measure how common these are on the acceptance maps first.

## Acceptance

- [ ] A map with translucent walls / flats (any Pirates! or DOOM2 MAP with translucency), a
      fog-boundary map (DOOM2 MAP27 or a Pirates! fog map), and a missing-texture map pass A/B.
- [ ] The translucent set's CPU time per frame is logged and stays small on the stress vantages.

## Comments
