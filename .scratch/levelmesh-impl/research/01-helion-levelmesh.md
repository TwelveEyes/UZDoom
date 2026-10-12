# Helion LevelMesh Deep-Dive

Research into how the Helion Engine (C#/.NET, OpenGL, ~35 gameticks/sec —
`Constants.TicksPerSecond = 35.0`) implements its "levelmesh" world-rendering pipeline, for use as a
reference in the UZDoom levelmesh-rendering effort. Every claim cites a file in the Helion repo
(master, `https://github.com/Helion-Engine/Helion`), paths relative to the repo root. The "Legacy"
OpenGL renderer (`Core/Render/OpenGL/Renderers/Legacy/World/`) is the pipeline analyzed; a Vulkan
renderer also exists but is out of scope.

**Core idea in one paragraph.** Helion pre-triangulates the whole level at load into a **static**
per-texture VBO (`StaticVertex`, no interpolation). A per-plane `SectorDynamic` bitmask flags sectors
that are not permanently static (movement, scroll, transfer-heights, alpha). When a sector becomes
non-static it is **linked into the render blockmap** (`DynamicSectors`/`DynamicSides` lists) and its
vertices are **zeroed out of the static VBO** (sub-upload). The per-tick CPU pass then only re-renders
the dynamic sectors in view into a separate **dynamic** per-texture VBO (`LegacyVertex`, carrying both
current *and* previous-frame Z/UV); the GPU interpolates `prev → current` by the tick fraction every
render. When movement completes the sector is unlinked and re-baked into the static VBO. Lighting is
a per-sector GPU buffer sampled per-vertex (not baked into vertices). Sprites are one GL point each,
expanded to a billboard quad by a geometry shader. Portals/sector_link are a flood-fill + fake-wall +
stencil system.

---

## 1. Mesh construction & triangulation

**Baked at level load.** `GeometryRenderer.UpdateTo(IWorld)` preloads every texture, allocates per-side
vertex caches and per-sector subsector lists, then calls `CacheData(world)` and
`m_staticCacheGeometryRenderer.UpdateTo(world, m_lightBuffer)`.
*(Core/Render/OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.cs — `UpdateTo`, `CacheData`)*

- `CacheData` walks **every BSP subsector** in `world.BspTree.Subsectors`, appends each to
  `m_subsectors[sector.Id]`, and for each of a subsector's edges calls `RenderSide` (to build the
  cached wall vertices); then per sector it calls `RenderFlat` for the floor and ceiling. The whole
  level is triangulated once up front. *(GeometryRenderer.cs — `CacheData`)*

**Vertex format (the heart of it).** The CPU triangulation output is `TriangulatedWorldVertex` =
`X, Y, Z, U, V, PrevZ, PrevU, PrevV` — **8 floats carrying BOTH the current and the previous-frame Z
and UV for every vertex.**
*(Core/Render/Common/Shared/World/TriangulatedWorldVertex.cs)*

Wall quads are grouped in `WallVertices { TopLeft, TopRight, BottomLeft, BottomRight, PrevTopZ,
PrevBottomZ }`. *(Core/Render/Common/Shared/World/WallVertices.cs)*

**Triangulation is a per-vertex fan.** `WorldTriangulator` has one entry per wall type
(`HandleOneSided`, `HandleTwoSidedLower`, `HandleTwoSidedMiddle`, `HandleTwoSidedUpper`) plus
`HandleSubsector` for flats. Each wall method emits the 4 corner `TriangulatedWorldVertex` using
`line.Segment.Start/End` and the facing sector's `floor.Z`/`ceiling.Z` (and `PrevZ`) for top/bottom
heights. The middle variant computes the visible texture window via `CalculateMiddleDrawSpan`
(unpegged-lower, offset, clamping to the lower/upper openings) and can bail out with `nothingVisible`.
`HandleSubsector` iterates `subsector.ClockwiseEdges`, emitting one vertex per map vertex at
`sectorPlane.Z`/`PrevZ`; the floor is iterated in **reverse** for counter-clockwise winding. UVs come
from `CalculateOneSidedWallUV` / `CalculateTwoSided{Lower,Middle,Upper}WallUV` / `CalculateFlatUV`,
each taking a `previous` flag so a second pass produces `PrevU/PrevV` from `Side.ScrollData.LastOffset*`
/ `SectorScrollData.LastOffset`.
*(Core/Render/Common/Shared/World/WorldTriangulator.cs; MiddleDrawSpan.cs; WallUV.cs)*

