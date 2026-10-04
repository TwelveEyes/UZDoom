# Per-frame game-logic feedback: exposure flags & dither transparency

Type: grilling
Status: resolved
Blocked by: 08 (resolved)

## Question

Ticket 02's research broke the "renderer traversal is render-only" assumption. Two per-frame game-logic
contracts must be replicated by the levelmesh path (details with file:line refs in
`../research/02-consumer-inventory.md`):

1. **Exposure flags**: `ML_MAPPED` (set only at `hw_bsp.cpp:439`) and `SSECMF_DRAWN`
   (`hw_bsp.cpp:358,362,374`) are read by the automap (`am_map.cpp:2103,2240,2692,3074,3359`) and
   serialized into savegames (`p_saveg.cpp:347-440`, including a recovery path that rebuilds
   `SSECMF_DRAWN` from `ML_MAPPED`). Decide how the levelmesh path computes and writes these per frame —
   from the frustum-culled draw set, or a cheap separate pass? What exact timing relative to the game
   tick and render thread?
2. **Dither-transparency loop**: visible actors (`RenderedTargets`, `hw_bsp.cpp:55,927-934`) →
   `P_CheckSight` filter (`hw_bsp.cpp:1134-1139`) → trace-based `WALLF_DITHERTRANS_*` writes
   (`hw_drawinfo.cpp:716-837`) consumed by the next frame's wall/flat drawing. Decide the
   visible-actor-list hook (available from the per-sprite frustum pass) and where the trace loop runs.

End state: a spec-ready "per-frame game-logic contracts" section with the update sequence (exposure pass +
dither loop placement in the frame timeline) and an acceptance checklist (automap behavior, save/restore
round-trip, dither appearance parity).

## Locked decisions (grilling, 2 rounds, 2026-10-04)

- **D1 — Exposure = candidate LOS tests.** Per frame, per view, candidates = the subsectors present in
  that view's culled draw list (08). Every not-yet-exposed candidate is tested with 2.5D occlusion rays
  from the camera to static per-subsector test points; the subsector is exposed when any ray is
  unoccluded. Chosen over (a) a full occlusion pass replicating the classic clipper (exact parity,
  O(reachable lines) CPU per frame) and (b) pure frustum piggyback (cheapest, over-reveals).
- **D2 — Dither transparency = full port now** (not deferred). Keeps the full-parity lock; the runtime
  cost is gated behind the off-by-default `r_dithertransparency` cvar.
- **D3 — `cullcolor` reproduced** (not the `gl_cullcolor` fallback): the exposure pass writes
  `Level->cullcolor` per frame exactly like the classic's distance-cull branch.
- **D4 — Test points = all fan vertices.** Every distinct vertex of the subsector's seg fan (typically
  3–6), precomputed at build. Best coverage of the fan's angular extent (the classic marks when the
  subsector's full angular range is not covered by a *single* occluder; more points approximate that
  rule more closely).
- **D5 — Ray mechanism = BSP raycast.** New utility: walk the glnodes tree from the root toward the
  test point, 2D segment-vs-seg intersection at each node, stop at the first hit that is solid under
  the mode's solidness rule. O(tree depth + hits), deterministic. Chosen over reusing the classic
  line-walk trace (unbounded cost on dense line layouts).
- **D6 — `cullcolor` gate = frustum list only.** `IsDistanceCulled` is evaluated on the culled draw
  list's segs without the classic's occlusion gate. One documented minor delta (a culled line behind a
  wall can set the color); the value latches per level, so the effect is transient.

## Locked by code investigation (no decision needed)

### Classic exposure mechanics (verified 2026-10-04)

- **Clipper seeding** (`CreateScene`, `hw_drawinfo.cpp:488-496`): the standard clipper is seeded with
  the horizontal frustum's *complement* (`yaw ± FrustumAngle()`); the pitch clipper is seeded **only for
  OOB views** with the "suplex" window `pitch ± (20 + ½FOV)`, capped at 179°.
