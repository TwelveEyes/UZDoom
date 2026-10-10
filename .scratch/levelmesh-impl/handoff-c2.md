# Handoff — Chunk C2: GL effect shader wiring (DONE, 2026-10-09)

## Status: COMPLETE
C2 timed out during final verification; the wiring was already complete and sound.
Parent finished the fp validation, fixed a real bug, re-validated, and closed C2.

## What C2 did (6 files)
1. **hw_renderstate.h** — added `EFF_LEVELMESH` to `ERenderEffect` (before MAX_EFFECTS).
2. **textures.h** — added `EFFSHADER_LevelMesh` to `AllShaderIndex` (before ALLSHADER_COUNT).
3. **hw_shaderpatcher.h** — added `const char *prelude` + `bool glonly` to `FEffectShader`
   (trailing fields; existing entries default to nullptr/false via aggregate init).
4. **hw_shaderpatcher.cpp** — registered the levelmesh effect:
   `{ "levelmesh", "shaders/glsl/levelmesh.vp", "shaders/glsl/levelmesh.fp",
     "shaders/glsl/func_normal.fp", "shaders/glsl/material_normal.fp",
     "#define NO_ALPHATEST\n", "shaders/glsl/levelmesh_light.glsl", true }`.
   Also added `0, //EFFSHADER_LevelMesh` to the requiredProperties array.
5. **gl_shader.cpp** — two changes:
   - `FShader::Load`: a leading `#` on `vert_prog_lump` marks inline source (mirrors the
     existing fragment-proc `#` convention). Otherwise it's a lump name.
   - `FShaderCollection::CompileNextShader` (effect case): if `prelude != nullptr`, load the
     prelude lump, prefix it with `#` (inline marker), and concatenate it before the vp source.
6. **vk_shader.cpp** — `CompileNextShader` (effect case): skip `glonly` entries (levelmesh VP
   uses a bare scalar uniform the Vulkan GLSL subset forbids; Vulkan path is a later ticket).
   `LoadVertShader`: same leading-`#` inline-source convention.

## Parent fix (bug found during validation)
- **levelmesh.vp**: `out int vFogEnabled` → `flat out int vFogEnabled`.
  The fp declares `flat in int vFogEnabled`. An int varying must be `flat` in BOTH stages;
  without the matching `flat` on the VS out, GL would fail at link time. Real runtime bug.

## Validation
- **Build**: `cmake --build build --config RelWithDebInfo --parallel 3` — clean, no warnings.
- **VS**: glslangValidator clean at both 330 core and 430 core
  (prelude = engine pre_placeholder + levelmesh_light.glsl + levelmesh.vp).
- **FP**: glslangValidator clean at both 330 core (NUM_UBO 32/8) and 430 core (SSBO).
  (prelude = ViewpointUBO + i_data + generated inputs + levelmesh.fp + func_normal.fp +
  material_normal.fp).
- Note: glslangValidator validates standalone; the real engine prepends its own prelude. The
  standalone prelude was constructed to match gl_shader.cpp's i_data + ViewpointUBO exactly.
  Chunk F's in-game bring-up is the final confirmation.

## Carried into Chunk F (A/B bring-up)
- The levelmesh effect is registered but not yet DRAWN by any backend. Chunk E builds the
  vertex/index buffers; Chunk D builds the backend draw state; Chunk F wires the actual
  draw call that selects EFF_LEVELMESH and the cvar to toggle it.
- The `glonly` flag means Vulkan users get no levelmesh path yet (expected; GL33 first).
- In-game visual verification of the raytraced frame is still outstanding (see scratchpad).
