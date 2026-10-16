# 23: Regions the classic leaves black where the levelmesh draws geometry

**Map:** `../map.md`: Acceptance policy.
**Type:** research
**Status:** needs-triage
**Blocked by:** —

## Question

Demo A/B (classic vs levelmesh, Vulkan, both with and without `--fullbright`) shows large
regions the classic renders black (nothing drawn) where the levelmesh draws plausible
geometry:

- movers DOOM2 MAP30 ticks 300 / 305 / 360 / 365 (stairs room);
- SOS MAP46 ticks 210 / 660 / 2280 (vertical strips);
- Hexen MAP01 ticks 270 / 300; Hexen MAP10 ticks 30 / 1770;
- SOS MAP32 ticks 120 / 2040.

`lm_stateupdate 0` changes nothing, and the same ticks fail on the pre-11 binary, so it is not
runtime texture state. Is it the classic clipper / visibility missing geometry (a classic bug the
levelmesh need not reproduce: allowlist under DC5 or a new class), or the levelmesh drawing
geometry the classic correctly hides (a levelmesh bug)? Inspect with `tools/abtest/ab_sheet.py
sheet RUN COMBO TICK` and the game-state set-diff (`--gamestate`).
