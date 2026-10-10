# Handoff E2 — chunk E part 2: first GL33 draw of the level-mesh

Status: COMPLETE. Implements ticket 03 chunk E2: when `Level->useLevelMesh`
is true, `HWDrawInfo::RenderScene` draws wall/flat geometry through the
per-(region, texture) sub-range table via a new DFrameBuffer seam instead of
the three classic geometry passes; cvar-0 (`gl_uselevelmesh 0`) is untouched.
Not committed (per task). Game was not run (per task); in-game verification
is the next chunk's job.

Build result: `cmake --build build --config RelWithDebInfo --parallel 3` from
repo root → `[100%] Built target zdoom`, exit 0. All modified TUs were
force-recompiled fresh; ZERO new warnings (the only warnings in the rebuild
are the pre-existing tree baselines: a_dynlight.h:266 anon-enum,
d_main.cpp:1178 sign-compare — both present before this change).

## What landed

### A) currentSlot plumbing
- `src/common/rendering/levelmesh_frame.h`: `FLevelMeshFrame::currentSlot`
  (int, default -1). Records the sector-state ring slot the frame's data was
  uploaded to; the draw pass feeds it to the shader as uSectorStateSlot.
- `src/d_main.cpp` E1 block: `Level->levelMeshFrame.currentSlot = slot;`
  right after `Build()`, before `UploadLevelMeshSlot`. The comment above the
  block now says E1/E2 and points at the draw pass. D_Render builds the frame
  BEFORE `action()` (the render), so by the time RenderScene runs, currentSlot
  is valid on every levelmesh frame; the -1 guard below is belt-and-braces.

### B) scene-layer gate (`src/rendering/hwrenderer/scene/hw_drawinfo.cpp`)
- In `HWDrawInfo::RenderScene`, the three classic geometry passes (part 1
  PLAINWALLS/PLAINFLATS, part 2 MASKEDWALLS/MASKEDFLATS with
  gl_mask_threshold, part 3 MASKEDWALLSOFS) are wrapped in:
  `if (Level->useLevelMesh && screen->DrawLevelMesh(state, Level)) { } else { <the three passes> }`.
- The state setup above the gate (SetDepthFunc(DF_Less), AlphaFunc(0),
  ClearDepthBias, EnableTexture(gl_texture), EnableBrightmap(true)) is shared
  by both paths and unchanged.
- Fallback semantics: `DrawLevelMesh` returns false when the backend cannot
  draw (no GL objects / no frame built yet) → classic passes run, so the
  level is always drawn. gl_uselevelmesh 0 → `useLevelMesh` false → the seam
  call never happens; byte-for-byte classic.
- NOTE: the scene is still BUILT unconditionally (RenderBSP fills the draw
  lists); only drawing is replaced. With `gl_sort_textures` on, the sort of
  now-unused wall/flat lists still runs — harmless waste, left alone to keep
  the gate minimal.

### C) DFrameBuffer seam
- `src/common/rendering/v_video.h`: `virtual bool DrawLevelMesh(FRenderState
  &state, FLevelLocals *level) { return false; }` next to the E1 upload seams.
- `src/common/rendering/gl/gl_framebuffer.{h,cpp}`: `OpenGLFrameBuffer::
  DrawLevelMesh` implements the whole pass (see D). The per-entry loop lives
  in the GL layer, NOT in a new hw_drawinfo.cpp section: the scene layer is
  backend-agnostic (no GL includes), and the loop needs FGLLevelMesh VAO
  handles + record-pool texture binds. The only GL-specific primitive that
  crosses into FRenderState is `DrawLevelMesh(vao, index, count)` (below).

### D) the draw pass (`OpenGLFrameBuffer::DrawLevelMesh`, gl_framebuffer.cpp)
1. Guards: `GLLevelMesh` null / !IsReady / !IsRecordsReady / no mesh → false
   (classic fallback). Empty draw list → true (drawn nothing, no fallback).
   `currentSlot < 0` → false.
2. `state.SetEffect(EFF_LEVELMESH)` once for the pass; BindEffect rebinds on
   the next standard draw carrying a different effect (each classic draw sets
   its own effect, so no restore needed).
3. Record pools bound once to fixed sampler units 12/13/14 (surface / sector
   state ring / light data) as GL_TEXTURE_BUFFER; active unit restored to
   GL_TEXTURE0 afterwards. Units 12-15 are unused by anything else: material
   layers own 0-11 (prelude `tex` + texture2..texture12), ShadowMap 16,
   LightMap 17.
