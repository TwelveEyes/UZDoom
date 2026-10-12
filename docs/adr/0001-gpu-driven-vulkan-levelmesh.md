# ADR 0001: GPU-driven levelmesh, Vulkan only

Status: accepted (2026-10-11)

## Context

The first levelmesh design was a GL33-first static mesh. It used per-texture IBO ranges and
frustum-only culling, and evaluated the light chain per vertex. The audit
(`.scratch/levelmesh-impl/audit-2026-10-11.md`) found that this design could not beat the classic
BSP rasterizer, for three reasons:

- It gave up the clipper's occlusion culling and put nothing in its place.
- It kept the classic scene build running.
- GL 3.3 lacks the tools that make a static mesh fast: compute, SSBOs, multi-draw-indirect, and
  bindless textures.

## Decision

The levelmesh path is GPU-driven and exists only on the Vulkan backend:

- clusters baked at load;
- per-view compute culling (frustum plus two-phase Hi-Z) that writes indirect draw commands
  (`drawIndirectCount`);
- bindless materials through descriptor indexing;
- dirty-only state updates;
- a per-surface shading-resolve pass.

The GL backend keeps only the classic path. On Vulkan devices that lack the required features, the
engine falls back to the classic path.

## Consequences

- Per-frame CPU cost becomes independent of level size, and GPU cost follows the visible set.
- No GL 4.6 port is planned; GL users get no levelmesh benefit.
- Some CPU consumers of visibility read GPU results a frame or more late. ADR 0002 covers this as
  delta class DC3.
- ZVulkan feature requests may need subtree changes, which must be flagged.