- **Standard clipper occluder feed** — non-OOB traversal only (`hw_bsp.cpp:378-429`): 1s lines and
  `WALLF_BLOCKRENDERING` unconditionally (no height check), except lines with `WALLF_DITHERTRANS_MID`;
  two-sided non-polyobject lines via `hw_CheckClip` (fake-flat height check); a same-sector 2s line with
  an invalid mid texture **early-returns** — never clipped *and never mapped* (map-exempt line).
- **OOB traversal**: no occluder ranges are ever added to the standard clipper (`hw_bsp.cpp:389,401,429`
  are all `!doOob`-gated), so within the (frustum ∩ suplex) window the angle gate passes
  unconditionally. The OOB per-subsector gate (`hw_bsp.cpp:800-868`) checks angle-visible (frustum),
  pitch-visible (suplex window), and radar-visible:
  `!r_radarclipper || LEVEL3_NOFOGOFWAR || (SSECMF_DRAWN && !deathmatch) || clipperr.SafeCheckRange(...)`.
- **Radar clipper** (`clipperr`): fed in *both* modes — 1s unconditionally, 2s non-polyobject via
  `hw_CheckClip`, with a padding angle; no `DITHERTRANS_MID` exclusion; no frustum seed. Used only as a
  gate in OOB radar mode. In **deathmatch** the already-exposed fast path is disabled (every candidate
  re-tested per frame; the mark itself stays monotonic).
- **OOB radar traversal viewpoint** = `Viewpoint.OffPos`, not `Pos` (`hw_bsp.cpp:1114-1120`).
- **Marks**: `SSECMF_DRAWN` set per passing seg (`hw_bsp.cpp:358,362,374`); `ML_MAPPED` set per line at
  `hw_bsp.cpp:439` after the range gate (skipped by the same-sector invalid-mid early return). A
  subsector whose segs are all 1s lines is marked in OOB mode only when
  `(r_radarclipper && !NOFOG) || r_distance_cull_type > 0` and the radar gate passes
  (`hw_bsp.cpp:361-363`) — never under the OOB non-radar default.
- **Secret gate**: OOB views (`!r_radarclipper`) skip undiscovered secret sectors' subsectors entirely
  (`hw_bsp.cpp:797` — no draw, no exposure) and things inside them (`hw_bsp.cpp:637`). Normal views have
  **no** secret gate; automap display of secrets is gated in shared `am_` code
  (`am_map.cpp:2336-2346`, `am_map_secrets`).
- **Ortho no-fog** (`bDoOrtho && (!r_radarclipper || NOFOG)`): `RenderOrthoNoFog`
  (`hw_bsp.cpp:1057-1075`) walks *all* subsectors whose bbox overlaps a 2D viewbox
  (`ext = 3 · offset · tan(½FOV)`) and runs the same `DoSubsector` gates — exposure follows the view
  mode as above.
- **Consumers**: automap reads both flags (`am_map.cpp:2103,2240,2692,3074,3359`); savegames serialize
  both (`p_saveg.cpp:347-440`) with a recovery path rebuilding `SSECMF_DRAWN` from `ML_MAPPED`.

### Classic dither loop (verified 2026-10-04)

- **Gate** (`hw_bsp.cpp:925`): `r_dithertransparency && doOob && RTnum < MAXDITHERACTORS &&
  mCurrentPortal == nullptr` — any OOB non-portal view (main or offscreen) runs its own loop;
  `MAXDITHERACTORS = 20` (`hw_bsp.cpp:54`); `RenderedTargets`/`RTnum` reset per `RenderBSP` call.
- **Collection** (in `DoSubsector`, per sector via the `validcount` gate): `touching_renderthings`,
  per-thing per-frame dedup (`thing->validcount`), eligibility
  `(MF3_ISMONSTER && !MF_CORPSE) || MF_MISSILE`, cap 20, **traversal order** — which in OOB mode
  (no pruning) is subsector order.
