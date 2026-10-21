# Known-diff allowlists (levelmesh A/B, ADR 0002)

Per-map lists of reviewed, tolerated differences between the classic and levelmesh paths for
`tools/abtest.py`. Policy: `docs/adr/0002-delta-class-acceptance.md`.

## What passes without an entry

`ab_manifest.json` → `tolerance` (calibration recorded there):

- a pixel counts as different only if its max per-channel delta exceeds `channel` (2);
- differing pixels form 4-connected clusters, and clusters smaller than `min_cluster` (4 px)
  are within tolerance.

Classic vs classic on one backend is byte-identical, so the tolerance only absorbs levelmesh
rasterization noise. Every larger cluster, and every game-state difference, needs an entry here.

## Delta classes

| class | meaning |
|---|---|
| DC1 | z-fight: coplanar/near-coplanar surfaces, either winner legitimate |
| DC2 | float-z rounding: plane-anchored GPU z vs classic CPU z at sub-pixel edges |
| DC3 | feedback latency: CPU consumers of GPU visibility lag ≤ the readback depth |
| DC4 | dynamic-light selection / evaluation (ticket 19): (a) float summation order of a surface's light list (slot order) vs the classic per-surface list (pointer order); (b) additive-light color on sprites / particles (the record's `0.2`-scaled color `× 5.0`: ULP); (c) sprite shadow test: nearest shadowmap texel vs the classic CPU ray test, shadowmaps on only |
| DC5 | visibility precision: exact depth visibility vs the clipper, for game-state sets |

A new class needs an ADR 0002 amendment and a matching change to `CLASSES` in `abtest.py`.
The tool rejects any other class.

## File format (`<map>.json`, `ab-allowlist/2`)

```json
{"format": "ab-allowlist/2", "map": "DOOM2_MAP01", "entries": [
 {"kind": "pixels", "mode": "static", "tick": 96, "class": "DC1",
  "rect": [600, 600, 640, 630], "cx": 620.0, "cy": 615.0, "radius": 25.0,
  "reason": "floor/3D-plane z-fight at the lift edge", "reviewer": "gene", "date": "2026-10-13"},
 {"kind": "gstate", "mode": "demo", "tick": 2599, "class": "DC5",
  "subsectors": [63], "lines": [],
  "reason": "...", "reviewer": "gene", "date": "2026-10-13"}]}
```

- `tick: null` matches every tick of the mode.
- A pixel cluster is covered if its bbox lies inside `rect`, or every pixel lies inside the
  circle `(cx, cy, radius)`.
- `gstate` entries cover the listed subsector / line indices whose exposure flags differ
  (`--gamestate` runs). They must be DC5.

## Workflow (the human step is mandatory)

1. **Run.** `abtest.py run --only MAP` fails and writes crops to `<run>/<combo>/report/`, each
   showing A | B | 4× diff.
2. **Fix or stage.** A real bug gets fixed in the engine. Otherwise stage the candidates:
   `abtest.py record MAP MODE <run>/<combo>/a <run>/<combo>/b [--gamestate]`. This writes
   `pending/<map>.json` with class `UNREVIEWED` and reason `PENDING ...`. It never touches the
   allowlist.
3. **Review.** A human edits each pending entry: set a class and a concrete reason, or delete
   the entry.
4. **Promote.** `abtest.py promote MAP --reviewer NAME` moves only the reviewed entries,
   stamping the reviewer and date; unreviewed ones stay pending.
5. **Commit.** Commit `<map>.json`. `pending/` is gitignored.

`abtest.py validate` checks the manifest and every allowlist (format, classes, required fields,
no `PENDING` reasons).