**Flats are fanned.** In `RenderFlat`, a subsector with `N` clockwise edges is split into `N-2`
triangles; total flat vertices per sector is `Σ (ClockwiseEdges.Count - 2) * 3` (see
`InitSectorVerticies`). The fan is emitted by `GetFlatVertices(root, second, third)` over
`i = 1 .. len-2`. *(GeometryRenderer.cs — `RenderFlat`, `InitSectorVerticies`, `GetFlatVertices`)*

**Two on-GPU vertex layouts.**
- `StaticVertex` = `pos(3), uv(2), Alpha, AddAlpha, LightLevelBufferIndex` — **no prevPos/prevUV**
  (static geometry, no interpolation). *(StaticVertex.cs)*
- `LegacyVertex` = `pos(3), uv(2), Alpha, AddAlpha, LightLevelBufferIndex, prevPos(3), prevUV(2)` —
  13 floats, carries the previous-frame position and UV. *(LegacyVertex.cs)*

`GetWallVertices`/`SetWallVertices` expand a 4-corner `WallVertices` into a 6-vertex triangle strip
(0-1-2-3-4-5) and, per vertex, set `PrevX = TopLeft.X; PrevY = TopLeft.Y; PrevZ = wv.PrevTopZ` — i.e.
**the previous X/Y equal the current X/Y (2D position is fixed for walls); only PrevZ differs**, so a
wall animates its *height*, not its horizontal position. For flats, `GetFlatVertices` sets
`PrevZ = root.PrevZ`. *(GeometryRenderer.cs — `GetWallVertices`, `SetWallVertices`, `GetFlatVertices`)*

Per-sector data baked: the sector's floor/ceiling `SectorPlane` (Z, PrevZ, texture, light level — §5),
the per-sector subsector list, and cached per-side / per-sector vertex arrays (`m_vertexLookup`,
`m_vertexLowerLookup`, `m_vertexUpperLookup`, `m_vertexFloorLookup`, `m_vertexCeilingLookup`).
*(GeometryRenderer.cs — field declarations, `UpdateTo`)*

## 2. Dynamic sector handling (the "state management" system)

**The state flag.** `SectorPlane.Dynamic` is a `SectorDynamic` bitmask; a plane is static only when it
has no dynamic specials:
```csharp
[Flags] public enum SectorDynamic { None=0, Movement=1, Light=2, TransferHeights=4,
    TransferHeightStatic=8, Scroll=16, Alpha=32 }
// Sector.cs
IsFloorStatic   => Floor.Dynamic == SectorDynamic.None;
IsCeilingStatic => Ceiling.Dynamic == SectorDynamic.None;
AreFlatsStatic  => IsFloorStatic && IsCeilingStatic;
IsMoving        => ActiveFloorMove != null || ActiveCeilingMove != null;
```
*(Core/World/Static/SectorDynamic.cs; Core/World/Geometry/Sectors/Sector.cs; SectorPlane.cs)*

**What marks things dynamic, and when (the trigger) — `StaticDataApplier`.**
*(Core/World/Static/StaticDataApplier.cs)*
- **At load**, `DetermineStaticData(world)` marks inherently-dynamic geometry: a two-sided line with
  `Alpha < 1` → both sides `SetAllWallsDynamic(Alpha)` + `RenderBlockmap.LinkDynamicSide`; a line with
  `ScrollData` → `SetAllWallsDynamic(Scroll)` + link; a `SectorSpecialBase` (light) → `Light` (which
  is a **no-op** on the flag, see below); a `ScrollSpecial` → `Scroll`; any sector with
  `TransferHeights` → `TransferHeights`.
