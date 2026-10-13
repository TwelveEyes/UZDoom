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

## Provenance (2026-10-12)

The project takes no code or architecture from VKDoom. `HWMeshHelper` and the mesh-helper mode are
GZDoom code: Christoph Oelckers, `9cd57faec`, on GZDoom master. UZDoom's copy is identical to
GZDoom's. VKDoom also baked through classic emitters, but it re-baked surfaces on every change and
extended `HWMeshHelper` in its own tree. This ADR bakes once, moves planes on the GPU, and takes
flats from the static vertex builder. The user reviewed the overlap and kept this ADR.
See `.scratch/levelmesh-impl/research/04-prior-art-vkdoom.md`.
