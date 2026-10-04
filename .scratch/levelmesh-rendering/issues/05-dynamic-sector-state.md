# Dynamic sector state mechanism

Type: grilling
Status: resolved
Blocked by: 01, 04

## Question

Lock the per-sector dynamic-state mechanism (direction already locked at charting: per-sector state buffer +
vertex-shader warp; Doom topology is static, so no runtime re-triangulation):

- Exactly what per-sector dynamic attributes exist: floor/ceiling planes, light levels, colormap/glow/pulse/
  scroll state, runtime texture changes.
- Buffer layout and streaming: full per-frame upload vs dirty-only updates; single dynamic buffer vs
  multiple.
- How the renderer learns which sectors changed: dirty-sector list maintained by `p_spec`? version counters
  on `sector_t`? Where in the existing code do sector changes already get detected?
- Timing vs the game tick and the render thread: when updates are computed, when they reach the GPU.
- Participation of 3D floor models and sector_link target sectors.
- Confirm the exact mechanism against Helion's approach (findings in ticket 01).

End state: a spec-ready section with buffer layouts and the update protocol.

## Answer

**Resolved 2026-10-03 (grilling, 4 decisions confirmed with the user).** The per-sector state mechanism is
locked: a **sector state buffer** of one 96-byte record per sector, **full-copied every frame** into a ring
of `HW_MAX_PIPELINE_BUFFERS` slots (2 desktop / 4 Android, `buffers.h:32`), carrying **single current
CPU-interpolated** values; the **snapshot pass** (per-frame, after `DoInterpolations`) both streams the
buffer and drives the 04 static↔dynamic lifecycle by per-frame parameter diffing. Helion's lifecycle and
per-sector GPU buffer concept are kept; Helion's dirty-only streaming, prev/current GPU mix, and dynamic
VBO are deliberately **not** adopted.

### Decisions (confirmed)

1. **Streaming: full per-frame copy** of all sector records into a persistent ring buffer; the VS selects
   the frame slot by uniform. No dirty tracking for streaming (flicker lights dirty many sectors per tick
   anyway; ~50k sectors × 96B ≈ 4.5 MB/frame is negligible; byte-identical frames help the A/B tool).
2. **Record contents: the full record** — tilted planes, scroll, per-plane light, colormap, glow,
   transdoor, sky offset (layout below). Runtime texture changes stay out: the mutable texture index
   lives in the 04 surface record.
