# Handoff C1a: GLSL Library for the Classic DOOM Light Chain

## File created

`wadsrc/static/shaders/glsl/levelmesh_light.glsl` — pure-function GLSL 330 library
(no main, no attributes, no varyings; single uniform `float[512] uDistFogTable`).
No `#version` line in the file, matching the repo convention: none of the existing
`wadsrc/static/shaders/glsl/*.vp`/`*.fp` files carry a `#version` directive; the
engine prepends `#version 330 core` itself (src/common/rendering/gl/gl_shader.cpp:457-459).

Tab indentation, repo header comment style.

## Functions (each with a `// Port of <C++ name> at <file:line>` comment)

| GLSL function | C++ source |
|---|---|
| `int LM_RoundHalfEven(float v)` | `RoundHalfEven` at src/utility/m_round.h:52-58 (llrint, ties-to-even) |
| `int LM_CalcLightLevel(int lightmode, int lightlevel, int rellight, int weapon, int blendfactor)` | `CalcLightLevel` at src/rendering/hwrenderer/scene/hw_lighting.cpp:88-114 |
| `int LM_RescaleLightLevel(int lightlevel, int r_extralight)` | `RescaleLightLevel<true>` at src/rendering/hwrenderer/scene/hw_lighting.h:35-47 |
| `int LM_CalcRelLight(int lightlevel, int orglightlevel, int rel)` | `CalcRelLight` at src/rendering/hwrenderer/scene/hw_walls.cpp:2100-2115 |
| `vec3 LM_CalcLightColor(int lightmode, int light, vec3 pe, int blendfactor)` | `CalcLightColor` at src/rendering/hwrenderer/scene/hw_lighting.cpp:119-141 |
| `float LM_GetFogDensity(int lightmode, int lightlevel, int fogrgb, int fog_a, int sectorfogdensity, int blendfactor, int level_fogdensity, int outsidefogdensity, int outsidefog_rgb, int outsidefog_a, bool no_lightfade)` | `GetFogDensity` at src/rendering/hwrenderer/scene/hw_lighting.cpp:159-203 |

ELightMode int values used: LinearStandard=0, DoomBright=1, Doom=2, DoomDark=3,
DoomLegacy=4, Build=5, ZDoomSoftware=8, DoomSoftware=16 (src/doomtype.h:52-63).

## Signature deviation (supervisor-approved)

`LM_GetFogDensity`'s 3rd parameter is `int fogrgb` (packed 24-bit fog RGB,
i.e. `fogcolor.d & 0xffffff`), NOT the `int fogrgb_is_zero` boolean from the task
prompt. The exact C++ outsidefog branch (hw_lighting.cpp:190) compares the fog RGB
against `outsidefog & 0xffffff`, which is impossible with only the zero-boolean.
Supervisor decision: option (A) exact port — the zero test `fogrgb == 0` is
computed inside the function, and the outsidefog equality check is
`outsidefogdensity != 0 && outsidefog_a != 0xff && fogrgb == outsidefog_rgb`.
`fog_a` is unused by the C++ body (kept for call-site symmetry, same as in C++
where only `fogcolor.d & 0xffffff` is read).

## Porting notes

- C-style `(float)` casts are not valid core GLSL (glslang demands
  GL_NV_explicit_typecast); all promotion points use functional casts
  `float(x)` at the exact same positions. Consistent with existing repo shaders.
- `LM_RoundHalfEven`: `r = floor(v + 0.5)`; if `fract(v) == 0.5` (exact tie) and
  `mod(r, 2.0) == 1.0` (r odd), subtract 1. GLSL `mod(x, y) = x - y*floor(x/y)`
  gives 1.0 for negative odd r, so negative ties round correctly.
- `LM_CalcLightColor` returns 0-255 in a vec3 (not normalized); the
  `clamp(_, 0, 255)` mirrors the C++ `uint8_t` narrowing (a no-op for all
  reachable inputs, since `light` is clamped to 255 upstream).
- `distfogtable[2][256]` is flattened row-major into `uDistFogTable[512]`:
  index `256 + hwClampLight` for lightmode != LinearStandard, else `hwClampLight`.
- `no_lightfade` = `!(Level->flags3 & LEVEL3_NOLIGHTFADE)`.
- C++ `weapon` is bool, parameter is int per task; `!weapon` ported as `weapon == 0`.
- C++ `weaponPureLightLevel` parameter is dead (feature disabled, [Nash] comment
  preserved) and C++ `oldlightmode` local is unused — both omitted.
- int division truncation (C++ and GLSL 330 both truncate toward zero) preserved
  in `20 + (light + rellight - 20) / 5` and `(lightlevel + rellight) / 5`.

## Validation: glslangValidator

glslangValidator has no `#include`, so the library was concatenated into a throwaway
vertex shader at `/tmp/levelmesh_light_test.vert` (header `#version 330` +
`#extension GL_ARB_gpu_shader5 : require` + library + a dummy `main()` that calls
every function) — temp file NOT committed:

```
{ printf '#version 330\n#extension GL_ARB_gpu_shader5 : require\n'; \
  cat wadsrc/static/shaders/glsl/levelmesh_light.glsl; \
  cat <<'EOF'
void main()
{
	int a = LM_RoundHalfEven(1.5);
	int b = LM_CalcLightLevel(2, 128, 5, 0, 0);
	int c = LM_RescaleLightLevel(100, 4);
	int d = LM_CalcRelLight(120, 100, 10);
	vec3 e = LM_CalcLightColor(2, 200, vec3(128.0, 64.0, 32.0), 0);
	float f = LM_GetFogDensity(2, 128, 0, 0, 0, 0, 0, 0, 0, 0, true);
	gl_Position = vec4(float(a + b + c + d) + e.x + f, 0.0, 0.0, 1.0);
}
EOF
} > /tmp/levelmesh_light_test.vert
glslangValidator -S vert /tmp/levelmesh_light_test.vert
```

Output: `/tmp/levelmesh_light_test.vert` with no errors/warnings, exit code 0
(clean). Two iterations needed to get clean: `int3` → `ivec3` (not a GLSL type)
and C-style → functional float casts.

## Build result

`cmake --build build --config RelWithDebInfo --parallel 3` → `[100%] Built target
zdoom`, no warnings, no errors. The new file is picked up by the PK3 repack
(`wadsrc` → static shaders PK3).

## Residual risks / notes for C1b

- `LM_CalcLightColor` returns a 3-component vec3 (alpha=255 is implicit at the
  call site); the C++ `PalEntry` has a=255.
- No unit tests exist; GLSL-vs-C++ bit-exactness (e.g. `192.0 - (192 - lightlevel)
  * 1.87` vs `192.f - (192 - lightlevel)* 1.87f`) is argued equal: int→float
  conversions of values in this range are exact, single multiply/subtract with
  the same IEEE-754 rounding. In-game verification is the project standard.
- `uDistFogTable` must be filled by the host from the `gl_distfog` CVAR callback
  formulas (hw_lighting.cpp:35-57) — that is a C++/binding-side concern, out of
  scope for this library file.
- In-game verification not performed (no display; the light chain is not yet
  wired into a shader — that is a later chunk).