- `SetSectorDynamic(world, sector, floor, ceiling, flag, ...)`: sets `sector.{Floor|Ceiling}.Dynamic
  |= flag`, and **if the sector is not yet in the blockmap, `world.RenderBlockmap.Link(world, sector)`
  adds it to the blockmap's `DynamicSectors` lists.** Then it dispatches:
  - **Light short-circuits** — `if (flag == Light) return;` before the flag is ever set. A light
    special never makes a sector/side dynamic; lighting is handled purely through the GPU light buffer
    (§5). So light changes cost only a buffer write, no geometry work.
  - **Movement** — `SetSectorDynamicMovement` marks every wall of the sector's lines
    (`SetWallsDynamic(AllWallTypes, Movement)`), including the partner side when the *other* sector is
    the moving one. **`Movement` is refused at load** (`if (IsLoading && flag == Movement) return;`).
  - **TransferHeights** — marks the sector's own walls dynamic.
- **A sector's full movement lifecycle:**
  1. *Move starts* (`World_SectorMoveStart` → `SetSectorDynamic(..., Movement)`): plane flagged,
     sector linked into the blockmap, all its walls flagged dynamic, and the static cache
     **zeroes the sector's static vertices** (`ClearGeometryVertices` + `UploadSubData`) so it is no
     longer drawn from the static VBO. From the next tick the dynamic pass picks it up (it is now in
     `block.DynamicSectors`).
  2. *Move completes* (`World_SectorMoveComplete` → `ClearSectorDynamicMovement`): `Dynamic &=
     ~Movement`, sector **unlinked** from the blockmap (`UnlinkFromWorld`), wall flags cleared, and the
     plane/walls are **re-baked into the static VBO** (`AddSectorPlane(..., update:true)` +
     `AddLine(..., update:true)`).
  *(StaticDataApplier.cs — `ClearSectorDynamicMovement`; StaticCacheGeometryRenderer.cs —
  `World_SectorMoveStart`, `HandleSectorMoveStart`, `World_SectorMoveComplete`, `HandleSectorMoveComplete`)*

**Blockmap dynamic lists.** Each `Block` holds `LinkableList<Sector> DynamicSectors`,
`LinkableList<Side> DynamicSides`, and the usual `BlockLines`/`Entities`. `RenderBlockmap.Link` /
`LinkDynamicSide` add a sector/side to every block it spans. *(Core/World/Blockmap/Block.cs,
BlockMap.cs)*

**The GPU does the per-frame interpolation.** The dynamic path uses `InterpolationShader`; its vertex
shader blends prev→current by the tick fraction:
```glsl
uvFrag   = mix(prevUV, uv, timeFrac);
mixPos   = vec4(mix(prevPos, pos, timeFrac), 1.0);
gl_Position = mvp * mixPos;
```
So within a gametick a moving vertex travels from its previous to its current position — the CPU only
uploads the *current* and *previous* values; the GPU interpolates every render. Static geometry uses
`StaticVertex` + `StaticShader`, which has no prevPos and no `mix`.
*(Core/Render/OpenGL/Renderers/Legacy/World/InterpolationShader.cs; StaticVertex.cs)*

**Per-tick CPU pass touches only dynamic geometry.** In `LegacyWorldRenderer.IterateBlockmap`, for
each in-view block: `RenderSector` for `block.DynamicSectors`, `RenderSectorWall` for
`block.DynamicSides`. Inside `GeometryRenderer`:
- `RenderSectorSideWall` → `if (!side.IsStatic) RenderSide(...)` — **static sides are skipped**.
- `RenderTwoSided` gates each sub-wall on `side.{Lower|Middle|Upper}.IsDynamic` (+ visibility).
- `RenderSectorFlats` → `if (floorVisible && !sector.IsFloorStatic) RenderFlat(...)` (and ceiling) —
  **static flats are skipped**.
- The cached vertex array is only *rebuilt* when `side.OffsetChanged || m_sectorChangedLine ||
  data == null || m_cacheOverride` (walls) or `generate || flatChanged` (flats); otherwise the cached
  `LegacyVertex[]` is reused and simply re-appended to the VBO.
