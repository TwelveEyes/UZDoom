# 25: Polyobject origin subsectors baked as map-wide flats (citadel z-fighting)

**Map:** `../map.md`: Data model (ticket 06 bake).
**Type:** task
**Status:** resolved
**Blocked by:** —

## Question

citadel.pk3 MAP01 (Doom 2 mod, UDMF, 4048 sectors, 48 polyobjects, 1242 3D floors) shows
heavy z-fighting on floors and ceilings with the levelmesh. Is it levelmesh-only, and where
does it come from?

Demo: `tools/abtest/demos/v2/CITADEL_MAP01.lmp` (tour, 2600 tics; `make_demo.py` reads the
map wad nested in the pk3); manifest entry `CITADEL_MAP01`.

## Answer

Levelmesh-only, and not from 3D floors. The bake triangulated the flats of **polyobject
origin subsectors** (`SSECF_POLYORG`), which the classic never renders
(`HWDrawInfo::DoSubsector`: "their vertices no longer are where one may expect"). When the
polyobjects spawn, `PO_Init` moves their vertices to the start spots. The origin subsectors
share those vertices, so their `CreateVerticesForSubsector` fans stretch across the map. In
citadel MAP01, sector 751 (floor `CEIL5_1`, ceiling `SLIME15`, z 0 / 128) has subsectors
whose bounds reach from (-896, 1238) to (3440, -2376). Its flats lay over every 0 / 128
room in that area, z-fighting with the real floors and ceilings or covering them, including
a floor pit.

Bisection (A/B, Vulkan, demo mode):

| B pins | failing ticks of 87 |
|---|---|
| — | 42 |
| `lm_3dfloors 0` | 42 (same fail set) |
| `lm_draw 0` | 0 (87 identical: classic deterministic, the diff comes from the levelmesh draw) |
| `--fullbright` | 42 (geometry, not lighting) |

Probe: a temporary `lm_flatsat x y` console command listed the baked flat surfaces under a map
point. At the tour stops, it found sector 751 / 812 surfaces far from those sectors.

Fix (`hw_levelmeshbake.cpp`):

- `ProcessFlats` skips `SSECF_POLYORG` subsectors, including their portal, translucent and
  3D-plane entries.
- The side loop skips sides whose segs are all in such subsectors.
- `lm_bakestats` reports `poly origin: sides N, partial sides N, subsectors N`. A partial
  side, with some segs in an origin subsector, is still baked whole.

Results, failing ticks of 87 before → after:

| map | poly origin (sides / partial / subsectors) | lit | fullbright |
|---|---|---|---|
| CITADEL_MAP01 | 16 / 0 / 163 | 42 → 4 | 42 → 4 |
| HEXEN_MAP01 | 2 / 2 / 27 | 12 → 11 | |
| HEXEN_MAP10 | 0 / 1 / 28 | 12 → 0 | |
| HEXEN_MAP27 | 36 / 0 / 36 | 25 → 1 | |
| MYHOUSE_MAP01 | 148 / 25 / 788 | 12 → 5 | |

No tick fails only after the fix (`ab_sheet.py failsets`). The other manifest maps have no
origin subsectors.

- Runs before: `20261016T190337Z-ab` (HEXEN) and `20261016T191211Z-ab` (MYHOUSE_MAP01).
- Runs after: `20261016T185110Z-ab` and `20261016T185931Z-ab`. Citadel:
  `20261016T184104Z-ab` (fullbright) and `20261016T184442Z-ab` (lit).

What remains on citadel MAP01: ticks 1410 / 1440 / 1470 / 1710, at most 295 px each. These
are 1-px vertical seams at wall edges, edge specks of the DC2 kind (`ab_sheet.py sheet
20261016T184104Z-ab CITADEL 1470`).

Ticket 24's ticks 420 / 450 / 480, where a ceiling was drawn across the top of the view,
were this bug. Its sky ticks 930 / 960 remain.
