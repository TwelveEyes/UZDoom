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
- 2026-10-16 (from 12): Pirates MAP58 demo (run `20261016T071521Z-ab`): ticks 150 / 360 / 960 /
  990 / 1050 / ... (10k-380k px, delta up to 202) show far islands / ships the levelmesh
  draws where the classic shows only sky (`ab_sheet.py sheet 20261016T071521Z-ab
  PIRATES_MAP58 150 960 360`). Identical with `lm_3dfloors 0`, so not 3D floors. Same
  pattern as the regions above: the classic leaves geometry out (clipper / sky) and the
  levelmesh draws it, or the reverse.
- 2026-10-17 (from 14): with the levelmesh drawing the sky portals' geometry, the remaining
  sky-region diffs on DOOM2 MAP01 (ticks 2010–2190, camera outside the map), SOS MAP12 (ticks
  1410–1500) and Pirates MAP43 tick 120 (dark blocks above the cliffs) show geometry the classic
  leaves out where its sky shows. No sky wall covers them, so it is this ticket's pattern, not a
  sky rule.
