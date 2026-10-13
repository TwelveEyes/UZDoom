# 03: A/B tooling v2: tolerance + delta classes, Vulkan pairs

**Map:** `../map.md`: Acceptance policy. ADR 0002.
**Type:** task
**Status:** resolved
**Blocked by:** 01

## What to build

Move `tools/abtest.py` from zero tolerance to the delta-class policy:

- **Pairing.** The default is Vulkan classic vs Vulkan levelmesh; GL33 pairs are dropped from the
  levelmesh matrix. Keep fresh-level-load-per-path runs and the existing capture mode / synthetic
  clock.
- **Comparator.**
  - Per-pixel channel tolerance and minimum cluster size. Pick the values from a calibration run of
    classic vs classic across driver restarts, plus a known-good classic vs classic run with
    `vid_preferbackend` toggled. Record both in the manifest.
  - Report surviving clusters with bbox, pixel count, max delta, and a crop + 4× diff image.
- **Allowlist v2 schema.** `{map, mode, tick, class (DC1..DC5), rect, centroid, radius, reason,
  reviewer, date}`. The tool refuses unknown classes. `record` writes candidates to a review file,
  never straight into the allowlist; a human promotes them.
- **Game-state diff mode.** Dump per-subsector `SSECMF_DRAWN` and per-line `ML_MAPPED` at chosen
  ticks (console command or capture hook), then set-diff the two paths. Ticket 18 uses this.
- **Debug fullbright override.** Add an engine cvar (name decided here) that forces uniform
  full-bright shading identically on both paths. Ticket 08 uses it to test geometry and visibility
  before ticket 10 lands lighting. It must be a no-op when off and must not change classic output.
- Update `tools/abtest/README.md` and `allowlists/README.md`.

## Acceptance

- [x] Calibration run recorded; tolerance and cluster thresholds justified in the manifest.
- [x] Classic vs classic (Vulkan) passes with zero clusters on the static and demo sets.
- [x] Allowlist v2 schema enforced; promoting a candidate requires a human step.
- [x] Game-state dump + set-diff works on DOOM2 MAP01 (classic vs classic → empty diff).
- [x] The fullbright override is byte-identical to normal rendering when off. When on, classic vs
      classic stays identical.

## Comments

- 2026-10-13 (resolved):
  - **Engine (two cvars, both `CVAR_NOSAVE`, off by default):**
    - `r_ab_fullbright` (`hw_drawinfo.cpp` `SetFullbrightFlags`): every view is a plain
      fullbright scene (white light, no fog, no dynamic lights, no player special colormap).
      Portal drawinfos inherit it. The levelmesh path (ticket 08) must honour it.
    - `r_ab_gamestate` (`d_abcapture.cpp`): each captured tick also writes
      `<map>_<tick>.gstate` (header + hex bitsets of `SSECMF_DRAWN` per subsector and
      `ML_MAPPED` per line).
  - **`tools/abtest.py` rewritten (v2):**
    - Vulkan-only A/B by default (manifest `ab_backends`); tour demos (`demos/v2`) for demo
      mode.
    - Comparator: max-channel tolerance + minimum cluster size; failing clusters are reported
      with bbox / px / max delta and a crop image (A | B | 4× diff) in `<run>/<combo>/report/`.
    - Allowlist `ab-allowlist/2` with classes DC1–DC5; unknown classes are rejected; `pixels`
      and `gstate` kinds.
    - `record` stages UNREVIEWED candidates in `pending/` (gitignored). `promote --reviewer`
      moves only entries a human gave a class + reason.
    - New `calibrate`, `statediff`, `promote`; `--gamestate`, `--fullbright`.
    - Manifest is `ab-manifest/2`.
  - **Calibration** (recorded in `ab_manifest.json` → `tolerance`):
    - Classic Vulkan vs Vulkan across 3 launches: byte-identical (static 30/30, demo 87/87).
    - Classic GL33 vs Vulkan: static max delta 24, single-px clusters above delta 8; demo max
      delta 208, 1-px column clusters up to 1004 px.
    - Chosen: `channel 2`, `min_cluster 4`. This is a tight floor for levelmesh rasterization
      noise; the cross-backend numbers are an upper bound that would hide real bugs.
  - **Found and fixed: demo-mode non-determinism.**
    - The tour demos' tic-0 cheat notify lines tick via `C_Ticker`, which also runs for the
      real-time startup tics before capture pacing applies, so their presence at tics 30–90
      varied per launch (685-px clusters).
    - Fix: every A/B run pins `show_messages 0`.
  - **Acceptance evidence:**
    - `run --determinism --gamestate` (Vulkan): static 30/30 and demo 87/87 identical; game
      state identical (191 subsectors / 319 lines at tic 2599).
    - Self-test with synthetic edits:
      - a 1271-px cluster fails and is cropped;
      - a 1-px speck plus a +2-shade pixel pass on tolerance;
      - a dropped subsector shows in `statediff`;
      - `record` → `promote` refuses before review and promotes after it;
      - an allowlist then covers both diffs;
      - class `DC9` is rejected.
    - Fullbright off: 30/30 static ticks byte-identical to a capture from the pre-change
      engine (the two engine files reverted, incremental rebuild).
    - Fullbright on: classic vs classic static 30/30 and demo 87/87 identical; visually
      uniform full light.
  - **Notes:**
    - `--runs` is now made absolute (the engine chdirs to `build/`).
    - Game-state DC5 entries cover listed indices; ticket 18 may want a richer
      classification.
