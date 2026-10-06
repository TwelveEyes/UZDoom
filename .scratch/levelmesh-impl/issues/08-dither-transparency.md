# 08: Dither transparency: full port

**Spec:** `.scratch/levelmesh-rendering/spec.md` §8 (dither transparency — the FULL port,
amendments A5–A8). Detail: `.scratch/levelmesh-rendering/ticket-breakup/digest-07-08.md`
(dither target collection, the classic tail, flag storage, cull-walk consumption, the A5
line→sub-range table, the A8 frame-build order) + `digest-slices-proposal.md` (slice 08).

**What to build:** The dither-transparency loop ported in FULL (user-chosen), gated by the
off-by-default `r_dithertransparency` cvar. Per OOB non-portal view (gate: the cvar && OOB &&
target count under the cap && no active portal view): (1) TARGET COLLECTION from the view's
visible-sprite list (03's sprite-gather output): filter by mapsection + the A4 OOB secret gate
(things inside undiscovered secrets excluded), eligibility = monster-not-corpse or missile
(`(MF3_ISMONSTER && !MF_CORPSE) || MF_MISSILE`), per-frame dedup, sorted by subsector index
ascending (= the classic traversal order — exact, because the OOB traversal has no pruning),
cap 20. (2) Then the CLASSIC TAIL VERBATIM: per target, the sight check from the CONSOLE
PLAYER'S PAWN (keep the player-pawn asymmetry — sight from the pawn, not the camera) → the
classic flag-setting method (its trace set, portal-restricted trace, mapsection gate, tier/
count rules, own-sector 3D-floor flags). (3) Flag storage = the classic's playsim storage — the
line-side dither flags + per-line decrement counter, the flat plane flag, the 3D-floor plane
flags — NO new GPU state; the dither effect is a shader variant, not data. (4) CONSUMPTION
during the cull walk, per view: for each surface (wall tier / flat plane / 3D-floor plane) whose
flag is set and whose sub-range is in this frame's list, append a dither fragment — the SAME
sub-range index range re-drawn with the `DITHERTRANS` pipeline variant (A6 — the dither bit
added to the levelmesh pipeline key; the existing `#define DITHERTRANS` fragment adapted per 03's
promoted-uniform variant) — then CLEAR-ON-CONSUME: top/bottom flags cleared; mid: the per-line
decrement counter reduced by the line's per-sub-range seg-count (A5's static table), flag
cleared at 0; flat / 3D-floor flags cleared. UNSEEN SURFACES KEEP RESIDUE — surfaces not in this
frame's list keep their flags (the classic's clear-on-consume semantics). Same-frame
consumption: flags written by the tail (frame build) are consumed by the cull walk of the SAME
frame — the classic's tail → render order. A5: the static line→sub-range table built at BUILD
time — per line (and per flat plane / 3D-floor plane): the sub-ranges containing it, the index
range per sub-range, the per-line segCount within each sub-range (the decrement unit), the
per-seg map-exempt bit (2s same-sector line with an invalid mid texture), the per-subsector
`hasSidedefSegs` bit. A8: the frame-build order is sprite gather → dither tail → cull walk →
slot handoff; the tail is the LAST playsim-mutating step of the view build, and nothing after
it (the same-frame walk, the worker) may depend on the validcount-keyed trace-memo state the
sight check touches. A/B: the dither 3D frame (cvar on, OOB camera, monsters behind partial
walls) — zero tolerance; decay + cap-20 + save/restore checks (reload per path — dither state
is monotonic game state).

**Blocked by:** 07 (the gate requires OOB views + the cull walk with the visible-sprite list
and the A5 line→sub-range table).

**Status:** ready-for-agent

- [ ] Dither 3D-frame A/B zero-tolerance: `r_dithertransparency 1`, OOB camera, monsters behind partial walls (reload per path).
- [ ] Decay test: an actor leaves view → the affected wall returns to normal within its decrement-count frames, frame-for-frame match with the classic.
- [ ] Crowded-scene cap-20 test (the target cap is the classic's 20).
- [ ] Save/restore round trip with dither active (mid-exploration save, partial fog + dither) equals the classic's equivalent round trip.
- [ ] A5 table built at build time: per-line (and per flat/3D-floor plane) sub-range index ranges + per-sub-range segCounts + per-seg map-exempt bits + per-subsector `hasSidedefSegs`.
- [ ] The `DITHERTRANS` pipeline-variant dimension is in the levelmesh pipeline key; dither fragments re-draw the same sub-range index ranges under the adapted fragment; clear-on-consume decrements by the per-sub-range seg-count; unseen surfaces keep residue.
- [ ] A8 order holds: sprite gather → dither tail → cull walk → slot handoff; the tail before the walk, same-frame consumption, nothing after the tail depends on the validcount-keyed trace-memo state it touches.
- [ ] cvar 0 = classic, unchanged (the cvar is off by default).

**Note:**
1. Parity is exact by construction on the classic side (the tail + flag storage are reused verbatim); the levelmesh-specific surface is the dither-fragment consumption in the cull walk.
