# 20: Retire the BSP walk from the levelmesh frame

**Map:** `../map.md`: Transitional strategy, Frame model (end state), Perf bars.
**Type:** task
**Status:** ready-for-agent
**Blocked by:** 11, 13, 16, 18, 19, 22

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
- 2026-10-17 (from 15): levelmesh portal views each end the scene render pass for their compute
  cull, and the classic path restarts it. On myhouse MAP01 V2 (two portal views) that costs
  ~0.8 ms `wall` p50 while the views' own GPU work is 0.15 ms. An experiment where each portal
  view only ended the render pass reproduced the cost. With portal views known before the scene
  (GPU discovery, the candidate list), cull them all in one compute batch ahead of the render
  pass, and draw each from its own buffers (`VkLevelMeshRenderer::ViewBuffers`).
