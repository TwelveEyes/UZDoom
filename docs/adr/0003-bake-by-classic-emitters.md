# ADR 0003: Bake geometry by running the classic emitters

Status: accepted (2026-10-11)

## Context

The first levelmesh builder reimplemented `HWWall::Process`, `DoTexture`, `DoMidTexture`,
`SetWallCoordinates`, the gl_seamless splits, and subsector triangulation, in about 1600 lines. Most
of its bugs came from copying that logic imperfectly, and every future classic fix would have had to
be made twice.

## Decision

The levelmesh bake calls the classic code at map load:

- **Walls:** `HWWall::Process` runs through `HWWallDispatcher`'s mesh-helper mode (`HWMeshHelper`,
  `src/rendering/hwrenderer/scene/hw_walldispatcher.h`).
- **Flats:** the classic static flat vertex builder (`hw_vertexbuilder.cpp`) produces them.

The bake converts the captured vertices into plane-anchored form, `z = ZatPoint(anchor plane) +
offset`. Every surface tracks its planes at all times; there is no static/dynamic lifecycle.

Small changes to classic code for the capture are allowed: hooks, flags, and exposing data. Copies of
classic logic are not.

## Consequences

- Geometry parity holds by construction. One code path keeps the wall and flat logic.
- Classic processing that depends on the view has to be identified. Those outputs go into side
  lists instead of the mesh (ticket 06).
- Load time includes running the classic processing once per seg and subsector. That cost is
  linear.
