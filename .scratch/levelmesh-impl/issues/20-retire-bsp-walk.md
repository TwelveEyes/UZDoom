# 20: Retire the BSP walk from the levelmesh frame

**Map:** `../map.md`: Transitional strategy, Frame model (end state), Perf bars.
**Type:** task
**Status:** needs-triage (pending the 05 gate)
**Blocked by:** 11, 13, 16, 18, 19

## What to build

On levelmesh levels, `HWDrawInfo::CreateScene` (BSP walk, clipper, `HWWall` / `HWFlat` processing,
the job pool) no longer runs for **any** view type:

- main view, portal views, mirrors, skyboxes;
- camera textures (`AActor` cameras, `RenderTextureView`);
- stereo eyes;
- the OOB / ortho views from ticket 18.

Each piece that still depended on the walk is switched to its levelmesh owner:

| piece | owner |
|---|---|
| geometry | tickets 08–13 |
| sky | ticket 14 |
| portals | ticket 15 |
| translucent / hack fills / fog boundaries | ticket 16 |
| actors / decals | ticket 17 |
| exposure / dither / cullcolor | ticket 18 |
| dynamic lights | ticket 19 |

Audit `research/02-consumer-inventory.md` and anything added since for leftover consumers of
walk-produced state (`validcount` memos, `rendered_*` counters, `section` / `mapsection` gates,
`CurrentMapSections`, `PO_LinkToSubsectors` render use, particle subsector lists). Each one is
either ported or proven unused.

Afterwards, the levelmesh render-CPU window must be independent of level size.

## Acceptance

- [ ] `CreateScene` is never entered on a levelmesh level (assert or counter in `r_perflog`).
- [ ] Full ticket-02 perf protocol run (Vulkan): results vs bars recorded. If the bars are missed,
      the ticket stays open with a profile-backed analysis.
- [ ] A/B regression sweep over the DOOM2 / HEXEN / HERETIC sets: no new unclassified diffs.

## Comments
