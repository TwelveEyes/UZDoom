# Handoff B-fix — sector state record re-laid to the locked spec layout

Supersedes the record layout in `handoff-b-state.md`. The FLevelMeshState class,
the ring, the 12-byte light record, Init, and the accessors are unchanged; only
`LevelMeshSectorState` was re-laid and PackSnapshot now fills the previously
missing fields (scroll, glow, flags, raw-ingredient light).

## New 96-byte record layout (LevelMeshSectorState)

Exact match of spec section 4 "Sector state record (96 bytes)":

| offset | type   | field           | C++ source |
|---|---|---|---|
| 0  | float[4] | floorPlane | `sec.floorplane.Normal()` + `fD()` → (nx, ny, nz, d) |
| 16 | float[4] | ceilPlane  | `sec.ceilingplane.Normal()` + `fD()` → (nx, ny, nz, d) |
| 32 | float[2] | floorScroll | `sec.GetXOffset(sector_t::floor)`, `sec.GetYOffset(sector_t::floor)` |
| 40 | float[2] | ceilScroll  | `sec.GetXOffset(sector_t::ceiling)`, `sec.GetYOffset(sector_t::ceiling)` |
| 48 | uint16  | lightlevel    | raw `sec->lightlevel` (0..255) |
| 50 | int16[2] | planeLight   | `sec.GetPlaneLight(floor/ceiling)` (secplane Light offset) |
| 54 | uint16  | flags         | bit0 = 0 (transdoor, ticket 05); bit1 = `GetFlags(floor) & PLANEF_ABSLIGHTING`; bit2 = `GetFlags(ceiling) & PLANEF_ABSLIGHTING` |
| 56 | uint32  | glowFloorColor | raw `sec->planes[floor].GlowColor` (PalEntry, ABGR); sentinels 0 = texture-glow fallback, ~0u = disabled preserved by raw store |
| 60 | float   | glowFloorHeight | `sec->planes[floor].GlowHeight` |
| 64 | uint32  | glowCeilColor  | raw `sec->planes[ceiling].GlowColor` |
| 68 | float   | glowCeilHeight  | `sec->planes[ceiling].GlowHeight` |
| 72 | uint8[9]| colormap      | `LM_PackColormap(sec.Colormap)` (unchanged from chunk B) |
| 81 | uint8[3]| reserved      | 0 |
| 84 | float[2]| reserved (C++ name `reservedF`) | 0 (05's skyOffset slot, reserved per 07 amendment) |
| 92 | uint32  | reserved (C++ name `reservedU32`) | 0 |

Total 96 bytes. The spec's three `reserved` words are named `reserved`,
`reservedF`, `reservedU32` in C++ (C++ forbids duplicate member names).

## C++ source per field (file:line)

- `secplane_t` plane: `src/gamedata/r_defs.h:671` (`floorplane, ceilingplane`),
  `Normal()`/`fD()` on `secplane_t` (`src/gamedata/r_defs.h:288`).
- Plane data struct `splane`: `src/gamedata/r_defs.h:651-666` — `xform` (653),
  `Flags` (654), `Light` (655), `GlowColor` PalEntry (658), `GlowHeight` float (659).
- `sec->lightlevel`: `src/gamedata/r_defs.h:694` (`short`).
- `GetXOffset` / `GetYOffset`: `src/gamedata/r_defs.h:835` / `850` (const).
- `GetFlags`: `src/gamedata/r_defs.h:915`; `GetPlaneLight`: `src/gamedata/r_defs.h:932`.
- `PLANEF_ABSLIGHTING = 1`: `src/gamedata/r_defs.h:470`.
- `FColormap`: `sec.Colormap` (`src/gamedata/r_defs.h:682`); pack helper
  `LM_PackColormap` in `src/common/rendering/levelmesh_state.cpp` (unchanged).
- `PalEntry`: `src/common/utility/palentry.h:37`, implicit
  `operator uint32_t` at line 42 — raw store into the uint32 field.
- Scroll source of truth (classic HW path): `HWSectorPlane::GetFromSector`,
  `src/rendering/hwrenderer/scene/hw_drawstructs.h:95-105` — it reads exactly
  `GetXOffset(pos)` / `GetYOffset(pos)` (y includes `baseyOffs` via the
  `addbase=true` default) and feeds the flat texture matrix
  (`hw_SetPlaneTextureRotation`, `src/rendering/hwrenderer/scene/hw_flats.cpp:49`).
- Runtime scroll driver: `DScroller::Tick` → `sector->AddXOffset/AddYOffset`,
  `src/playsim/mapthinkers/a_scroll.cpp:176-192`; interpolation blends the
  offsets inside the window (`DSectorScrollInterpolation`,
  `src/r_data/r_interpolate.cpp:585-614`), so reading the live xform inside the
  interpolation window is the single current CPU-interpolated value (ADR 0002).
- Consistency: the levelmesh builder's load-dynamic scroll detection already
  keys off `xform.xOffs/yOffs` (`src/common/rendering/levelmesh.cpp:389`).

## Ambiguity notes (per task constraint: store sensible value, note here)

1. **Scroll source.** The task offered texture `RowOffset()/ColOffset()` or the
   sector scroll. The spec (section 4, update protocol step 1) says
   "scroll (`planes[].xform`)", and the classic HW path's own
   `HWSectorPlane::GetFromSector` reads `GetXOffset/GetYOffset` (y includes
   `baseyOffs`). `FGameTexture::RowOffset` is the wall/FTexCoordInfo path and
   the texture-manager animation offset, not the sector's live scroll. Stored
   the sector xform offsets: `xOffs`, `yOffs + baseyOffs` (world units; the VS
   divides by texture display size, same as `hw_SetPlaneTextureRotation`).
   Texture absence is irrelevant to this source.
2. **Glow accessors are non-const.** `sector_t::GetGlowColor/GetGlowHeight`
   (`src/gamedata/r_defs.h:942-950`) lack `const`, so PackSnapshot reads
   `sec.planes[pos].GlowColor/GlowHeight` directly (public `splane` members).
3. `lightlevel` is `short` in C++; stored as uint16 per spec (values are 0..255).

## static_assert result

`src/common/rendering/levelmesh_state.h`:
- `static_assert(sizeof(LevelMeshSectorState) == 96)` — passes.
- 14 `offsetof` static_asserts pinning every spec offset (16, 32, 40, 48, 50,
  54, 56, 60, 64, 68, 72, 81, 84, 92) — all pass.
- No `#pragma pack` needed: the natural layout already lands every member at
  its spec offset (the first 4-byte member after byte 47 sits at 56).
- `static_assert(sizeof(LevelMeshLightState) == 12)` unchanged, still passes.

## Build result

`cmake --build build --config RelWithDebInfo --parallel 3` →
`[100%] Built target zdoom`, exit 0. Force-recompiled
`levelmesh_state.cpp` after deleting its object file: zero warnings, zero
errors from the modified TU. The only warning in a full rebuild is the
pre-existing baseline `src/playsim/a_dynlight.h:266` in unrelated TUs.
(One pre-existing `-Wsign-compare` in PackSnapshot,
`level.sectors.Size() != sectorCount`, was fixed as part of this change.)

## Files changed

- `src/common/rendering/levelmesh_state.h` — record re-laid + offset table +
  offsetof asserts.
- `src/common/rendering/levelmesh_state.cpp` — PackSnapshot fills all spec
  fields; sign-compare fix.
- Ring, Init, accessors, `LevelMeshLightState`, and
  `FLevelMesh::Build`'s call in `levelmesh.cpp` untouched.
