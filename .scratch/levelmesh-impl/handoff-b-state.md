# Handoff B — sector state ring + snapshot pack

Subagent timed out after completing the work (was chasing a pre-existing DAP
warning); parent verified the full state below and wrote this handoff.

## Files changed
- `src/common/rendering/levelmesh_state.h/.cpp` — new, backend-neutral.
- `src/CMakeLists.txt` — PCH_SOURCES entry for levelmesh_state.cpp.
- `src/common/rendering/levelmesh.h` — `#include "levelmesh_state.h"`,
  `FLevelMesh::state` member.
- `src/common/rendering/levelmesh.cpp` — `FLevelMesh::Build` calls
  `mesh.state.Init(lvl)` (spec §3 build order step 5).

## 96-byte sector state record (LevelMeshSectorState, no padding gaps)
| offset | type | field | source |
|---|---|---|---|
| 0 | float[3] | floorNormal | sec->floorplane.Normal() |
| 12 | float | floorD | sec->floorplane.fD() |
| 16 | float[3] | ceilNormal | sec->ceilingplane.Normal() |
| 28 | float | ceilD | sec->ceilingplane.fD() |
| 32 | float | floorTexZ | GetPlaneTexZ(floor) — tick-blended inside the interpolation window (ADR 0002) |
| 36 | float | ceilTexZ | GetPlaneTexZ(ceiling) |
| 40 | int32 | lightlevel | sec->lightlevel |
| 44 | int32[2] | planeLight | [0] floor / [1] ceiling secplane Light offset |
| 52 | uint8[9] | colormap | FColormap pack: LightColor(3) BlendFactor(1) Desaturation(1) FadeColor(3) FogDensity(1, low byte) |
| 61 | uint8[35] | reserved | zero; scroll/glow/flags slots reserved for later slices |

## 12-byte 3D light record (LevelMeshLightState, packed)
| offset | type | field | source |
|---|---|---|---|
| 0 | int16 | lightlevel | *lightlist->p_lightlevel |
| 2 | uint8[9] | colormap | lightlist->extra_colormap pack |
| 11 | uint8 | reserved | 0 |

Built from every sector's `e->XFloor.lightlist`, skipping entry 0
(lightsource == NULL = sector's own ceiling light = surface lightlistRef -1),
deduplicated by F3DFloor in sector-index order, then list order. Deterministic
per game state; light indices are NOT stable across frames — the frame build
must resolve light references in the same frame it packs.

## API
- `FLevelMeshState::Init(FLevelLocals&)` — sizes the ring
  (HW_MAX_PIPELINE_BUFFERS × sectorCount × 96), initial PackSnapshot into slot 0.
- `LevelMeshPackedState PackSnapshot(FLevelLocals&, int slot)` — full copy of
  every sector's record into `slot` + rebuild of the 3D light buffer; returns
  {sectorState, sectorSize, lightState, lightSize} for the frame build to stream.
- Accessors: GetSectorSlot(slot), SectorSlotSize(), GetLightData(), LightDataSize(), LightCount().
- Owned by `FLevelMesh::state` (one per level, lives with the mesh).

## ADR 0003 compliance
Full copy, sector-index order, no pointers, reserved bytes zeroed, no
uninitialized floats → byte-identical across runs for identical game state.

## Build
`cmake --build build --config RelWithDebInfo --parallel 3` → `[100%] Built
target zdoom`, zero warnings from the modified/new files (remaining DAP
format warnings are pre-existing baseline).
