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
- 2026-10-17 (from 16, scope moved here by user decision): produce the translucent walls /
  flats, fog boundaries and missing-texture fills without the walk. Today the walk emits them
  and the levelmesh leaves them in the lists. Findings, the recommended approach, the fill
  kinds and bake counts are in ticket 16's Answer. The test kit is `make_transtest.py`
  (`ab_manifest_transtest.json`); its fill rooms only show when the classic's conditions hold,
  so A/B them after the change. Log the translucent set's CPU time per frame on the stress
  vantages (ticket 16's second acceptance item).
- 2026-10-18 (from 17, decal scope moved here by user decision): actors and particles come from
  the GPU visibility readback (`HWDrawInfo::GatherLevelMeshThings`, one frame late), but the
  walk still produces the `HWWall`s that carry decals. Gather decals of visible sides from the
  same bitset once the walk is gone. The gather runs after `RenderBSPNode` and applies the
  walk's subsector rejections (`CurrentMapSections`, polyobject origin, clip portal); without
  the walk it needs its own place in the frame and a fallback for views without a readback
  (today: the walk gathers). Ticket 17's perf table shows the cluster-level gather processing
  2-3x the walk's sprites in myhouse interiors.
- 2026-10-18 (from 18): the OOB radar rule (`r_radarclipper`) is the one view-mode rule still
  sourced from the walk: `DoSubsector` stamps the radar-accepted subsectors
  (`LevelMeshBake::RadarStamp`) that the leaf mask keeps (`HWDrawInfo::FinishLevelMeshLeafMask`),
  and `AddLine` keeps the radar `SSECMF_DRAWN` marks. Retiring the walk needs a standalone radar
  pass: a clipper-only BSP traversal from `Viewpoint.OffPos` (the `rClipper` logic of `AddLine`)
  producing the stamp set and the radar marks. Secret gating and the ortho viewbox are
  walk-independent (`BuildLevelMeshLeafMask`). The dither targets come from the actor gather
  (`CollectDitherTargets`), the exposure marks and cullcolor latch from the mark pass
  (`ApplyLevelMeshExposure`); both need the frame placement the gather gets here. The mark pass
  sees the main view's depth before the portals draw, so geometry the levelmesh draws behind a
  line portal (covered later by the portal's stencil) is exposed too; with the walk gone the
  levelmesh will have to stop at portal lines anyway.