3. **Change detection: snapshot-pass parameter diff** (classic `vboheight` mechanism, `hw_vertexbuilder.cpp:443`,
   applied to records). No playsim hooks. Scroll sectors are marked dynamic **at load** from their sector
   special (Helion's `StaticDataApplier` pattern) and never hit the re-bake path.
4. **Helion parity: single current value.** UZDoom's `DoInterpolations(I_GetTimeFrac())`
   (`d_main.cpp:585`, `r_interpolate.cpp:470`) already writes the tick-blended plane into `sector_t` before
   the renderer runs, and the classic path bakes exactly those values per frame — so the levelmesh warps
   the same numbers the classic path renders. A/B pixel parity on moving geometry is exact by construction.

### Locked by code investigation (no decision needed)

- **Frame flow:** no async render thread in the HW path (`r_thread.h` is software-renderer residue).
  `D_Display` → `D_Render(action, true)`: per level `DoInterpolations(I_GetTimeFrac())` →
  `RenderView` → `RestoreInterpolations()` (`d_main.cpp:573-593`). The snapshot pass runs where the
  classic `CheckUpdate` runs — inside frame setup, after interpolation.
- **Existing machinery the levelmesh replaces:** per-plane `vboheight` compare + sub-upload
  (`hw_vertexbuilder.cpp:443`), `vertex_t::dirty` → `RecalcVertexHeights` (`hw_walls.cpp:2189`), and
  fresh-per-frame reads of light/colormap/scroll. All three collapse into the snapshot pass.
- **3D floors, fake flats, sector_link: no special mechanism.** Every sector owns a record; a surface's
  plane refs (04) point at its own or its `heightsec` **model sector**; the VS samples the *referenced*
  sector's record — that is how fake-flat surfaces and 3D-floor walls follow the model. Model sectors
  and sector_link targets (`p_linkedsectors.cpp`) are ordinary sectors moved by ordinary thinkers.
  `P_RecalculateAttached3DFloors` already runs inside interpolation, before the snapshot. Fake-flat model
  sectors must be same-level (classic path constraint).
- **Multi-level:** each `FLevelLocals` gets its own buffer + snapshot (the `AllLevels()` loop already does
  this for interpolation).

### Spec-ready: sector state record (96 bytes, 4-byte aligned, SSBO)

```c
struct SectorState {                 // 96 bytes
	float	floorPlane[4];    //  0: tilted floor plane (nx,ny,nz,d) — classic secplane_t
	float	ceilPlane[4];     // 16: tilted ceiling plane
	float	floorScroll[2];   // 32: floor UV offset (applied in VS)
	float	ceilScroll[2];    // 40: ceiling UV offset
	uint16	light[3];         // 48: [0] floor, [1] ceiling, [2] wall — per-plane effective light
	uint16	flags;            // 54: bit0 transdoor (VS applies the classic z-1 floor offset); rest reserved
	uint32	colormap;         // 56: selfmap (fog/pulse/damage), classic semantics
	uint32	glowFloorColor;   // 60: packed PalEntry (ABGR)
	float	glowFloorHeight;  // 64
	uint32	glowCeilColor;    // 68
	float	glowCeilHeight;   // 72
	float	skyOffset[2];      // 76: current sky scroll (R_UpdateSky), same value in every sector
	uint32	reserved[2];      // 84: zeroed growth headroom
};
```

- Tilted planes (not bare z) so the VS warp matches the classic per-vertex `ZatPoint` derivation exactly,
  including slanted flats. `GetPlaneTexZ` values during the frame are the interpolated ones — the record
  never carries prev/current.
- `light[]` carries the per-plane effective light level the classic flat/wall code consumes (exact
  effective-light formula — lightsec adjustment, clamping — is ticket 07's; the *slot and per-frame write*
  are locked here). A surface reads the light of the sector its plane refs point at (model sector for
  fake flats), matching Helion's transfer-light sector semantics.

### Spec-ready: update protocol

**Load.** Build the CPU record array (`num_sectors × 96B`) and the persistent ring
(`HW_MAX_PIPELINE_BUFFERS` copies; Vulkan mapped memory / GL persistent-mapped — backend mechanics are
08's). Mark load-dynamic sectors: scroll specials (floor/ceiling), transdoor/alpha flats → their surfaces
get the dynamic flag in the surface record. Fake-flat surfaces referencing a potentially-moving model get
the dynamic flag at load too (their plane refs track the model sector).

**Per frame** (levelmesh frame setup, after `DoInterpolations`, inside `D_Render`'s action):
1. **Pack.** Iterate `Level->sectors`; read planes (`floorplane`/`ceilingplane`), scroll (`planes[].xform`),
   light, `selfmap`, glow, transdoor, sky offset → write the current ring slot (full copy).
2. **Diff.** Compare each record's plane+scroll words against the cached previous-frame copy.
   First change → sector `moved` this frame; two consecutive unchanged frames → sector `settled`.
   (A moving plane changes every frame at any speed, so N=2 is sufficient.)
3. **Lifecycle** (per 04): a *static* surface whose plane refs include a `moved` sector → flip its dynamic
   flag in the surface record (and rewrite its vertices' dual-purpose slot: `(baked z, baked v)` →
   `(vparam, 0)`). A *dynamic* surface whose referenced sectors are all `settled` → **re-bake**: write the
   current planes' z back into its vertex-pool range, restore the dual-purpose slot, clear the flag.

**Write rules (pipeline hazard):** the per-frame full copy touches only the *current* slot — that is the
ring's purpose. Rare small writes (re-bake z patches, first-movement slot rewrites, surface-record flag or
texture-index changes) are applied to **all** ring slots (≤ 4 small sub-uploads) so in-flight frames never
read half-updated state. `RestoreInterpolations()` runs after frame setup; nothing further is coupled to it.
Non-interpolated frames (cutscenes, `r_NoInterpolate`) pack raw tick values, exactly as the classic path renders them.

**GPU side.** For a dynamic surface the VS: `SectorState s = state[slot][surface.planeRef.sector]`;
`z = ZatPoint(s.plane, vertex.xy)` (plus the transdoor z-1 offset when flagged); `uv += s.scroll`; light/
colormap/glow come from `s` (shading model: 07). Static surfaces never touch the buffer. The per-frame
slot index is a single uniform.

### Helion alignment (vs. `research/01-helion-levelmesh.md`)

| Aspect | Helion | Levelmesh (locked) |
|---|---|---|
| Per-sector GPU state buffer, sampled per vertex | yes (light buffer) | yes (full record) — **aligned** |
| Light changes never touch vertices | yes | yes — **aligned** |
| Load-time dynamic marking (scroll/alpha/transfer) | `StaticDataApplier` | same, from sector specials — **aligned** |
| Move-start → flag + leave static; move-complete → re-bake | `World_SectorMoveStart/Complete` | snapshot diff → flip / N=2 settled → re-bake — **aligned in effect**, detected differently |
| Streaming | dirty-only (3 floats per changed sector) | full per-frame copy — **deviated** (bandwidth negligible; A/B determinism) |
| Tick interpolation | prev+current in dynamic VBO, VS mixes | single current value, CPU-interpolated — **deviated** (`DoInterpolations` already blends; exact classic parity) |
| Dynamic storage | second per-plane dynamic VBO | none — VS warp from the state buffer — **deviated** (per 04) |
