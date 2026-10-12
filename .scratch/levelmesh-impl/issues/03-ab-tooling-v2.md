# 03: A/B tooling v2: tolerance + delta classes, Vulkan pairs

**Map:** `../map.md`: Acceptance policy. ADR 0002.
**Type:** task
**Status:** ready-for-agent
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

- [ ] Calibration run recorded; tolerance and cluster thresholds justified in the manifest.
- [ ] Classic vs classic (Vulkan) passes with zero clusters on the static and demo sets.
- [ ] Allowlist v2 schema enforced; promoting a candidate requires a human step.
- [ ] Game-state dump + set-diff works on DOOM2 MAP01 (classic vs classic → empty diff).
- [ ] The fullbright override is byte-identical to normal rendering when off. When on, classic vs
      classic stays identical.

## Comments