4. Fills ONE `LevelMeshDrawParams` per frame (see E) from FLevelLocals +
   cvars; `state.SetLevelMeshParams(p)` once before the loop.
5. Per entry: bounds-check region/texRange indices (stale entries skipped),
   resolve `FGameTexture *tex = TexMan.GetGameTexture(FSetTextureID(
   tr.textureIndex))` (null → skip), then per sub-range state:
   - masked texture: STYLE_Source + AlphaFunc(Alpha_GEqual, gl_mask_threshold)
     — exactly the classic part 2 treatment;
   - `tex->GetTranslucency()`: STYLE_Translucent + AlphaFunc(0) — the classic
     GLDL_TRANSLUCENT treatment minus sorting (documented gap);
   - else: STYLE_Source + AlphaFunc(0) (classic part 1).
   Then `state.SetMaterial(tex, UF_Texture, 0, CLAMP_NONE, NO_TRANSLATION,
   -1, nullptr)` and `state.DrawLevelMesh(GLLevelMesh->GetVAO(region),
   tr.iboOffset, tr.iboCount)`.
   Consecutive same-texture sub-ranges are cheap: FGLRenderState caches
   lastMaterial.

### E) per-draw parameter block
- `src/common/rendering/hwrenderer/data/hw_renderstate.h`: `struct
  LevelMeshDrawParams` (all fields default to a plausible no-state draw),
  `FRenderState::mLevelMesh`, `SetLevelMeshParams()`, and `virtual void
  FRenderState::DrawLevelMesh(unsigned int vao, int index, int count) { }`
  — NON-pure with an empty base body on purpose: VkRenderState (Vulkan, no
  levelmesh) inherits the no-op; only GL33 overrides.
- Fields: sectorStateSlot, surfaceCount, sectorCount (record pool geometry);
  fogDensity/outsideFogDensity/outsideFog/flags3/flags2/flags/skyfog/culldist
  (per-level, straight from FLevelLocals; densities are the RAW int values —
  levelmesh.vp's LM_GetFogDensity takes `int(uLevelFogDensity)` exactly like
  the classic SetFog/GetFogDensity chain); skyPos[3] = hw_sky1pos/hw_sky2pos/
  hw_skymistpos (per-frame sky scroll — levelmesh.vp adds
  uLevelSkyPos[kind] to the baked sky UVs, replacing the classic texture-
  matrix scroll; no extra work needed); lightMode = getRealLightmode(level,
  true) (FLevelLocals stores the user setting; the classic pass resolves it —
  there is NO FLevelLocals::lightmode member); distanceCullType, cullColor
  (gl_cullcolor → 0-1 floats), fogMode (gl_fogmode), visibility
  (r_visibility), extralight (r_extralight), weaponLight
  (r_viewpoint.extralight * gl_weaponlight, per the levelmesh.vp comment),
  fakeContrast (r_fakecontrast), wallHorizLight/wallVertLight (FLevelLocals),
  insybox (portalState.inskybox).

### F) shader plumbing
- `src/common/rendering/gl/gl_shader.h`: `FShader::LevelMeshUniforms` struct
  — raw int locations for all 23 levelmesh.vp uniforms + the three
  samplerBuffers (uSurface/uSectorState/uLightState), default -1. `Init()`
  fetches them; `Set(const LevelMeshDrawParams&)` pushes them (glUniform1i/
  1f/1ui/3fv/4fv). `-1` locations make every call a GL no-op, so non-
  levelmesh shaders cost nothing and share the code path. `FShader::
  SetLevelMesh()` public wrapper; member `lmU`.
- `src/common/rendering/gl/gl_shader.cpp`:
  - `lmU.Init(hShader)` after muTimer in Load().
  - After the ShadowMap(16)/LightMap(17) assignments (while hShader is still
    a program handle, before glUseProgram(0)): uSurface→12, uSectorState→13,
    uLightState→14.
- `src/common/rendering/gl/gl_renderstate.{h,cpp}`:
  - `FGLRenderState::ApplyShader()` pushes `activeShader->SetLevelMesh(
    mLevelMesh)` right after muAlphaThreshold (both the effect branch and
    the material branch flow through there).
  - `FGLRenderState::DrawLevelMesh(vao, index, count)`: ApplyState() +
    ApplyShader() (NO ApplyBuffers — the levelmesh VAOs carry their own IBO
    on GL_ELEMENT_ARRAY_BUFFER and mVertexBuffer is not set), then
    glBindVertexArray(vao) → glDrawElements(GL_TRIANGLES, count,
    GL_UNSIGNED_INT, byteOffset) → glBindVertexArray(GLRenderer->mVAOID) to
    keep the renderer's default-VAO assumption in sync with
    mCurrentVertexBuffer/mCurrentIndexBuffer. Wrapped in drawcalls.Clock().