- "Changed" = `SectorPlane.CheckRenderingChanged()` (true if `LastRenderChangeGametick >=
  LastRenderGametick-1`, **or `PrevZ != Z`** (still interpolating), or scrolling) and the transfer
  variant `Sector.CheckRenderingChanged`. *(SectorPlane.cs — `CheckRenderingChanged`; Sector.cs)*

Sides whose *own* sector is moving are skipped in the `DynamicSides` loop
(`if (side.Sector.IsMoving || partnerSide.Sector.IsMoving) continue;`) because the sector render path
already handles them. *(LegacyWorldRenderer.cs — `IterateBlockmap`)*

**3D floors / transfer heights.** `TransferHeights` links a *parent* sector to a *control* sector.
`TransferHeights.GetView(viewSector, viewZ)` returns `Top` if `viewZ > control.Ceiling.Z`, `Middle` if
`viewZ > control.Floor.Z`, else `Bottom` (a sector without transfer heights is always `Middle`).
`GetRenderSector(view)` returns a **virtual sector** that stitches parent + control planes/textures/
light per view (e.g. `Bottom` uses the control floor as the top plane and the parent floor as the
bottom, carrying both sectors' `DataChanges`). The virtual sector is what `RenderSectorFlats` draws.
`IterateBlockmap` computes this view and, for a *Middle* view where nothing (floor, ceiling, or the
control sector's floor/ceiling) is `Movement`-dynamic, it **skips** the dynamic render (that geometry
lives in the static path). `RenderSector` routes `TransferHeights != null` sectors through
`RenderSectorFlats(sector, renderSector, transferHeights.ControlSector)`.
*(Core/World/Geometry/Sectors/TransferHeights.cs; Sector.cs — `GetRenderSector`;
LegacyWorldRenderer.cs — `IterateBlockmap`; GeometryRenderer.cs — `RenderSector`)*

## 3. Sprites

**One GL point per entity; the quad is generated on the GPU.** The sprite pass draws
`PrimitiveType.Points`. `EntityVertex` = `Pos(Vec3F), LightLevel(float), Alpha, Fuzz, FlipU,
PrevPos(Vec3F)` — **no UV and no corner data**; the vertex shader interpolates the point
(`gl_Position = vec4(mix(prevPos, pos, timeFrac), 1.0)`) and a **geometry shader**
(`layout(points) in; layout(triangle_strip, max_vertices=4) out`) reads `textureSize(boundTexture,0)`,
computes `halfTexWidth = textureDim.x*0.5`, and emits a screen-facing billboard quad of
`textureDim.x × textureDim.y` offset along `viewRightNormal`. The CPU sends one point per entity; the
GPU expands it.
*(Core/Render/OpenGL/Renderers/Legacy/World/Entities/EntityRenderer.cs — `Render`; EntityVertex.cs;
EntityProgram.cs — vertex + geometry + fragment shaders)*

**Culling is distance + 180° FOV, NOT line-of-sight.** `LegacyWorldRenderer.RenderEntity` culls on
invisible/no-sector flags, viewer self, a dot-product "in front of the 180° view" test against
`OccludePos`, and `RenderDistanceSquared > MaxDistanceSquared`. There is **no per-sprite LOS or
wall-occlusion ray test** — sprites are occluded by the depth buffer against the level mesh.
*(LegacyWorldRenderer.cs — `RenderEntity`, `SetOccludePosition` ("This is a hack until frustum culling
exists." — the occluder is the view pushed 32 units back, disabled when pitch > 45°))*

**Z-fighting via a 2D-position nudge, not depth sort.** With `Render.SpriteZCheck`, sprites sharing a
2D position are nudged apart by `NudgeFactor * distance * count` (`m_renderPositions` counts entities
per position). *(EntityRenderer.cs — `RenderEntity`)* `Render.SpriteClip` clamps a sprite's Z offset so
it doesn't poke through its floor (`GetOffsetZ`, `SpriteClipMin`, `SpriteClipFactorMax`).
*(EntityRenderer.cs — `GetOffsetZ`)*

**Sorting is only for the alpha/fuzz path.** Opaque sprites are **not sorted**; they are batched per
texture and drawn as one `DrawArrays(Points)` per texture. Only entities that are transparent
(`SpriteTransparency && Alpha < 1`) or `Flags.Shadow` (fuzz) are deferred into `m_alphaEntities`;
`RenderAlphaObjects` sorts that list (plus `m_geometryRenderer.AlphaSides`) by
`RenderDistanceSquared` **descending** (farthest first) and draws them back-to-front.
*(LegacyWorldRenderer.cs — `RenderEntity`, `RenderAlphaObjects`, `RenderObjectCompare`;
EntityRenderer.cs — `RenderEntity` alpha branch)*

**Sprite lighting is a per-vertex scalar, not the light buffer:**
`lightLevel = Bright ? 255 : (TransferFloorLightSector.LightLevel +
TransferCeilingLightSector.LightLevel)/2`, stored in `EntityVertex.LightLevel`.
**8-way rotation** is a CPU diamond-angle bit shift:
`CalculateRotation = (viewAngle - entityAngle + SpriteFrameRotationAngle) >> 29`.
*(EntityRenderer.cs — `RenderEntity`, `CalculateRotation`)*

## 4. Portals / sector_link

**Supported — as a flood-fill + fake-wall + stencil system, not true geometry portals.** The
`Core/Render/OpenGL/Renderers/Legacy/World/Geometry/Portals/` subtree (and `Portals/FloodFill/`)
implements "sector_link"-style lighting/floor/ceiling **flood fill**: a control/"flood" sector's flat
(texture + light) is extended through linked sectors, bounded by tall fake walls, with the visible
region masked by the **stencil buffer**.

- `PortalRenderer.HandleStaticFloodFillSide` builds fake walls (`FakeWallHeight = 8192`) along a flood
  region's boundary and registers them via `m_floodFillRenderer.AddStaticWall(plane, wall, minZ, maxZ)`
  → a *flood key* stored on the side as `LowerFloodKey`/`UpperFloodKey` (+ the `...FloodKey2` "camera
  below/above" variants). *(Portals/PortalRenderer.cs)*
- `PortalStencilVertex` is a single `Pos` — "a trivial vertex that is used only for being turned into
  fragments for **stencil writing**." *(Portals/PortalStencilVertex.cs)*
- **Flood walls are per-texture static VBOs.** `FloodFillRenderer` keeps one `FloodFillInfo` (a
  `StaticVertex`-like VBO of `FloodFillVertex`) per flood texture. `AddStaticWall` appends a
  6-vertex quad (`VerticesPerWall = 6`: topLeft, bottomLeft, topRight, topRight, bottomLeft,
  bottomRight), each `FloodFillVertex` carrying pos, `prevTopZ`, the flood `planeZ`/`prevPlaneZ`, the
  fake-wall `minZ`/`maxZ` bounds, and the light index. `UpdateStaticWall` sub-updates a wall when the
  flood plane moves; `ClearStaticWall` zeroes the 6 verts + sub-uploads (freed to a list, not
  deleted). `Render` binds the flood program (with `SectorLightTexture`, `Mvp`, `MvpNoPitch`,
  `TimeFrac`, `LightLevelMix`, `ExtraLight`, `Camera`) and, per flood texture, `Vbo.UploadIfNeeded()`
  + `DrawArrays()`. *(Portals/FloodFill/FloodFillRenderer.cs, FloodFillVertex.cs, FloodFillInfo.cs)*
- **Normal geometry defers to flood.** In `RenderTwoSidedLower`/`RenderTwoSidedUpper`,
  `if (side.{Lower|Upper}FloodKey > 0 || ...FloodKey2 > 0) { Portals.UpdateStaticFloodFillSide(...);
  return; }` — a wall in a flood region is **not** drawn as normal geometry; the flood renderer draws
  it. `StaticCacheGeometryRenderer.CheckForFloodFill` recomputes `FloodTextures` each time two
  neighboring sectors change and adds/clears the corresponding static flood walls.
  *(GeometryRenderer.cs — `RenderTwoSidedLower/Upper`; StaticCacheGeometryRenderer.cs —
  `CheckForFloodFill`; StaticDataApplier.cs — `CheckFloodFill`, `SetFloodFillSide`)*
- The light carried into linked sectors comes from `Sector.TransferFloorLightSector` /
  `TransferCeilingLightSector` (`Sector.FloorRenderLightLevel`), so flood-fill also handles the
  sector_link **light** transfer. *(Sector.cs)*
- The 1.0.0 release notes list `Sector_SetLink`, `Sector_Set3dFloor`, `ExtraFloor_LightOnly`, and
  "reset 3D sectors and links" — confirming sector_link/3D-floor support at the ACS/special level.
  *(https://github.com/Helion-Engine/Helion/releases/tag/1.0.0.0)*

## 5. Lighting

**Per-sector light level lives in a GPU buffer, indexed per-vertex (not baked into vertices).** Each
vertex carries a `LightLevelBufferIndex`; the shader does:
```glsl
int texBufferIndex = int(lightLevelBufferIndex);
float lightLevelBufferValue = texelFetch(sectorLightTexture, texBufferIndex).r;
lightLevelFrag = clamp(lightLevelBufferValue, 0.0, 256.0);
```
`sectorLightTexture` is a `samplerBuffer` (1-D buffer texture). *(Shader/LightLevel.cs —
`VertexLightBufferVariables`, `VertexLightBuffer`)*

**Exact buffer layout** (`Core/Util/Constants.cs` — `Constants.LightBuffer`):
```
index 0            = DarkIndex        (0)
index 1            = FullBrightIndex  (255)
index 2 .. 33      = 32-colormap ramp (ColorMapStartIndex=2, ColorMapCount=32)
index 34 + S*3     = sector S's slot  (SectorIndexStart=34, BufferSize=3)
                       [ FloorOffset=0, CeilingOffset=1, WallOffset=2 ]
```
So each sector owns **3 floats** (floor, ceiling, wall light levels); the buffer is sized
`Sectors.Count*3*4 + 34*4` bytes (matches `GeometryRenderer.UpdateTo`). A vertex's
`LightLevelBufferIndex` points at one of a sector's 3 floats via
`GetLightBufferIndex(sector, LightBufferType.{Floor,Ceiling,Wall})`; Floor/Ceiling use the
**transfer-light sector's** Id (so flood-fill/3D-floor lights resolve to the right sector). When a
sector's light changes the CPU writes only that sector's 3 slots
(`StaticCacheGeometryRenderer.UpdateLights`) and **every vertex referencing it reflects it with no
vertex touched.** *(StaticCacheGeometryRenderer.cs — `GetLightBufferIndex`, `SetLightBufferData`,
`UpdateLights`; GeometryRenderer.cs — `UpdateTo`)*

**Distance fog is in the fragment shader.** `LightLevel.FragFunction` turns `lightLevelFrag` (0–256)
plus the vertex-space depth `dist` (`(mvpNoPitch * mixPos).z`, `lightFadeStart = 56`) into a
32-colormap index, then `lightLevel = (colorMaps - index)/colorMaps`, then
`lightLevel = mix(lightLevel, 1.0, lightLevelMix)`. `lightLevelMix = 1` = full-bright (god mode);
`extraLight` (from the player's light source; `ExtraLightFactor = 3`) shifts the index.
*(Shader/LightLevel.cs — `FragFunction`, `Constants`, `VertexDist`; LegacyWorldRenderer.cs —
`SetInterpolationUniforms`, `GetShaderUniforms`)*

**Light levels are per-sector and themselves interpolated** (via `PrevZ`-driven re-render + the
`SectorPlane.LightLevel` short). The *render* value is `Sector.FloorRenderLightLevel` /
`CeilingRenderLightLevel` (the transfer-light sector's floor/ceiling light). `SetLightLevel /
SetFloorLightLevel / SetCeilingLightLevel` set the level and stamp `RenderLightChangeGametick`;
`Sector.LightingChanged(gametick)` is the "changed recently" test driving the slot re-write.
*(SectorPlane.cs — `LightLevel`, `RenderLightLevel`; Sector.cs — `FloorRenderLightLevel`,
`CeilingRenderLightLevel`, `SetLightLevel`, `LightingChanged`)*

## 6. Draw organization & per-frame CPU work

**Per-frame pass order** (`LegacyWorldRenderer.PerformRender`):
1. `Clear`
2. `SetOccludePosition` (compute the 180° occlusion point)
3. `IterateBlockmap` — **the only per-tick geometry CPU work**; also emits opaque sprite points and
   queues alpha entities/sides
4. `PopulatePrimitives` (player tracers)
5. `m_geometryRenderer.RenderPortalsAndSkies` (sky sphere, flood-fill portals, static skies)
6. `m_entityRenderer.RenderNonAlpha` — opaque sprites (`DrawArrays(Points)` per texture)
7. bind `InterpolationShader` → `m_worldDataManager.DrawNonAlpha()` — **dynamic** geometry, non-alpha
8. bind `StaticShader` → `m_geometryRenderer.RenderStaticGeometry()` — **static** cached geometry
9. `m_entityRenderer.RenderAlpha` — alpha sprites (points)
10. bind `InterpolationShader` → `m_worldDataManager.DrawAlpha()` — dynamic alpha geometry
11. `m_primitiveRenderer.Render`
*(LegacyWorldRenderer.cs — `PerformRender`)*

**Batching is per texture, with two geometry passes.** It is neither one whole-level draw nor
per-sector draw calls. **Static** geometry: one `StaticVertex` VBO per (texture, repeatY), sorted so
transparent textures draw last (`TransparentGeometryCompare` — avoids a transparent middle discarding
things behind it); drawn by iterating `m_geometry` and calling `DrawArrays` per texture
(`StaticCacheGeometryRenderer.Render`). **Dynamic** geometry: one `LegacyVertex` VBO per texture
(`RenderWorldDataManager`), `DrawNonAlpha()`/`DrawAlpha()` = one `Vbo.Upload()` + `DrawArrays()` per
non-empty texture. Alpha objects (transparent walls + transparent/fuzz sprites) are the only
per-object, distance-sorted items.
*(StaticCacheGeometryRenderer.cs — `Render`, `AllocateGeometryData`, `TransparentGeometryCompare`;
RenderWorldDataManager.cs — `DrawNonAlpha`/`DrawAlpha`; RenderWorldData.cs — `Draw`;
LegacyWorldRenderer.cs — `RenderAlphaObjects`)*

**"Static" really means "persistent VBO updated in place," not "never uploaded again."** The static
cache updates itself from world events (`UpdateTo` subscribes to `SectorMoveStart`,
`SectorMoveComplete`, `SideTextureChanged`, `PlaneTextureChanged`, `SectorLightChanged`):
- **Light changes** → `UpdateLights` maps the light buffer and writes only the changed sector's 3
  floats. *(StaticCacheGeometryRenderer.cs — `World_SectorLightChanged`, `UpdateLights`)*
- **Geometry that must change** (movement start/complete, texture swaps) → `UpdateBufferData` collects
  the affected `StaticGeometryData` (texture + start index + length), groups by texture, sorts by
  index, and issues **`UploadSubData`** for contiguous byte ranges — only the changed bytes are
  re-uploaded. Newly-allocated geometry (no existing slot) goes through `m_runtimeGeometry`, which does
  a full `UploadIfNeeded` once and is cleared next frame. *(StaticCacheGeometryRenderer.cs —
  `UpdateRunTimeBuffers`, `UpdateBufferData`, `UpdateVertices`, `AddRuntimeGeometry`,
  `World_SideTextureChanged`, `World_PlaneTextureChanged`)*

**Culling is blockmap + 180° occluder + max distance, not a frustum.** `IterateBlockmap` iterates
`world.RenderBlockmap.Blocks` over a box of `MaxDistance` around the view position; each block is
skipped unless `block.Box.InView(occluder, viewDirection)` (the 180° "behind the camera" test, "a hack
until frustum culling exists"). *(LegacyWorldRenderer.cs — `IterateBlockmap`, `SetOccludePosition`)*

**What CPU work remains per frame (the "what's left on the CPU" answer):**
- The blockmap box iteration + per-block 180°/distance tests.
- For each **dynamic** sector/side in view: `CheckRenderingChanged`, and only for those that actually
  changed (or are still interpolating: `PrevZ != Z`, scrolling, recent light change) re-triangulate the
  changed wall/flat into the cached `LegacyVertex[]`; then append the (cached) vertices to the
  per-texture dynamic VBO.
- Per-tick re-upload of the **dynamic** VBOs (`Vbo.Upload()` per non-empty per-texture buffer) — static
  geometry is *not* re-uploaded (only sub-ranges when it changed).
- One `EntityVertex` point per visible entity + the 2D-position nudge + rotation index.
- Alpha pass: collect + sort of `m_alphaEntities` + `AlphaSides` by distance.

Static, non-moving geometry costs nothing on the CPU after the initial bake; the per-frame cost scales
with the number of *moving/changed* sectors and visible sprites, not with level size.
*(LegacyWorldRenderer.cs — `IterateBlockmap`; GeometryRenderer.cs — `RenderSector`,
`RenderSectorSideWall`, `RenderFlat`; RenderWorldData.cs — `Draw`; StaticCacheGeometryRenderer.cs)*

---

## Implications for UZDoom (researcher inference — not from Helion sources)
- The single most transferable idea is the **per-plane dynamic bitmask + blockmap dynamic lists +
  static/dynamic VBO split**, with the GPU doing prev→current interpolation so the CPU never steps
  animation; UZDoom's existing `sector`/`sector_t` movement machinery maps naturally onto a `Dynamic`
  flag + `Link/Unlink`.
- **Lighting as a per-sector GPU buffer sampled per-vertex** (3 floats/sector) is a cheap way to make
  animated lights free of vertex rewrites — a strong candidate against any per-vertex baked-light
  approach.
- **Sprites as one point + geometry-shader billboard** removes all per-sprite quad/UV/LOS work from the
  CPU; depth handles occlusion. UZDoom's current per-sprite CPU work could be reduced by the same
  device.
- **Portals/sector_link as flood-fill + fake-wall + stencil** is a pragmatic way to do linked
  lighting/floors without true portal geometry.

## Source files consulted (primary)
- Core/Render/Common/Shared/World/WorldTriangulator.cs, TriangulatedWorldVertex.cs, WallVertices.cs,
  WallUV.cs, MiddleDrawSpan.cs
- Core/Render/OpenGL/Renderers/Legacy/World/LegacyWorldRenderer.cs, InterpolationShader.cs,
  StaticVertex.cs, LegacyVertex.cs
- Core/Render/OpenGL/Renderers/Legacy/World/Data/RenderWorldDataManager.cs, RenderWorldData.cs,
  RenderData.cs, RenderDataCollection.cs
- Core/Render/OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.cs (read ~all ~1450 lines)
- Core/Render/OpenGL/Renderers/Legacy/World/Geometry/Static/StaticCacheGeometryRenderer.cs (read all),
  LightBufferType.cs (via refs)
- Core/Render/OpenGL/Renderers/Legacy/World/Shader/LightLevel.cs
- Core/Render/OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalRenderer.cs, PortalStencilVertex.cs,
  Portals/FloodFill/FloodFillRenderer.cs, FloodFillVertex.cs, FloodFillInfo.cs (via refs)
- Core/Render/OpenGL/Renderers/Legacy/World/Entities/EntityRenderer.cs, EntityVertex.cs,
  EntityProgram.cs
- Core/World/Geometry/Sectors/SectorPlane.cs, Sector.cs, TransferHeights.cs
- Core/World/Static/SectorDynamic.cs, StaticDataApplier.cs
- Core/World/Blockmap/Block.cs, BlockMap.cs
- Core/Util/Constants.cs (`LightBuffer`, `TicksPerSecond`)
- Repo README + 1.0.0 release notes (sector_link/3D-floor special list)
