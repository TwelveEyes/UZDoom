# 04: Dynamic sector lifecycle (movement, re-bake)

**Spec:** `.scratch/levelmesh-rendering/spec.md` §4 (dynamic sector state — the 96-byte sector
record, the full per-frame copy, the Pack/Diff/Lifecycle protocol, the write rules) + §3 (dynamic
lifecycle: flag flip + re-bake to static at rest). Detail:
`.scratch/levelmesh-rendering/ticket-breakup/digest-01-04.md` (dynamic sector state) and
`digest-slices-proposal.md` (slice 04).

**What to build:** The rest of the snapshot pass — 03 shipped Pack (per-frame full copy); this
ticket ships Diff + Lifecycle. Per-frame record diff against the cached previous-frame copy: first
change marks the sector `moved`; two consecutive unchanged frames mark it `settled` (a moving
plane changes every frame at any speed, so N=2 suffices). First movement flips the dynamic flag on
every surface referencing the sector's planes and rewrites each such surface's dual-purpose vertex
slot `(baked z, baked v)` → `(vparam, 0)` via sub-upload — from then on the VS warps those
surfaces from the sector state buffer. When a surface's referenced planes have ALL settled it
re-bakes to static: baked `(z,v)` is written back from the settled final (non-interpolated)
height and the flag clears; settled-but-dynamic surfaces (a partner still moving) simply wait.
The write rules (ADR 0003 pipeline hazard): the per-frame full copy touches only the *current*
ring slot, while the rare small writes — re-bake z patches, first-movement slot rewrites,
surface-record flag or texture-index changes — go to ALL ring slots (≤ 4 small sub-uploads) so
in-flight frames never read half-updated state. Non-interpolated frames (cutscenes,
`r_NoInterpolate`) pack raw tick values, exactly as the classic path renders them. Result: doors,
lifts, movers move on the levelmesh path exactly as the classic bakes them — the snapshot pass
runs inside the interpolation window, where the tick-blended planes are already written, so
moving-geometry parity is exact by construction (ADR 0002) — and return to static after settling.
A/B GL33 on door-open / lift-mid-transit ticks (DOOM2 MAP01 doors, MAP07 lift) + demo replay on
the current subset — zero tolerance; in-game a door mid-transit is pixel-identical to classic.

**Blocked by:** 03 (the flip/re-bake writes the surface records and vertex pool that 03 draws).

**Status:** ready-for-agent

- [ ] Door-open / lift-mid-transit ticks (DOOM2 MAP01 doors, MAP07 lift) + demo replay A/B GL33 on the current subset: zero tolerance.
- [ ] Lifecycle: first movement flips the dynamic flag + rewrites slots on every referencing surface; re-bake to static at settle, only when ALL referenced planes have settled, from the settled final non-interpolated height; scroll/transdoor/alpha (load-marked) surfaces never hit the re-bake path.
- [ ] Write rules hold: per-frame full copy = current ring slot only; flip/re-bake/texture-swap sub-uploads = all ring slots.

**Note:** The all-ring-slot write rule for rare small writes is the ADR 0003 pipeline-hazard rule; the snapshot pass runs inside the interpolation window and the classic path keeps its own machinery untouched.