### G) alpha testing enabled for the levelmesh effect
- `src/common/rendering/hwrenderer/data/hw_shaderpatcher.cpp` effectshaders
  table: the levelmesh row's defines changed from `"#define NO_ALPHATEST\n"`
  to `""`. levelmesh.fp already contains the hook at its material step
  (`#ifndef NO_ALPHATEST: if (frag.a <= uAlphaThreshold) discard;`, same
  position as main.fp); `uAlphaThreshold` comes from the C++ uniform prelude
  prepended to every fragment shader in FShader::Load. The FShaderManager
  alpha-variant selection (`mAlphaThreshold >= 0.f`) already picks the right
  compiled variant per sub-range.

## Texture unit map (final)
0 = tex (material base), 1-11 = texture2..texture12 (material layers),
12 = uSurface, 13 = uSectorState, 14 = uLightState, 15 free, 16 = ShadowMap,
17 = LightMap.

## Known gaps / decisions for E3+
- No per-segment sorting: masked sub-ranges draw immediately behind solid
  (classic part 2 is also unsorted — parity), and truly translucent
  sub-ranges draw in the same pass with depth mask ON instead of in the
  sorted GLDL_TRANSLUCENT queue with depth mask OFF. Additive-blend
  (STYLE_Add) surfaces are treated as plain opaque; they render visibly but
  not blended. Routing these into GLDL_TRANSLUCENT needs a new item type in
  hw_drawlist.h (the sorting is tied to HWWall seg geometry) — deliberately
  deferred.
- aNormal is unbound in the region VAOs (location 4 left unbound → reads
  (0,0,0,1)); levelmesh.fp takes its no-normal fallback branch. Normal-based
  lighting/glossiness is inert until E/F bind it or the VS derives normals.
- The sub-range table has no polygon offset: classic part 3
  (MASKEDWALLSOFS, depth bias -1/-128) has no levelmesh equivalent;
  back-to-back masked walls may Z-fight in edge cases.
- Sky: surfaces bake the sky texture index and scroll via uLevelSkyPos[kind]
  (per-surface skyScrollKind selects the component); treated as ordinary
  textured sub-ranges otherwise, matching the classic wall treatment of sky
  textures. Skybox/portal skies are out of scope until ticket 06.
- The draw pass binds materials with CLAMP_NONE; per-region clamp state is
  not tracked in the static table.
- If the levelmesh effect shader fails to compile on a driver,
  mEffectShaders[EFF_LEVELMESH] is null and ApplyShader would crash — same
  failure profile as the other builtin effects (fogboundary etc.), no special
  handling added.

## Residual risks / decisions
- Shader compilation is NOT verified by this chunk (game not run): removing
  NO_ALPHATEST only enables a line that already existed in levelmesh.fp with
  uAlphaThreshold provided by the prelude, so compile risk is low but the
  first in-game run is the real check. If the effect fails to load on some
  driver, see the gap above.
- weaponLight formula (`r_viewpoint.extralight * gl_weaponlight`) follows the
  levelmesh.vp comment; the classic per-wall GetWeaponLighting does a bit more
  (per-line distance falloff). First-draw parity is acceptable; refine in E3
  if visible.
- `getRealLightmode(level, true)` is called once per frame by the draw pass;
  it's a cheap flag read, but HWDrawInfo computes its own `lightmode` member —
  if that logic ever diverges from getRealLightmode the two paths could
  disagree. (HWDrawInfo::lightmode is private to the scene layer; passing it
  through the seam would be the clean fix if needed.)
- Fog: mFogEnabled/EnableFog(true) from RenderScene still flows into
  muFogEnabled, which levelmesh.fp uses for its `uFogEnabled != -3` check —
  consistent with classic. Per-surface fog density/color is computed in the
  VS from the record pools + uLevelFogDensity, not on the CPU.
- VAO hygiene: DrawLevelMesh restores mVAOID after each sub-range; attribute
  state written by ApplyShader (VATTR_COLOR/NORMAL) targets the default VAO,
  exactly as standard draws do.
- The scene is built but not drawn when useLevelMesh is on: wall/flat list
  memory + sorting cost remains. Could be gated later for perf, at the cost
  of a bigger RenderBSP change.
