# Handoff D2 — record pools as samplerBuffers (surface / sector ring / 3D light)

Status: COMPLETE. Extends `FGLLevelMesh` (chunk D1's VBO+VAO class) with the
three record pools levelmesh.vp fetches via texelFetch on samplerBuffers
(GL 3.3 core, no SSBO). Not committed (per task).

Build result: `cmake --build build --config RelWithDebInfo --parallel 3` from
repo root → `[100%] Built target zdoom`, exit 0. The modified TU was force-
recompiled fresh (`touch gl_levelmesh.cpp` + rebuild) with ZERO warnings
(pre-existing a_dynlight.h:266 / DAP baselines in other TUs are the only
warnings in the tree).

Shader validation: `/tmp/levelmesh_vp_d2.vert` =
`"#version 330\n#extension GL_ARB_gpu_shader5 : require\n"` + precision lines
+ minimal prelude stand-in (ViewMatrix/ProjectionMatrix/NormalViewMatrix/
ModelMatrix uniform mat4s, all 9 `in` attributes at their VATTR_ locations,
the standard out varyings vTexCoord/vColor/pixelpos/glowdist/gradientdist/
vWorldNormal/vEyeNormal/vLightmap) + `levelmesh_light.glsl` + patched
`levelmesh.vp`. `glslangValidator -S vert /tmp/levelmesh_vp_d2.vert` → exit 0,
no errors. (gpu_shader5 is required for the `flat out int vFogEnabled`.)

## Files changed
- `src/common/rendering/gl/gl_levelmesh.h` — three TBO buffer+texture member
  pairs + sector count/light capacity/work-area members; `GetSurfaceTexture()`/
  `GetSectorStateTexture()`/`GetLightTexture()`, `IsRecordsReady()`,
  `UploadSectorSlot()`, `UploadLightData()`, private `CreateRecordPool()`.
- `src/common/rendering/gl/gl_levelmesh.cpp` — repack helpers (file-local
  statics), pool creation in `SetMesh`, texture+buffer teardown in `Destroy`,
  the two per-frame upload methods.
- `wadsrc/static/shaders/glsl/levelmesh.vp` — exactly two changes:
  (1) `LMSplitPlaneRef` now rejects `planeRef < 0.0` (host sentinel -1.0f;
  0xFFFFFFFF is not float32-representable), comment adjusted to explain the
  encoding; (2) the three stale samplerBuffer comments corrected to the real
  record sizes. No other shader line touched.

## API surface for chunk E
```cpp
#include "gl_levelmesh.h"   // namespace OpenGLRenderer, FGLLevelMesh *GLLevelMesh

// per-frame, at the frame build site (inside the interpolation window):
bool   FGLLevelMesh::IsRecordsReady() const;          // all three TBOs created
GLuint FGLLevelMesh::GetSurfaceTexture() const;       // static surface records
GLuint FGLLevelMesh::GetSectorStateTexture() const;   // HW_MAX_PIPELINE_BUFFERS-slot ring
GLuint FGLLevelMesh::GetLightTexture() const;         // grow-only 3D light buffer

void   FGLLevelMesh::UploadSectorSlot(int slot, const uint8_t *bytes);
//   bytes = mesh->state.GetSectorSlot(slot), size = SectorSlotSize() = sectorCount*96.
//   Repacks every 96-byte record into the 36-float layout and glBufferSubData's
//   the one slot at offset slot*sectorCount*144. No per-frame allocation.

void   FGLLevelMesh::UploadLightData(const uint8_t *bytes, size_t byteSize);
//   bytes = mesh->state.GetLightData(), byteSize = LightDataSize() = count*12.
//   glBufferData if the record count grew the capacity, else glBufferSubData at 0.
```

Draw-time recipe (recommended; follow the existing engine pattern in
gl_shader.cpp:755 where uniforms are looked up against `shader->GetHandle()`):
1. Bind the three textures to sampler units (pick free units; the levelmesh
   path does not use any texture unit yet, so e.g. 0/1/2 work — verify
   against whatever the fp uses when chunk E lands it):
   `glActiveTexture(GL_TEXTURE0+i); glBindTexture(GL_TEXTURE_BUFFER, GetXxxTexture());`
2. `int loc = glGetUniformLocation(shader->GetHandle(), "uSurface");` (and
   "uSectorState", "uLightState") → `glUniform1i(loc, unit)`. Do this once per
   frame (or cache; the locations are stable for the program).
3. Set the int uniforms: `uSectorStateSlot` = the slot just uploaded,
   `uSectorCount` = sectorCount (the model's `mesh->state.sectorCount`),
   `uSurfaceCount` = `mesh->SurfaceCount()`.
4. Bind the region VAO (`GetVAO(r)`) and draw as D1 documented.

Note: the fp side of the levelmesh pair does NOT fetch these buffers (all
per-surface values are promoted varyings); only the VS needs them bound.

## Repack slot tables AS IMPLEMENTED

Surface record — 24 floats = 6 float4 texels per `LevelMeshSurface`
(exactly LMFetchSurface's fetch order; static, uploaded once in SetMesh):

| slots | fields |
|-------|--------|
| [0]   | textureIndex as float() (0xFFFFFFFF rounds to 0xFFFFFFC0 in float32; never fetched/compared by the VS — noted in a code comment) |
| [1]   | regionIndex |
| [2..5]| planeRef[0..3] via `LM_PlaneRefFloat`: -1.0f for LM_PLANEREF_NONE (raw 0xFFFFFFFF not float32-exact), float(ref) otherwise |
| [6]   | lightRef |
| [7]   | flags |
| [8..11]| vparam[0..3] (raw floats) |
| [12..15]| light, tierLight, relSmooth, relNonSmooth (int16 → float) |
| [16]  | lightlistRef (int16; -1 stays -1.0f, the VS compares >= 0.0) |
| [17][18] | bandOffset, bandCount |
| [19][20] | skyScrollKind, skyScrollKind2 |
| [21..23]| 0,0,0 reserved |

Sector state — 36 floats = 9 float4 texels per `LevelMeshSectorState`; ring is
HW_MAX_PIPELINE_BUFFERS slots, slot-major: record r of slot s lives at float
offset s*sectorCount*36 + r*36 (the VS computes idx = uSectorStateSlot*
uSectorCount + sectorIndex). Exactly LMFetchSector's fetch order:

| slots | fields |
|-------|--------|
| [0..3]  | floorPlane[4] (nx,ny,nz,D) |
| [4..7]  | ceilPlane[4] |
| [8..9]  | floorScroll[2] |
| [10..11]| ceilScroll[2] |
| [12][13][14][15] | lightlevel, planeLight[0], planeLight[1], flags (16-bit → float) |
| [16..19]| glowFloorColor ABGR PalEntry unpacked to r,g,b,a 0-255: r=(c>>16)&0xFF, g=(c>>8)&0xFF, b=c&0xFF, a=(c>>24)&0xFF |
| [20]    | glowFloorHeight |
| [21..24]| glowCeilColor same ABGR unpack |
| [25]    | glowCeilHeight |
| [26..34]| colormap[0..8] as floats 0-255 (LightColor rgb, BlendFactor, Desaturation, FadeColor rgb, FogDensity) |
| [35]    | 0 reserved |

3D light — 12 floats = 3 float4 texels per `LevelMeshLightState`, exactly
LMFetchLight's fetch order:

| slots | fields |
|-------|--------|
| [0]   | lightlevel (int16 → float) |
| [1..9]| colormap[0..8] as floats 0-255 |
| [10][11]| 0,0 reserved |

Repackers are file-local statics in gl_levelmesh.cpp (`RepackSectorSlot`,
`RepackLightData`, plus `LM_PlaneRefFloat` and `LM_ExpandABGR`); sector/light
records are memcpy'd from the byte ranges into struct locals before field
access (no unaligned reads, no strict-aliasing issues).

## Object lifetimes + upload strategy
- All three buffer+texture pairs: created in `SetMesh` via private
  `CreateRecordPool` (glGenBuffers → glBufferData on GL_TEXTURE_BUFFER →
  glGenTextures → glTexBuffer(GL_RGBA32F) → NEAREST filter + REPEAT wrap →
  unbind), destroyed in `Destroy()` (textures and buffers deleted with
  nonzero guards, members zeroed) — i.e. built at level load, dropped at
  level unload / video mode change, exactly like the D1 VBO/IBO/VAOs. Every
  upload path ends with `glBindBuffer(GL_TEXTURE_BUFFER, 0)`.
- Surface TBO: GL_STATIC_DRAW, one-shot 24-float/surface repack in SetMesh.
- Sector ring TBO: GL_DYNAMIC_DRAW, allocated once at
  HW_MAX_PIPELINE_BUFFERS * sectorCount * 144 bytes with null data; slot 0 is
  filled from `mesh->state.GetSectorSlot(0)` in SetMesh so the buffer is valid
  before chunk E's first frame. Per-frame: one glBufferSubData per slot
  (sectorCount*144 bytes) at the slot offset; repack work area is a member
  `TArray<float>` sized once (grows only), no per-frame allocation.
- Light TBO: GL_DYNAMIC_DRAW, grow-only capacity (`mLightCapacity` records);
  initial fill from the build-time PackSnapshot in SetMesh. Per-frame:
  glBufferData on growth else glBufferSubData at 0. A level with zero 3D
  lights keeps a 1-record (zeroed) allocation so the texture object is never
  zero-sized; an empty per-frame upload is a no-op (the VS only fetches it
  for lightlistRef >= 0).
- `SetMesh(mesh)` remains a no-op when `mesh == mMesh` (safe per-frame call);
  `IsRecordsReady()` is separate from D1's `IsReady()` (VAOs) and is set in
  lockstep with it by SetMesh.

## Known gaps deferred to later chunks
- Band table TBO: `LevelMeshBand` is EMPTY until ticket 05 — deliberately no
  fourth pool was created. When ticket 05 lands, add a third-style static TBO
  (buffer+texture pair + repack + GetXxxTexture) and extend IsRecordsReady.
- Draw call + `gl_uselevelmesh` cvar + pass order: chunk E. Chunk E calls
  SetMesh → PackSnapshot → UploadSectorSlot(slot, ...) →
  UploadLightData(...) at the frame-build site, then binds textures/uniforms
  per the recipe above.

## Residual risks
1. `GetSurfaceTexture`/`GetSectorStateTexture`/`GetLightTexture` return raw
   handles; nothing enforces binding them before a levelmesh draw — chunk E
   must bind all three (or skip the pass when !IsRecordsReady()).
2. The sector ring TBO size is HW_MAX_PIPELINE_BUFFERS * sectorCount * 144
   bytes (96 B/sector × 2 slots on desktop, × 4 on Android). A 50k-sector
   level is ~20 MB (desktop) — acceptable for a static per-level object, but
   worth a budget note in the E/F review.
3. `float(s.textureIndex)` precision loss at 0xFFFFFFFF is documented and
   harmless only as long as the VS never compares textureIndex; if a future
   chunk adds such a compare it must use the int() round-trip like
   surfaceIndex does (exact below 2^24) — 0xFFFFFFFF still would not decode,
   so keep that path out of shader logic.
4. Light records do not keep stable indices across frames (documented in
   levelmesh_state.h); chunk E must resolve lightlistRef in the same frame it
   packs — no change needed here, but the TBO contents are frame-local.
5. aNormal is still unbound (D1 residual) — normal-based shading fallback
   remains until E/F provides per-surface normals.
6. GL context is assumed current at SetMesh/upload time (same assumption as
   D1's hooks; true for the single-context SDL path).