- **Tail** (`hw_bsp.cpp:1134-1140`, after the traversal): per target
  `P_CheckSight(players[consoleplayer].mo, target, 0)` — sight from the **console player's pawn**, not
  the camera — then `SetDitherTransFlags(target)`. Comment: "Can't do this inside DoSubsector() because
  both Trace() and P_CheckSight() affect 'validcount'".
- **`SetDitherTransFlags`** (`hw_drawinfo.cpp:815-858`): 3 iterations × 2 traces = 6 traces per actor
  (campos pulled back one actor radius from the target along the view line, X/Y wobble — iterations 0
  and 2 share the same offset, a classic quirk — Z at +0.25·Height and +0.75·Height),
  `TRACE_PortalRestrict`, gated by the `CurrentMapSections` mapsection bitset inside the trace callback;
  wall hit on a 2s line → tier from `HitPos.Z` vs both sides' floor/ceiling at the hit point
  (BOTTOM/MID/TOP), `dithertranscount = max(1, opposite side's ffloors count)`; 1s line → MID when
  `bf ≤ Z ≤ bc`, count 1; floor/ceiling hit → `plane_t::dithertransflag` (incl. the 3D-floor
  `FF_EXISTS | FF_RENDERPLANES`, not-`THISINSIDE` loop); the actor's own sector's 3D floors get both
  planes flagged unconditionally; ortho views use a fixed view-vector trace.
- **Consumption** (same frame): the tail runs before `RenderScene` — flags written by frame N's tail
  are consumed by frame N's own draw ("next pass" = the RenderScene pass). Clear-on-consume:
  `hw_walls.cpp:85-109` (TOP/BOTTOM cleared when the tier draws; MID decrements `dithertranscount` per
  seg draw, cleared at 0), `hw_flats.cpp:311` (`plane.dithertransflag` → `EFF_DITHERTRANS`). Flags on
  surfaces not drawn this frame persist into the next frame's draw.
- **`CurrentMapSections`**: per-view bitset sized to `NumMapSections` (`hw_drawinfo.cpp:251-281`), set
  to the viewpoint's mapsection plus portal destination mapsections (`hw_portal.cpp:528,668,808,915`).

### Classic `cullcolor` (verified 2026-10-04)

- **Writer**: the distance-cull branch of `AddLine` (`hw_bsp.cpp:378-392`):
  `r_distance_cull_type > 0 && IsDistanceCulled(seg)` → also feeds the clipper, and when
  `seg->frontsector->Colormap.FadeColor != 0` → `Level->cullcolor = FadeColor`. Never reset per frame —
  the value latches to the last visible culled line's fade color.
- **Initialization**: `gl_cullcolor` at level load / cvar change (`r_utility.cpp:174-176`), zero-fallback
  at `r_utility.cpp:1294`.
- **Consumers**: view clear color (`r_utility.cpp:1294-1295`, `SetClearColorPal`); fog color when
  `gl_distance_cull_type > 1` (`hw_setcolor.cpp:108`).

## Levelmesh spec: per-frame game-logic contracts

### Frame timeline (game thread, inside the interpolation window)

Per frame, views in classic view order (main → offscreen → portal views):

1. (05, once) Snapshot pass — sector-state ring slot.
2. (08) Sprite gather for the view → visible-sprite list.
3. (11) **Dither tail** (OOB non-portal views only): target collection + `P_CheckSight` +
   `SetDitherTransFlags` — writes flags for this frame.
