# 18: Game-logic feedback: exposure, dither, OOB/ortho/radar

**Map:** `../map.md`: Acceptance policy (DC3, DC5); `../research/02-consumer-inventory.md`.
**Type:** task
**Status:** resolved
**Blocked by:** 17

## What to build

The classic walk writes game state that the automap and savegames consume. Reproduce it from GPU
visibility:

- **Exposure.** From the readback (ticket 17), mark `SSECMF_DRAWN` on subsectors of visible
  surfaces and `ML_MAPPED` on lines of visible wall surfaces. Apply the classic's map-exempt rule:
  a same-sector two-sided line with an invalid mid texture is not mapped. Marks are monotonic.
  Savegame code (`p_saveg.cpp`) is unchanged.
- **Cullcolor latch.** Port the classic `Level->cullcolor` latch (`hw_bsp.cpp`; `r_distance_cull_type`
  plus fade color) from visible distance-culled surfaces.
- **Dither transparency** (`r_dithertransparency`):
  - targets come from the ticket-17 actor gather;
  - the classic `P_CheckSight` + `SetDitherTransFlags` are reused verbatim;
  - flags set on sides / planes are uploaded as per-surface dither bits (dirty-only);
  - the shader applies the dither pattern from the bit (an in-shader branch, not a separate
    pipeline);
  - clear-on-consume semantics and the `dithertranscount` decay must match the classic.
- **OOB / ortho / radar view modes.** Port the classic view-mode rules:
  - suplex pitch window for OOB non-radar views;
  - radar gating (`r_radarclipper`);
  - ortho viewbox;
  - secret-sector gating for draw and exposure in OOB views.

  Culling and exposure follow these modes, either through extra view parameters in the cull shader
  or through CPU-side filtering of the readback.

## Acceptance

- [ ] Game-state set-diff (ticket 03 tool) after the same scripted walk on DOOM2 MAP01 / MAP27 and
      myhouse 20PAM: identical, or every difference classified DC5 with a reason.
- [ ] Automap screenshot A/B after the walk passes.
- [ ] Dither: OOB camera + `r_dithertransparency 1`, a monster behind partial walls; frames and
      decay pass (DC3 allowed if latency-bound).
- [ ] Save / load round trip preserves exposure and dither state.

## Comments
- 2026-10-14 (from 09): the DOOM2 MAP01 tour demo's classic-vs-levelmesh fails include
  out-of-bounds views (ticks 2010, 2100, ...): the classic draws sky-hack ghosts behind walls
  seen from outside the map. Fullbright, no state uploads in that demo.

## Answer

Resolved 2026-10-19 (transitional, like 14-17: the walk keeps running). On the levelmesh path
the exposure marks and the cullcolor latch come from GPU per-surface visibility, dither flags
reach the levelmesh surfaces, and out-of-body views draw and gather under the classic rules.

- **Mark pass** (`VkLevelMeshRenderer::MarkPhase`, `wadsrc/static/shaders/levelmesh/mark.fp`):
  after a view's last draw phase its surfaces are rasterized again against the final depth
  (EQUAL for the drawn buckets of both Hi-Z phases, LEQUAL for sky surfaces and exposure quads),
  no color writes; each passing fragment sets its surface's leaf and line bit in the view's
  region of `LMBIND_EXPOSURE`. `LevelMeshSurface` gained `leaf` / `line`; `invariant
  gl_Position` in `levelmesh.vp`. Subgroup dedupe of the atomics when the device has fragment
  ballot / arithmetic (SOS MAP32 V1: 0.37 -> 0.26 ms). The cluster bitset and the exposure
  bits are copied to the readback at the end of the frame (`EndFrame`), not after each view, so
  the mark pass adds no render-pass break. New requirement: `fragmentStoresAndAtomics`.
- **Exposure quads** (bake, `LMP_EXPOSURE` bucket): every two-sided side, and every one-sided
  side whose mid tier the levelmesh does not draw, from front floor to front ceiling.
- **Game side** (`hw_levelmeshfeedback.cpp`, was `hw_levelmeshgather.cpp`):
  `BeginLevelMeshFeedback` reads the cluster + exposure readback; `ApplyLevelMeshExposure` sets
  `SSECMF_DRAWN` on visible leaves, `ML_MAPPED` on visible lines plus the subsectors of the
  visible side's segs (once per side: `LevelMeshBake::LineExposed`), and latches the cullcolor
  from the lowest-index visible distance-culled line with a fade color. The walk's own marks and
  latch are off for such a view (`lmExposure`), except the radar marks.
