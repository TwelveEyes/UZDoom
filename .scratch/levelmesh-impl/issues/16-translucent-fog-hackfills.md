# 16: Translucent surfaces, fog boundaries, hack fills

**Map:** `../map.md`: Frame model (CPU step 3, translucent pass).
**Type:** task
**Status:** resolved
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

- [x] A map with translucent walls / flats (any Pirates! or DOOM2 MAP with translucency), a
      fog-boundary map (DOOM2 MAP27 or a Pirates! fog map), and a missing-texture map pass A/B.
- [ ] The translucent set's CPU time per frame is logged and stays small on the stress vantages.

## Comments

- 2026-10-13 (from 06): the inputs are `LevelMeshBake::{TranslucentWalls, FogWalls, UpperMissing,
  LowerMissing, TranslucentFlats}`, with counts per map in `tools/abtest/results/lm_bake_survey.md`.
  Heightsec surfaces are baked unfaked and flagged `LMSF_HEIGHTSEC`. The classic fake-flat
  depends on the view area, so they need the classic path or a per-view variant.

## Answer

**Scope moved to ticket 20 (user decision, 2026-10-17).** In the transitional state the classic
walk still emits the translucent walls / flats, fog boundaries and hack fills, and the levelmesh
leaves them in the draw lists (`RemoveLevelMeshOwned` only drops owned opaque tiers / planes).
They already match the classic. Producing them from the bake changes nothing visible until the
walk is gone, so that work moves to ticket 20 (see its comment). This ticket delivered the test
kit, a bug fix it exposed, and the findings below.

**Test kit:** `make_transtest.py` (commit `dc3c3a3455`), 9 rooms × 2 maps (default lightmode,
lightmode 2); see the abtest README. Translucent / `FF_FOG` 3D floors are in the 3D-floor map.

**Bug fixed** (commit `564e2500f7`): a mid texture made translucent at run time
(`TranslucentLine`) stayed opaque in the levelmesh while the classic also drew it translucent
(up to 640k px). The tier base records the line's alpha and `ML_ADDTRANS` at bake. A different
value makes the tier inexpressible (`LMTS_DEFERRED`, back to the classic), and `TranslucentLine`
reports the change. ZScript can write `Line.alpha` directly without a hook, so the state
stream's sweep catches that within its period.

**Results**, classic against levelmesh:
- **transtest** MAP01 / MAP02 fail 12 / 15 of 171 ticks: specks of at most 182 px (one at
  delta 36) in the fake-plane, portal-floor and deep-water rooms (run `20261017T171549Z-ab`).
- **3D-floor map** (translucent / fog 3D floors): unchanged; those stay classic.
- **Hexen**, after the fix:
  - MAP01 fails 11 of 87. No side switches to the classic during the demo (`lm_statelog 1`),
    so these predate the fix (ticket 23's black regions among them).
  - MAP10 fails 1, MAP27 fails 1.
  - One MAP10 run had an incomplete capture on the levelmesh side; the rerun was complete.

**Findings for ticket 20:**
- **Translucent set.** The bake's `TranslucentWalls` already holds fog boundaries:
  `HWWall::DoMidTexture` puts them through `PutWall(di, true)`, so the mesh helper files them as
  translucent, and `LevelMeshBake::FogWalls` is never filled (0 on every surveyed map).
  - Re-running the classic emitter for the frustum-visible candidate segs, with a dispatcher
    that keeps only translucent output, would reproduce `GLDL_TRANSLUCENT` exactly, including
    dynamic lights and `ViewDistance`.
  - Watch-outs:
    - `PutWall` runs `ProcessDecals` for solid walls, so a translucent-only dispatcher must
      return before it.
    - Candidates must follow run-time changes (line alpha / `ML_ADDTRANS`, mid texture
      translucency): the side dirty hooks plus the sweep.
    - Heightsec sides need the view area (`hw_FakeFlat`).
- **Translucent flats.** The classic makes them only for portal planes with alpha < 1,
  reflective planes (alpha = 1 − reflect) and translucent 3D planes, per section.
- **Hack fills.** They come in four kinds, all classic and all walk-dependent (the walk's seen
  subsectors, `validcount` neighbour searches):
  - plane hacks (`HandleMissingTextures`: `DoOneSectorLower` / `Upper`, `DoFakeBridge` /
    `DoFakeCeilingBridge`);
  - flood gaps (`PrepareUnhandledMissingTextures`: stencil + projected plane, view-height
    gated);
  - open-sector deep water (`HandleHackedSubsectors`, `hacked` subsectors);
  - self-referencing sectors, which are not a hack routine: `render_sector` makes them
    ordinary flats.

  Bake counts per map are in `results/lm_bake_survey.md` (`upper_missing` / `lower_missing`):
  - 0–21 on the DOOM2 / Hexen / Heretic maps;
  - up to 1200 on SOS MAP32 and 305 on Pirates MAP58;
  - 84 on myhouse MAP01.

  The candidates are static (missing textures at bake plus texture changes). Recommended
  option (a): run the classic routines for the frustum-visible candidates, with the walk's
  seen-flags replaced by visibility feedback (ticket 17). Measure the per-frame count on SOS
  MAP32 first; option (b), GPU stencil fills, only if that is too slow.