4. (08 + 11) **Cull walk** → culled draw list, during which: dither fragment splitting (reads step 3's
   flags + last frame's residue), exposure marking, `cullcolor` computation.
5. (06/08) Portal view registration, occlusion-query prediction.
6. Slot handoff; the worker issues the 3D frame.

Ordering constraint: the dither tail precedes the cull walk (same-frame consumption, mirroring the
classic's tail → RenderScene order). The tail is the last playsim-mutating step of view build
(`P_CheckSight`/`Trace` touch `validcount`-keyed memos); nothing in the view after it — and nothing in
the worker — may depend on those memos.

### Exposure pass (per view)

**Candidate sets** (per view mode):

| View mode | Candidates |
|---|---|
| Normal (`!bDoOob`) | subsectors in the view's culled draw list (exact frustum) |
| OOB, non-radar (`!r_radarclipper \|\| LEVEL3_NOFOGOFWAR`) | subsectors in the culled list computed with the **suplex pitch window** (08 amendment A1: expanded vertical bounds) |
| OOB, radar (`r_radarclipper && !LEVEL3_NOFOGOFWAR && bDoOob`) | subsectors in the culled list; the radar gate also culls (08 amendment A2) |
| Ortho no-fog | subsectors in the viewbox-culled list (08 amendment A3) |

**Per candidate subsector S** (only while `!(S->flags & SSECMF_DRAWN)`):

1. OOB secret gate: `bDoOob && !r_radarclipper && S->sector` is an undiscovered secret
   (`isSecret() && wasSecret()`) → skip (no draw, no exposure — 08 amendment A4).
2. Mode test:
   - OOB non-radar: mark (piggyback) — except when S has **no seg with a sidedef** (precomputed
     `hasSidedefSegs` bit — the classic's 1s-only-subsector nuance).
   - OOB radar: radar gate = radar ray test over all fan vertices (radar solidness, origin =
     `Viewpoint.OffPos`). Pass → mark. Fail → S is also removed from this frame's draw list (radar-
     gated rendering). Deathmatch: no already-exposed fast path (every candidate tested per frame).
   - Normal: standard ray test over all fan vertices (standard solidness, origin = `Viewpoint.Pos`).
3. On mark: `S->flags |= SSECMF_DRAWN`; for each seg of S with a linedef that is **not map-exempt**
   (precomputed per seg: same-sector 2s line with an invalid mid texture):
   `linedef->flags |= ML_MAPPED`. Marks are monotonic — once exposed, never re-tested, never cleared.
   Re-testing of still-occluded candidates runs every frame (deterministic, frame-rate independent for
   a given camera path — the classic re-tests per frame too).

**BSP raycast (new levelmesh utility)**:

- Input: 2D origin, 2D test point, solidness mode (standard/radar). Walk the glnodes tree: at each
  node, determine which side the segment lies on / whether it crosses the node's split line; on a
  segment-vs-node-seg intersection, test the solidness predicate; solid → return BLOCKED, non-solid →
  continue the walk (the ray passes through, exactly as non-solid lines add no clip range in the
  classic). Track the sector on the origin side of each hit for the 2s fake-flat evaluation.
- **Solidness predicates** (classic sources, evaluated at *current* plane heights):
  - *Standard*: 1s or `WALLF_BLOCKRENDERING` → solid, unless `WALLF_DITHERTRANS_MID`; 2s: polyobject →
    never; same sector → map-exempt if the mid texture is invalid (ray passes); different sector →
    solid iff `hw_CheckClip(sidedef, currentsector, hw_FakeFlat(backsector, ...))`, unless
    `WALLF_DITHERTRANS_MID`.
  - *Radar*: 1s → always solid (no height check, no dither exclusion); 2s: polyobject → never; else
    solid iff `hw_CheckClip(...)` (the classic's radar padding angle is a range-coverage detail
    irrelevant to a ray test — documented, negligible).
- Reuses the existing `hw_CheckClip`/`hw_FakeFlat` height evaluators directly. Deterministic: static
  tree and seg order; the only per-frame inputs are the origin and the predicate heights.

**`cullcolor` (per view, in view order)**: for each seg with a sidedef in the culled list:
`r_distance_cull_type > 0 && IsDistanceCulled(seg) && frontsector->Colormap.FadeColor != 0` →
`Level->cullcolor = FadeColor`. Latching unchanged (no per-frame reset; `gl_cullcolor` initialization
unchanged). Per D6 the classic's occlusion gate is omitted (documented delta).

### Dither loop (per OOB non-portal view)

1. **Target collection** from the view's visible-sprite list (08): filter by
   `CurrentMapSections[thing->subsector->mapsection]`, the OOB secret gate (OOB `!r_radarclipper`:
   things in undiscovered secrets excluded — classic `hw_bsp.cpp:637`), eligibility
   `(MF3_ISMONSTER && !MF_CORPSE) || MF_MISSILE`, and per-frame dedup (`thing->validcount`); sort by
   `thing->subsector` index ascending (the classic's traversal order — exact, because the OOB
   traversal has no pruning); cap at 20.
2. **Per target**: `P_CheckSight(players[consoleplayer].mo, target, 0)` (keep the player-pawn
   asymmetry) → `SetDitherTransFlags(target)` — the classic method verbatim
   (`hw_drawinfo.cpp:815`): 6 traces, `TRACE_PortalRestrict`, mapsection gate, tier/count rules,
   own-sector 3D-floor flags, ortho variant.
3. **Flag storage**: the classic's playsim storage — `FLineSide::Flags`
   (`WALLF_DITHERTRANS_TOP/MID/BOTTOM`) + `FLineSide::dithertranscount`, `plane_t::dithertransflag`,
   `F3DFloor` plane flags. No new GPU state; the dither effect is a shader variant, not data.
4. **Consumption** (during the cull walk, per view): for each surface (wall tier / flat plane /
   3D-floor plane) whose flag is set and whose sub-range is in this frame's list, append a **dither
   fragment** — the same sub-range index range drawn with the `DITHERTRANS` pipeline variant
   (08 amendment A6; the existing `EFF_DITHERTRANS` / `main.fp #define DITHERTRANS` shader adapted per
   07) — then consume it: TOP/BOTTOM flags cleared; MID: `dithertranscount -= segcount(line,
   sub-range)` (precomputed, 08 amendment A5 — replicates the classic's per-seg decrement), flag
   cleared at 0; flat/3D-floor: `dithertransflag` cleared. Surfaces not in this frame's list keep
   their flags (residue — the classic's clear-on-consume semantics). Same-frame consumption holds:
   flags written by step 3 of view build are consumed by step 4 of the same frame, exactly as the
   classic's tail → RenderScene order.

### Savegame round-trip

No change: the flags live on `subsector_t`/`line_t`, the levelmesh writes the same fields, and
`p_saveg.cpp:347-440` (incl. the `SSECMF_DRAWN`-from-`ML_MAPPED` recovery path) is shared code.

**A/B tool note (for 09)**: exposure and dither state are *monotonic game state*. An A/B comparison of
frames exhibiting these effects must **reload the level per path** (or reset the flags) — alternating
paths on a live level cross-contaminates the flags.

### Amendments to other tickets

- **08 (draw organization)** — new "Amendments (post-resolution, ticket 11)" section:
  A1 suplex-pitch-window cull for OOB non-radar views (expanded vertical AABB bounds in the cull walk);
  A2 OOB radar views: per-subsector radar gate applied in the cull walk (culls *and* exposes —
  radar-occluded subsectors are not drawn, classic `hw_bsp.cpp:797` + gate block); A3 ortho no-fog
  views: viewbox AABB cull (`ext = 3·offset·tan(½FOV)`, classic `RenderOrthoNoFog`); A4 secret render
  gate (undiscovered-secret subsectors and things excluded from OOB `!r_radarclipper` views' draw list
  and sprite gather); A5 static per-line `(sub-range, index range, segCount)` table + per-seg
  map-exempt bit + per-subsector `hasSidedefSegs` bit (build-time; feeds dither fragments,
  `dithertranscount` decrement, and the exposure nuance); A6 `DITHERTRANS` pipeline-variant dimension
  in the levelmesh pipeline key set; A7 the visible-sprite list is the dither target source (already
  noted — now with the filter/order/cap rules above); A8 frame-build order per view: sprite gather →
  dither tail → cull walk (dither splitting + exposure + `cullcolor`) → slot handoff.
- **07 (lighting)**: `cullcolor` is a **per-level per-frame dynamic** fog/clear-color source (written
  by the exposure pass, latched, `gl_cullcolor`-initialized) — add to the per-level per-frame uniform
  data (alongside `levelSkyPos`); the fog-color selection rule (`hw_setcolor.cpp:108`:
  `gl_distance_cull_type > 1` → `Level->cullcolor`) must be reproduced in the levelmesh fog setup.

## Acceptance checklist (feeds 09)

1. **Exposure parity (default cvars)**: scripted camera walk; compare per-subsector `SSECMF_DRAWN` +
   per-line `ML_MAPPED` between classic and levelmesh at matched tick/camera state. Target: identical
   sets. Any diff must be classified as an approved candidate-approximation delta (a subsector whose
   full range is covered by no single occluder, but whose fan vertices are each blocked by different
   lines) or a bug. Strong proxy: savegame byte-diff of the flag sections after the same scripted walk.
2. **Automap visual**: A/B automap screenshot after the same walk (default cvars) — zero tolerance
   (the automap is the shared 2D pass; its only input here is the flags).
3. **Secrets**: map with undiscovered secrets — OOB camera (`!r_radarclipper`): secret geometry neither
   rendered nor exposed; normal view: secret exposure un-gated (classic behavior).
4. **Dither**: `r_dithertransparency 1` + OOB camera actor + map with a monster behind partial walls —
   pixel A/B of the 3D frame (zero tolerance); decay test (actor leaves view → wall returns to normal
   within `dithertranscount` frames, frame-for-frame match); crowded-scene cap-20 test.
5. **`cullcolor`**: map with fade sectors + `gl_distance_cull_type 2` — fog + clear color match
   (non-default cvar → behavioral check; the frustum-only-gate delta is documented and allowed).
6. **Save/restore**: mid-exploration save (partial fog + dither active) on the levelmesh path → exit →
   load → automap + dither state equal the classic's equivalent round trip.
7. **Radar mode** (`r_radarclipper`, dev cvar): OOB camera → radar-gated rendering + fog accumulation;
   documented delta allowed (full static occluder set vs the classic's exposure-dependent radar-clipper
   accumulation) — not zero tolerance.
8. **Ortho OOB** (`VPSF_ORTHOGRAPHIC` actor, no-fog): viewbox cull + exposure per A3.

## Provenance

- Grilling rounds 1+2 (2026-10-04): D1–D6 as listed; all other mechanics locked by the code
  investigation above (file:line verified this session; the research doc 02 under-specified the
  `cullcolor` writer, the secret render gate, the OOB clipper-seeding/pitch-window behavior, and the
  same-frame dither consumption — all corrected here).
- D1's chosen option carries two documented deltas vs the classic (fan-vertex approximation of the
  full-range-coverage rule; frustum candidates vs traversal-visited subsectors — the classic's coarse
  box pruning can skip a partially visible subsector the levelmesh exposes). Both bias toward
  over-reveal or under-reveal in corner cases only; default-cvar acceptance (checklist item 1) is the
  arbiter.
- Classic-path sources: `hw_bsp.cpp` (clipper gates, marks, dither collection/tail, ortho no-fog,
  `IsDistanceCulled`), `hw_drawinfo.cpp` (clipper seeding, `SetDitherTransFlags`, trace callback,
  `CurrentMapSections`), `hw_walls.cpp:85-109` / `hw_flats.cpp:311` (dither consumption),
  `hw_setcolor.cpp:108` / `r_utility.cpp:174-176,1294-1295` (`cullcolor`), `am_map.cpp` +
  `p_saveg.cpp:347-440` (consumers).