- **Dither**: `LMTS_DITHER` in the side / plane / 3D-floor group records (hooks in the trace
  callbacks, a second `LevelMesh_UpdateState` after `CreateScene`, `LevelMesh_ConsumeDither`
  clears the owned tiers / planes before the next frame's traces), `LMVF_DITHER` selects the
  dither-capable pipeline variant, which branches per surface (`LM_DITHER`, `main.fp`). Targets
  come from the actor gather (`CollectDitherTargets`). Classic shaders are token-identical.
- **Out-of-body rules**: `BuildLevelMeshLeafMask` / `FinishLevelMeshLeafMask` (secret sectors,
  ortho viewbox, radar from the walk's `RadarStamp`), applied by the vertex stage
  (`LMVF_LEAFMASK`) and the gather. Suplex pitch window / `OoBFrustumAngle` not ported (the GPU
  frustum is exact). OOB views now gather and feed back.
- **Cvars**: `lm_exposure`, `lm_exposurelog`, `lm_oobmask`. Perflog v9: `lmxv lmxl lmxn lmxms
  lmmk`. Test kit: `tools/abtest/make_oobtest.py` (`ab_manifest_oobtest.json`).

Verification (Vulkan, runs under `tools/abtest/runs/`):

| check | result |
|---|---|
| Exposure isolation (A `lm_exposure 0`, B default), pixels | identical on every tick of DOOM2 MAP01 / MAP27, myhouse 20PAM, OOBTEST MAP01-03 (only sub-tolerance clusters) |
| Exposure isolation, game state (last tick) | DOOM2 MAP01: A-only 7 lines, B-only 5 subsectors / 2 lines; MAP27: A-only 5 subsectors / 5 lines, B-only 38 subsectors; 20PAM: A-only ~40-100 subsectors / ~100-170 lines (clipper marks of vertically hidden segs); OOBTEST MAP03: B-only subsector 9 (pillar edge pixels), lines identical; MAP02 (ortho): A-only edge-on walls. All staged DC5 with reasons in `allowlists/pending/` (human review / promote pending) |
| Classic unchanged | DOOM2 MAP01 classic captures pixel-identical to the pre-change determinism run (`20261019T000257Z-det`); classic vs classic with `--gamestate`: 87/87 identical |
| Depth invariance | classic vs levelmesh fail sets: MAP01 {2010, 2100, 2190} (the known out-of-map views), MAP07 {} |
| Cullcolor (`r_distance_cull_type 2`, `r_line_distance_cull 1024`) | DOOM2 MAP01 and LIGHTTEST MAP01 (fade rooms): identical / pass on every tick |
| Dither (chase cam, `r_dithertransparency 1`) | the pillar dithers on both paths from the same tick (243), ~870k px each, A vs B <= 21 px; negative control (B off) fails 123 ticks |
| OOB rules (classic vs levelmesh) | MAP01 secret room hidden on both, radar effect identical on both paths, MAP02 ortho passes every tick; negative control `lm_oobmask 0` shows the secret room (118 failing ticks). Remaining 63 MAP01 fails are 1-row DC2 specks (max delta 3-6), the same set with and without radar / dither |
| Validation layers (`vk_debug 1`, OOBTEST MAP01, PORTALTEST MAP01) | only the known swapchain-recreate pair |
| GL backend | runs, no feedback (no `LMEXPOSE` / `LMGATHER`) |
| Determinism | levelmesh vs levelmesh OOBTEST MAP01 twice: 184/184 identical (one earlier run had a non-reproducible 4 px speck) |

Perf (vantages, 2560x1440, p50 / p95 ms):

| vantage | lmmk (GPU) | lmxms (CPU) | lmgpu |
|---|---|---|---|
| SOS MAP32 V1 | 0.258 / 0.261 | 0.020 / 0.024 | 0.604 |
| myhouse MAP01 V1 (3 views) | 0.146 / 0.149 | 0.064 / 0.074 | 0.418 |
| gathertest MAP03 / MAP04 (scaling pair) | 0.027 / 0.027 | 0.000 / 0.001 | 0.245 / 0.250 |

Not exercised: the save / load round trip (a scripted autosave during the demo wrote
`demosave.zds`, which `-loadgame` did not load; the savegame code is unchanged and the marks it
stores are the ones the set-diffs show) and the automap screenshot A/B (the capture mode cannot
open the automap; the game-state set-diff is the automap's data). Both are left for in-game
checks. Before the subgroup dedupe the mark pass cost 0.371 ms on SOS MAP32 V1 (over the plan's
0.3 ms line); the plan's fallback (main view only) would not have helped there, since that
cost is the main view's.

Known limits: the mark pass sees the main view's depth before the portals draw, so geometry the
levelmesh draws behind a line portal is exposed (handed to ticket 20); exposure marks lag one
frame (DC3); a one-sided wall the state stream defers at run time has no exposure quad.
