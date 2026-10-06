# 07: Exposure pass + OOB/ortho views + cullcolor

**Spec:** `.scratch/levelmesh-rendering/spec.md` §8 (per-frame game-logic feedback — exposure
pass, BSP raycast, cullcolor latch, offscreen-view cull rules A1–A4). Detail:
`.scratch/levelmesh-rendering/ticket-breakup/digest-07-08.md` (the exposure pass, the glnodes
BSP raycast + standard/radar solidness, the cullcolor latch, A1–A4, acceptance checks 1–3 + 5
+ 7/8) + `digest-slices-proposal.md` (slice 07).

**What to build:** The per-frame per-view CANDIDATE LOS pass reproducing the classic automap
fog (the levelmesh replacement for the classic's piggyback marking during the BSP walk): per
frame, per view, in classic view order (main → offscreen → portal views), candidates from the
view's culled draw list are ray-tested from the camera to STATIC fan-vertex test points (all
distinct vertices of the subsector's seg fan, precomputed at build — the levelmesh's static data
already carries them); marks are MONOTONIC — `SSECMF_DRAWN` on the subsector, `ML_MAPPED` on
each non-map-exempt seg (2s same-sector line with an invalid mid texture) — feeding the shared
automap + savegame code UNCHANGED; once marked a candidate is never re-tested or cleared;
still-occluded candidates are re-tested every frame (deterministic, frame-rate independent for
a given camera path). Mode behavior: normal views = standard ray test (standard solidness,
origin = viewpoint pos); OOB non-radar = PIGGYBACK mark (no ray test) EXCEPT subsectors with no
seg having a sidedef (precomputed `hasSidedefSegs` bit — the classic's 1s-only-subsector
nuance); OOB radar = the radar gate ray-tests all fan vertices (radar solidness, origin = the
off position) and a failing subsector is NOT DRAWN either — the gate culls the draw, not just
exposure — with deathmatch re-testing every subsector per frame (no already-exposed fast path).
The ray tests use the NEW BSP raycast utility over the glnodes tree: a segment-vs-node-seg walk
(which side of the split / segment-vs-node-seg intersection), with the solidness predicates —
standard or radar — evaluated at CURRENT plane heights via the existing clip-check / fake-flat
evaluators (polyobject lines never solid; the standard mode's dither exclusion and the radar
mode's no-height-check rules included); deterministic — static tree + origin + predicate
heights, O(tree depth + hits). Offscreen-view rendering on the levelmesh path with the classic
view-mode cull rules: A1 the SUPLEX PITCH WINDOW for OOB non-radar views (pitch ±(20 + ½FOV),
capped at 179°) — the expanded bounds apply to the culled list ITSELF, since that list IS the
exposure candidate set (an exact-frustum list would under-expose); A2 the OOB radar gate
(above); A3 the ortho no-fog VIEWBOX CULL (subsector bbox vs the 2D viewbox instead of the
frustum); A4 the undiscovered-secret gate for draw + exposure (OOB non-radar views exclude
undiscovered-secret sectors' subsectors from the culled list and things inside them from the
visible-sprite list; normal views are un-gated — the automap's secret DISPLAY gating is shared
code). The per-frame per-view `Level->cullcolor` latch from the culled list's segs, in view
order: a seg with a sidedef that passes the distance cull and whose frontsector fade color is
nonzero → latches that fade color; latching semantics unchanged (NO per-frame reset,
`gl_cullcolor` initialization), consumed by the fog/clear-color rules in 03's VS (view clear
color; fog color when `r_distance_cull_type > 1`). The latch's gate is FRUSTUM-ONLY — the
classic's occlusion gate is OMITTED: one documented delta (a culled line behind a wall can set
the color; the value latches per level, so the effect is transient). A/B: flag-array + savegame
byte-diff parity on a scripted camera walk (RELOAD PER PATH — exposure is monotonic game state;
a live session cannot host both paths); automap screenshot A/B after the same walk — zero
tolerance (shared 2D pass); cullcolor check on a fade map with `r_distance_cull_type 2`;
radar/ortho behavioral checks — the documented static-occluder delta is allowed, NOT zero
tolerance.

**Blocked by:** 04 (needs the full snapshot pass + the main draw; OOB views are a second view
class on the same frame build).

**Status:** ready-for-agent

- [ ] `SSECMF_DRAWN` / `ML_MAPPED` flag sets identical between classic and levelmesh after a scripted camera walk (reload per path); any diff classified as the approved fan-vertex candidate-approximation delta (a subsector whose full range is covered by no single occluder, but whose fan vertices are each blocked by different lines) or a bug.
- [ ] Savegame flag sections byte-identical after the same scripted walk.
- [ ] Automap screenshot A/B zero-tolerance after the same walk (the automap is the shared 2D pass; its only input here is the flags).
- [ ] Secrets neither drawn nor exposed in OOB non-radar views with undiscovered secrets; un-gated in normal views.
- [ ] Cullcolor latch semantics (no per-frame reset, `gl_cullcolor` init) + fog/clear-color consumers match the classic (fade map, `r_distance_cull_type 2`: fog + view clear color match; the frustum-only gate is the documented delta).
- [ ] BSP raycast deterministic — same origin + predicate heights → same result across runs (static tree, static seg order).
- [ ] Offscreen cull rules hold: the suplex pitch window applies to the culled list itself; the OOB radar gate culls the draw too (deathmatch re-tests all); ortho no-fog viewbox cull.
- [ ] Radar / ortho behavioral per spec §8 acceptance 7/8 — documented static-occluder delta allowed, NOT zero tolerance.
- [ ] cvar 0 = classic, unchanged.

**Note:**
1. The BSP raycast is a small independent deterministic utility (static tree, origin, predicate heights) — it can be verified in isolation before the exposure pass is wired up.
