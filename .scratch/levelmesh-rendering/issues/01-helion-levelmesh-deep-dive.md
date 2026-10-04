# Helion LevelMesh deep-dive

Type: research
Status: resolved
Blocked by:

## Question

How does Helion actually implement its levelmesh pipeline, so the UZDoom spec can borrow mechanisms rather
than just shapes? Cover, with file references into the Helion repo
(`https://github.com/Helion-Engine/Helion`, master; start at `Core/Render/Common/Shared/World/` and
`Core/Maps/Specials/`, follow what they reference):

1. **Mesh construction**: what data is baked at level load (vertex attributes, per-sector data), storage
   layout, how sector geometry is triangulated (`WorldTriangulator`, `WallVertices`,
   `TriangulatedWorldVertex`, etc.).
2. **Dynamic sector handling**: how moving sectors (doors, lifts, 3D floor models) keep a static mesh
   correct — the "state management" system: per-sector state buffers, vertex-shader warping vs re-upload,
   what triggers an update.
3. **Sprites**: how they are rendered against the static mesh — frustum cull only or line-of-sight checks,
   draw organization, sorting.
4. **Portals / sector_link**: supported, partially supported, or absent?
5. **Lighting**: how light levels, sector light changes, and dynamic lights are represented on the mesh
   (per-vertex bake vs per-sector state vs shader pass).
6. **Draw organization**: one whole-level draw vs per-sector vs chunked; the per-frame draw-call structure;
   what CPU work remains per frame.

Findings: single Markdown file with a cited source per claim, per the research skill.

## Answer

Full findings: `../research/01-helion-levelmesh.md`. Gist:

- **Two per-texture VBOs split by a per-plane `SectorDynamic` bitmask** (`Movement/Light/TransferHeights/Scroll/Alpha`): a static `StaticVertex` VBO (no interpolation, uploaded once) and a dynamic `LegacyVertex` VBO carrying **both current and previous-frame Z/UV**. The GPU (`InterpolationShader`) interpolates `prev → current` by the tick fraction every render, so the CPU never steps animation. *(SectorDynamic.cs; InterpolationShader.cs; StaticVertex.cs; LegacyVertex.cs)*
- **Static means "persistent VBO updated in place," not "never uploaded."** A sector only costs CPU while it is in the blockmap's `DynamicSectors`/`DynamicSides` lists. Move start → link + zero-out its static verts (sub-upload); the per-tick pass then re-renders it into the dynamic VBO. Move complete → unlink + re-bake static. Static VBOs are patched via `UploadSubData` byte ranges. **Light changes never touch vertices** — they write a **per-sector GPU light buffer** (3 floats/sector: floor/ceiling/wall; a `samplerBuffer` sampled per-vertex by index; 32-colormap ramp + dark/fullbright slots). *(StaticDataApplier.cs; StaticCacheGeometryRenderer.cs; LightLevel.cs; Constants.cs)*
- **Sprites are one GL point each** (no UV/corners on the CPU); a **geometry shader** expands each point to a screen-facing billboard quad. Culling is distance + 180° FOV with **no line-of-sight ray** — the depth buffer occludes. Only transparent/fuzz sprites and transparent walls are distance-sorted (back-to-front); opaque sprites draw one `DrawArrays(Points)` per texture. *(EntityRenderer.cs; EntityProgram.cs; LegacyWorldRenderer.cs)*
- **Portals / sector_link: supported**, as a **flood-fill + fake-wall + stencil** system rather than true portal geometry — a control sector's flat (texture + light) floods through linked sectors bounded by 8192-tall fake walls; flood walls are per-texture static VBOs. ACS specials `Sector_SetLink`/`Sector_Set3dFloor`/`ExtraFloor_LightOnly` confirmed in the 1.0.0 notes. *(PortalRenderer.cs; FloodFillRenderer.cs; PortalStencilVertex.cs; Helion 1.0.0 release notes)*
- **Draw organization: batched per texture**, two geometry passes (dynamic `InterpolationShader` + static `StaticShader`) plus two alpha passes, with sky/portals and opaque sprites interleaved. Culling is a blockmap box + 180° occluder + max distance (explicitly "a hack until frustum culling exists"). Per-frame CPU scales with *moving* sectors + visible sprites, not level size. *(LegacyWorldRenderer.cs; StaticCacheGeometryRenderer.cs; RenderWorldDataManager.cs)*
- **Mesh build:** the whole level is pre-triangulated at load — `WorldTriangulator` per wall type plus a per-subsector fan (`N-2` triangles per flat). A wall's `PrevZ` animates its height only (Prev X/Y = current X/Y). 3D floors use `TransferHeights` to build a virtual sector chosen by camera Z (`Top`/`Middle`/`Bottom`), stitching parent + control planes/textures/light. *(WorldTriangulator.cs; GeometryRenderer.cs; TransferHeights.cs)*
