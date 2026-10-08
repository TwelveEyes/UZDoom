# Known-diff allowlists (levelmesh A/B, ADR 0004)

Frozen per-map lists of tolerated pixel-diff clusters for the A/B
comparator (`tools/abtest.py`). The policy is spec section 9 / ADR 0004:

- **Zero tolerance outside the allowlist.** Any diff pixel not covered by
  an entry fails the run.
- **The only legitimate source of a diff is z-fighting** — a structural
  consequence of the levelmesh's true-z depth (ticket 04) vs the classic
  path's normalized per-view depth range. A diff that is not a z-fight is
  a real bug: fix the engine, do not allowlist it.
- **Human-reviewed, shrink-only.** Every entry is a human-reviewed
  z-fight. Entries can only be **removed** (re-review), never added
  silently. `abtest.py record` stages clusters in
  `pending/<map>.json` — reviewing and promoting an entry to
  `<map>.json` is a manual, reasoned step, and pending files must never
  be committed.

## File format

One JSON file per map: `allowlists/<map>.json` (`<map>` = the manifest
`name`, e.g. `DOOM2_MAP01`).

```json
{
  "format": "ab-allowlist/1",
  "map": "DOOM2_MAP01",
  "frozen": true,
  "entries": [
    {
      "mode": "static",
      "tick": 32,
      "cx": 1283.5,
      "cy": 701.2,
      "radius": 14.3,
      "reason": "z-fighting: coplanar 3D-floor flats at the lift pit, classic depth-normalization winner vs true-z"
    }
  ]
}
```

Fields:

| field    | type   | meaning |
|----------|--------|---------|
| `format`  | string | must be `ab-allowlist/1` |
| `map`     | string | manifest map name (must match the file name) |
| `frozen`  | bool   | `true` for committed allowlists (shrink-only) |
| `entries[].mode`   | string | `static` or `demo` — entries apply to one capture mode only |
| `entries[].tick`   | int\|null | capture tick the entry covers (`null` = any tick in this mode; prefer an exact tick) |
| `entries[].cx/cy`  | number | circle center, pixels, top-left origin, at the pinned 2560×1440 |
| `entries[].radius` | number | circle radius, pixels |
| `entries[].reason` | string | what z-fights here and why either winner is legitimate — must state z-fighting |

**Coverage rule** (enforced by `abtest.py`): a diff cluster is covered
iff **every pixel** of the cluster lies within some entry's circle for
the same (map, mode, tick). Clusters are 4-connected components of the
per-pixel diff bitmap.

## Workflow

1. First bring-up (ticket 09): run the matrix; every failing combo's
   clusters are staged with
   `abtest.py record <map> <mode> <backend> <dirA> <dirB>`.
2. Review each staged cluster in `pending/<map>.json` (images are in the
   run dir): real bug → fix the engine; z-fight → write a concrete
   `reason`, keep the circle.
3. Promote the true z-fights to `allowlists/<map>.json` (`frozen: true`),
   delete the pending file, re-run the combo to confirm zero uncovered
   pixels.
4. After that, the allowlist is frozen: `abtest.py record` output for a
   frozen map is a regression signal, not input.

This directory starts empty of per-map JSONs on purpose: ticket 01's
determinism bar is a classic-vs-classic zero-diff run with no
allowlist in play.
