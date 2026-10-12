# 05: Spike: GPU-driven culling go/no-go

**Map:** `../map.md`: Transitional strategy (the go/no-go gate), Frame model.
**Type:** prototype
**Status:** ready-for-agent
**Blocked by:** 02, 04

## Question

On the ticket-02 stress vantages, does GPU-driven cluster culling plus indirect drawing of the
static world beat the classic path by enough to justify tickets 06–21?

## What to build (throwaway, on a spike branch; do not merge)

- **Geometry.** Take the classic `DoomLevelMesh` geometry (positions + elements + surface ids). Cut
  it into spatial clusters by BSP subtree, with a triangle budget, and build 3D AABBs from the
  load-time heights.
- **Vulkan compute.** Frustum cull plus two-phase Hi-Z (phase 1: last frame's visible clusters;
  phase 2: the rest against the new Hi-Z). Write `vkCmdDrawIndexedIndirectCount` commands.
- **Shading.** A minimal shader: per-surface flat color, or one bindless texture lookup if
  descriptor indexing is easy to enable. Lighting does not matter here; cost does.
- **Mesh-only mode.** A cvar that skips `CreateScene` entirely and draws only the spike mesh, so the
  scene-build cost is removed.
- **Measurements.** At each stress vantage, compare against classic (Vulkan):
  - CPU render window;
  - GPU time per phase (timestamp queries: cull 1, draw 1, Hi-Z, cull 2, draw 2);
  - triangles drawn;
  - cluster counts.

  Sweep the cluster budget (64 / 128 / 256 / 512 triangles). Also report frustum-only, to show
  what Hi-Z buys.

## Deliverable

`../research/05-spike-results.md` with tables, plus a **go / no-go / go-with-changes**
recommendation against the ticket-02 bars, including:

- the projected end-state frame cost;
- the cluster budget to use;
- whether two-phase Hi-Z is needed;
- any surprises (MSAA depth for Hi-Z, render-pass splits in `VkRenderState`, ZVulkan feature
  plumbing).

The user decides. A no-go stops tickets 06+ for a re-plan.

## Acceptance

- [ ] Numbers for every stress vantage and the four cluster budgets.
- [ ] Clear recommendation; user decision recorded in `../map.md` Decisions log.
- [ ] Tickets 06–21 updated (triaged to `ready-for-agent`, or changed) according to the findings.

## Comments
