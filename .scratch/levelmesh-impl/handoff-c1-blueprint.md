# GLSL Vertex-Shader Port Blueprint: Classic DOOM Light Chain

This is the exact contract for a GLSL port of the per-surface light chain into the
levelmesh vertex shader. Every C++ expression is quoted verbatim with file:line
citations. The GLSL must be a formula-for-formula translation: int32 on integer
paths, float32 with exact C++ op order on float paths.

---

## 1. THE LIGHT CHAIN FUNCTIONS

### 1.1 `CalcLightLevel` — src/rendering/hwrenderer/scene/hw_lighting.cpp:88-114

```cpp
int CalcLightLevel(ELightMode lightmode, int lightlevel, int rellight, bool weapon, int blendfactor, bool weaponPureLightLevel)
{
	int light;

	if (lightlevel <= 0) return 0;

	bool darklightmode = (isDarkLightMode(lightmode)) || (isSoftwareLighting(lightmode) && blendfactor > 0);

	// [Nash] 14 June 2026 - disabled pure light level temporarily. Will be fixed after UZDoom v5.0
	if (darklightmode && lightlevel < 192 && !weapon)
	{
		if (lightlevel > 100)
		{
			light = RoundHalfEven(192.f - (192 - lightlevel)* 1.87f);
			if (light + rellight < 20)
			{
				light = 20 + (light + rellight - 20) / 5;
			}
			else
			{
				light += rellight;
			}
		}
		else
		{
			light = (lightlevel + rellight) / 5;
		}

	}
	else
	{
		light=lightlevel+rellight;
	}

	// Fake contrast should never turn a positive value into 0.
	return clamp(light, 1, 255);
}
```

**Classification:**
- **INTEGER path** (all int32): the `darklightmode && lightlevel < 192 && !weapon` branch's `light = 20 + (light + rellight - 20) / 5`, the `light = (lightlevel + rellight) / 5` branch, and the `light=lightlevel+rellight` branch. The final `clamp(light, 1, 255)` is int32.
- **FLOAT path** (float32): the single expression `RoundHalfEven(192.f - (192 - lightlevel) * 1.87f)`. The int `lightlevel` is promoted to float in the multiplication `(192 - lightlevel) * 1.87f` (the `192` is int, `192 - lightlevel` is int, then `int * float` → float). The result of `192.f - (float)` is float. `RoundHalfEven` (a.k.a. `llrint`) converts float → int32.
- **Promotion points:** `lightlevel` (int) → float at `(192 - lightlevel) * 1.87f` (the `* 1.87f` triggers int-to-float). Float → int32 at `RoundHalfEven`.

Helper functions used:
- `isDarkLightMode(lightmode)`: `lightmode == ELightMode::Doom || lightmode == ELightMode::DoomDark` (hw_drawinfo.h:368-370)
- `isSoftwareLighting(lightmode)`: `lightmode == ELightMode::ZDoomSoftware || lightmode == ELightMode::DoomSoftware || lightmode == ELightMode::Build` (hw_drawinfo.h:353-355)
- `RoundHalfEven(val)`: `static_cast<int32_t>(std::llrint(val))` (m_round.h:52-58) — round-to-nearest, ties-to-even.
- `ELightMode` enum values: NotSet=-1, LinearStandard=0, DoomBright=1, Doom=2, DoomDark=3, DoomLegacy=4, Build=5, ZDoomSoftware=8, DoomSoftware=16 (doomtype.h:52-63)

---

### 1.2 `GetFogDensity` — src/rendering/hwrenderer/scene/hw_lighting.cpp:159-203

```cpp
float GetFogDensity(FLevelLocals* Level, ELightMode lightmode, int lightlevel, PalEntry fogcolor, int sectorfogdensity, int blendfactor)
{
	float density;

	auto oldlightmode = lightmode;
	if (isSoftwareLighting(lightmode) && blendfactor > 0) lightmode = ELightMode::Doom;

	if (lightmode == ELightMode::DoomLegacy)
	{
		density = Level->fogdensity ? (float)Level->fogdensity : 18;
	}
	else if (sectorfogdensity != 0)
	{
		density = (float)sectorfogdensity;
	}
	else if ((fogcolor.d & 0xffffff) == 0)
	{
		if ((!isDoomSoftwareLighting(lightmode) || blendfactor > 0) && !(Level->flags3 & LEVEL3_NOLIGHTFADE))
		{
			density = distfogtable[lightmode != ELightMode::LinearStandard][hw_ClampLight(lightlevel)];
		}
		else
		{
			density = 0;
		}
	}
	else if (Level->outsidefogdensity != 0 && APART(Level->info->outsidefog) != 0xff && (fogcolor.d & 0xffffff) == (Level->info->outsidefog & 0xffffff))
	{
		density = (float)Level->outsidefogdensity;
	}
	else  if (Level->fogdensity != 0)
	{
		density = (float)Level->fogdensity;
	}
	else if (lightlevel < 248)
	{
		density = (float)clamp<int>(255 - lightlevel, 30, 255);
	}
	else
	{
		density = 0.f;
	}
	return density;
}
```

**Classification:**
- **INTEGER path:** the `clamp<int>(255 - lightlevel, 30, 255)` expression (line 196).
- **FLOAT path:** all the `(float)` casts. The `distfogtable` lookup is a float32 array access.
- **Promotion points:** int → float at every `(float)` cast. The `distfogtable` index `lightmode != ELightMode::LinearStandard` is a bool→int index; `hw_ClampLight(lightlevel)` is an int32 index.

Helper functions used:
- `isDoomSoftwareLighting(lightmode)`: `lightmode == ELightMode::ZDoomSoftware || lightmode == ELightMode::DoomSoftware` (hw_drawinfo.h:363-365)
- `hw_ClampLight(lightlevel)`: `clamp(lightlevel, 0, 255)` (hw_lighting.h:25-27)
- `APART(c)`: `(((c)>>24)&0xff)` (palentry.h:121)
- `LEVEL3_NOLIGHTFADE`: a flag bit on `Level->flags3`.
- `distfogtable[2][256]`: a `static float` array built by the `gl_distfog` CVAR callback (hw_lighting.cpp:35-57).
  - Formula for `distfogtable[0][i]`:
    - i < 164: `(float)((gl_distfog >> 1) + (gl_distfog)*(164 - i) / 164)`
    - 164 ≤ i < 230: `(float)((gl_distfog >> 1) - (gl_distfog >> 1)*(i - 164) / (230 - 164))`
    - else: 0
  - Formula for `distfogtable[1][i]`:
    - i < 128: `6.f + (float)((gl_distfog >> 1) + (gl_distfog)*(128 - i) / 48)`
    - 128 ≤ i < 216: `(216.f - i) / ((216.f - 128.f)) * gl_distfog / 10`
    - else: 0
  - In the GLSL port this is a **uniform buffer** of 256×2 float32 values, rebuilt on `gl_distfog` cvar change.

**Inputs the GLSL port needs (replacing C++ Level/sector state):**
- `lightmode` (int, per-level uniform)
- `lightlevel` (int32)
- `fogcolor` (vec4 or 4 separate int32/uint8 — only `.d & 0xffffff` used, i.e. the RGB components; `fogcolor.d` is the packed ARGB uint32)
- `sectorfogdensity` (int32) — this is `FColormap::FogDensity` (the packed 9-byte colormap's byte 8)
- `blendfactor` (int32) — from the packed colormap byte 3
- `Level->fogdensity` (int, per-level)
- `Level->outsidefogdensity` (int, per-level)
- `Level->flags3` (uint, per-level — specifically the `LEVEL3_NOLIGHTFADE` bit)
- `Level->info->outsidefog` (uint32, per-level — specifically `APART()` and `& 0xffffff`)
- `distfogtable` (uniform float[512], i.e. 256×2)

---

### 1.3 `CheckFog` — src/rendering/hwrenderer/scene/hw_lighting.cpp:215-262

```cpp
bool CheckFog(FLevelLocals* Level, sector_t *frontsector, sector_t *backsector, ELightMode lightmode)
{
	if (frontsector == backsector) return false;

	PalEntry fogcolor = frontsector->Colormap.FadeColor;

	if ((fogcolor.d & 0xffffff) == 0)
	{
		return false;
	}
	else if (fogcolor.a != 0)
	{
	}
	else if (Level->outsidefogdensity != 0 && APART(Level->info->outsidefog) != 0xff && (fogcolor.d & 0xffffff) == (Level->info->outsidefog & 0xffffff))
	{
	}
	else  if (Level->fogdensity!=0 || lightmode == ELightMode::DoomLegacy)
	{
		// case 3: level has fog density set
	}
	else
	{
		if (frontsector->lightlevel >= 248) return false;
	}

	fogcolor = backsector->Colormap.FadeColor;

	if ((fogcolor.d & 0xffffff) == 0)
	{
	}
	else if (Level->outsidefogdensity != 0 && APART(Level->info->outsidefog) != 0xff && (fogcolor.d & 0xffffff) == (Level->info->outsidefog & 0xffffff))
	{
		return false;
	}
	else  if (Level->fogdensity!=0 || lightmode == ELightMode::DoomLegacy)
	{
		return false;
	}
	else
	{
		if (backsector->lightlevel < 248) return false;
	}

	return ((frontsector->GetTexture(sector_t::ceiling)!=skyflatnum ||
			 backsector->GetTexture(sector_t::ceiling)!=skyflatnum));
}
```

**Classification:** pure INTEGER/BOOL logic. No float involved.

**Inputs the GLSL port needs (as record fields):**
- `frontsector_index` (from `planeRef[0]` → sector index)
- `backsector_index` (from `planeRef[2]` → sector index)
- `front_fadecolor_rgb` (vec3 or uint, from frontsector's `LevelMeshSectorState.colormap[5..7]`)
- `front_fadecolor_a` (uint8, unused here except for the branch test `fogcolor.a != 0`)
- `front_lightlevel` (int32, from `LevelMeshSectorState.lightlevel` of front sector)
- `back_fadecolor_rgb` (vec3 or uint, from backsector's state record)
- `back_lightlevel` (int32, from `LevelMeshSectorState.lightlevel` of back sector)
- `front_ceil_is_sky` (bool, from `frontsector->GetTexture(ceiling) == skyflatnum` — baked at build)
- `back_ceil_is_sky` (bool, from `backsector->GetTexture(ceiling) == skyflatnum` — baked at build)
- Per-level: `Level->outsidefogdensity`, `Level->fogdensity`, `Level->info->outsidefog`
- `lightmode` (int)

Note: This function determines whether a wall is a "fog boundary" (drawn as a separate fog-boundary quad). In the GLSL port, this is likely evaluated at build time for static surfaces (the front/back sector fog state is static topology). For dynamic light levels that cross the 248 threshold, a per-frame check is needed.

---

### 1.4 `RescaleLightLevel` — src/rendering/hwrenderer/scene/hw_lighting.h:35-47

```cpp
template<bool doClamp = true>
inline int RescaleLightLevel(int lightlevel) // TODO/tidy: move out of hwrenderer namespace
{
	if constexpr(doClamp)
	{
		//max is needed for negative r_extralight values
		return max(int((clamp(lightlevel, 0, 255) / 255.0) * (255.0 - r_extralight)) + r_extralight, 0);
	}
	else
	{
		//max is needed for negative r_extralight values
		return max(int((max(lightlevel, 0) / 255.0) * (255.0 - r_extralight)) + r_extralight, 0);
	}
}
```

**Classification:**
- **INTEGER path:** the `clamp(lightlevel, 0, 255)` / `max(lightlevel, 0)`, the final `+ r_extralight`, and `max(..., 0)`.
- **FLOAT path:** `clamp(lightlevel, 0, 255) / 255.0` (int → float division), then `* (255.0 - r_extralight)` (float × float), then `int(...)` truncation.
- **Promotion points:** int → float at the `/ 255.0` division. Float → int32 at the `int(...)` truncation (truncation toward zero, which for positive values is floor).
- **Cvar:** `r_extralight` is a global int cvar (`EXTERN_CVAR(Int, r_extralight)`, hw_lighting.h:27).

Note: The default template parameter is `doClamp = true`. The `doClamp = false` variant is not used in the light chain.

---

### 1.5 `CalcRelLight` — src/rendering/hwrenderer/scene/hw_walls.cpp:2100-2115

```cpp
inline int CalcRelLight(int lightlevel, int orglightlevel, int rel)
{
	if (orglightlevel >= 253)			// with the software renderer fake contrast won't be visible above this.
	{
		return 0;
	}
	else if (lightlevel - rel > 256)	// the brighter part of fake contrast will be clamped so also clamp the darker part by the same amount for better looks
	{
		return 256 - lightlevel + rel;
	}
	else
	{
		return rel;
	}
}
```

Also present as `LM_CalcRelLight` at src/common/rendering/levelmesh.cpp:82-93 (identical body).

**Classification:** pure INTEGER path. All int32. No float involved.

- `lightlevel`: the raw (unrescaled) light level from `side_t::GetLightLevel` (or the flat's effective light)
- `orglightlevel`: the raw `frontsector->lightlevel`
- `rel`: the fake-contrast rel value from `side_t::GetLightLevel` (baked as `relSmooth`/`relNonSmooth` in the surface record)

---

### 1.6 `CalcLightColor` — src/rendering/hwrenderer/scene/hw_lighting.cpp:119-141

```cpp
PalEntry CalcLightColor(ELightMode lightmode, int light, PalEntry pe, int blendfactor)
{
	int r,g,b;

	if (blendfactor == 0)
	{
		if (isSoftwareLighting(lightmode))
		{
			return pe;
		}

		r = pe.r * light / 255;
		g = pe.g * light / 255;
		b = pe.b * light / 255;
	}
	else
	{
		int mixlight = light * (255 - blendfactor);

		r = (mixlight + pe.r * blendfactor) / 255;
		g = (mixlight + pe.g * blendfactor) / 255;
		b = (mixlight + pe.b * blendfactor) / 255;
	}
	return PalEntry(255, uint8_t(r), uint8_t(g), uint8_t(b));
}
```

**Classification:** pure INTEGER path. All int32 arithmetic with implicit uint8_t narrowing at the end.
- `pe.r`, `pe.g`, `pe.b` are the `FColormap::LightColor` components (from the 9-byte packed colormap, bytes 0-2).
- `blendfactor` is from the packed colormap byte 3.
- **Palette-index (GPU texture):** The `FColormap::LightColor` acts as a pre-multiplied light color. In the classic path the result is used directly as `vColor` (a vec4 in the shader). It is NOT a palette lookup — it's a multiplicative tint. The `Desaturation` field (byte 4) is used separately for desaturation in the fragment shader.
- **Promotion:** int → uint8 at the return. In GLSL the int32 result is normalized to [0,1] by `/255.0` to produce a vec3 color.

---

### 1.7 `getExtraLight` — src/rendering/hwrenderer/scene/hw_lighting.h:51-53

```cpp
inline	int getExtraLight()
{
	return r_viewpoint.extralight * gl_weaponlight;
}
```

**Classification:** pure INTEGER.
- `r_viewpoint.extralight`: per-viewpoint int (d_player.h:401, set by actor weapon flashes).
- `gl_weaponlight`: global int cvar, default 8 (hw_lighting.cpp:33).
- In the levelmesh port this is a per-view uniform. Flats use the whole value as `rel`; walls add it to the fake-contrast rel.

---

### 1.8 `side_t::GetLightLevel` — src/playsim/p_sectors.cpp:1579-1627

```cpp
int side_t::GetLightLevel (bool foggy, int baselight, int which, bool is3dlight, int *pfakecontrast) const
{
	if (!is3dlight)
	{
		if (Flags & (WALLF_ABSLIGHTING_TIER << which))
		{
			baselight = TierLights[which];
		}
		else if (Flags & WALLF_ABSLIGHTING)
		{
			baselight = Light + TierLights[which];
		}
	}

	if (pfakecontrast != NULL)
	{
		*pfakecontrast = 0;
	}

	if (!foggy || sector->Level->flags3 & LEVEL3_FORCEFAKECONTRAST) // Don't do relative lighting in foggy sectors
	{
		if (!(Flags & WALLF_NOFAKECONTRAST) && r_fakecontrast != 0)
		{
			DVector2 delta = linedef->Delta();
			int rel;
			if (((sector->Level->flags2 & LEVEL2_SMOOTHLIGHTING) || (Flags & WALLF_SMOOTHLIGHTING) || r_fakecontrast == 2) &&
				delta.X != 0)
			{
				rel = RoundHalfUp
					(
						sector->Level->WallHorizLight
						+ fabs(atan(delta.Y / delta.X) / 1.57079)
						* (sector->Level->WallVertLight - sector->Level->WallHorizLight)
					);
			}
			else
			{
				rel = delta.X == 0 ? sector->Level->WallVertLight :
					  delta.Y == 0 ? sector->Level->WallHorizLight : 0;
			}
			if (pfakecontrast != NULL)
			{
				*pfakecontrast = rel;
			}
			else
			{
				baselight += rel;
			}
		}
	}
	if (!is3dlight && !(Flags & WALLF_ABSLIGHTING) && !(Flags & (WALLF_ABSLIGHTING_TIER << which)) && (!foggy || (Flags & WALLF_LIGHT_FOG)))
	{
		baselight += this->Light + this->TierLights[which];
	}
	return baselight;
}
```

**Classification:**
- **INTEGER path:** The ABSLIGHTING branches, the `baselight += this->Light + this->TierLights[which]` add, and the non-smooth rel selection.
- **FLOAT path:** The smooth rel formula: `RoundHalfUp(WallHorizLight + fabs(atan(delta.Y / delta.X) / 1.57079) * (WallVertLight - WallHorizLight))`. The `delta.X`/`delta.Y` are double (DVector2). `atan` is double. `RoundHalfUp` = `static_cast<int32_t>(std::floor(val + 0.5))`.
- **In the GLSL port:** The smooth rel is BAKED at build time into `relSmooth`/`relNonSmooth` (the spec says "no GPU atan"). The `BakeLight` function at levelmesh.cpp:290-299 calls `side->GetLightLevel(false, orglightlevel, which, false, &rel)` with `foggy=false` and stores the resulting rel. The GLSL selects between `relSmooth` and `relNonSmooth` at runtime based on the smooth condition.
- `foggy` is computed at the call site as: `(frontsector->Colormap.FadeColor != 0) || (Level->flags & LEVEL_HASFADETABLE)` (hw_walls.cpp:2140, computed in `HWWall::Process`).
- In the levelmesh builder, `BakeLight` is called with `foggy=false` (levelmesh.cpp:293), meaning the `!foggy` condition is always true at build time. The GLSL port must re-apply the `(!foggy || LEVEL3_FORCEFAKECONTRAST)` gate at runtime.

---

### 1.9 `GetFloorLight` / `GetCeilingLight` — src/playsim/p_sectors.cpp:872-898

```cpp
int GetFloorLight(const sector_t *sector)
{
	if (sector->GetFlags(sector_t::floor) & PLANEF_ABSLIGHTING)
	{
		return sector->GetPlaneLight(sector_t::floor);
	}
	else
	{
		return sector->ClampLight(sector->lightlevel + sector->GetPlaneLight(sector_t::floor));
	}
}

int GetCeilingLight(const sector_t *sector)
{
	if (sector->GetFlags(sector_t::ceiling) & PLANEF_ABSLIGHTING)
	{
		return sector->GetPlaneLight(sector_t::ceiling);
	}
	else
	{
		return sector->ClampLight(sector->lightlevel + sector->GetPlaneLight(sector_t::ceiling));
	}
}
```

**Classification:** pure INTEGER. `ClampLight` = `clamp(level, SHRT_MIN, SHRT_MAX)` (r_defs.h:998-1000) — effectively a no-op for normal light ranges. `GetPlaneLight(pos)` = `planes[pos].Light` (r_defs.h:932-934).

In the GLSL port: `PLANEF_ABSLIGHTING` is a flag bit; `planeLight` is in the sector state record at offset 44 (int32[2]). The VS computes: `ABSLIGHTING ? planeLight[slot] : clamp(lightlevel + planeLight[slot], SHRT_MIN, SHRT_MAX)`.

---

## 2. WHERE THE CHAIN IS INVOKED PER SURFACE

### 2.1 WALL rendering (hw_walls.cpp)

The light chain is invoked in `HWWall::Process` (line ~2140+) and the result stored in the `HWWall` object's `lightlevel` and `rellight` fields. Then `RenderTexturedWall` (line 204) applies them:

**Step 1: Compute orglightlevel and lightlevel (per tier):**

At `hw_walls.cpp:2290` (one-sided mid), `:2365` (two-sided upper), `:2439` (two-sided mid), `:2490` (two-sided upper for 3D-floor blocking), `:2507` (two-sided lower):

```cpp
lightlevel = RescaleLightLevel(seg->sidedef->GetLightLevel(foggy, orglightlevel, side_t::top|mid|bottom, false, &rel));
rellight = CalcRelLight(lightlevel, orglightlevel, rel);
```

Where `orglightlevel = frontsector->lightlevel` (the RAW, unrescaled sector light).

Note: `RescaleLightLevel` is applied to the result of `GetLightLevel` (which already includes the tier Light/TierLights add and the fake-contrast rel add). The `CalcRelLight` then takes the RESCALED lightlevel, the ORIGINAL orglightlevel, and the `rel` value.

**Step 2: In RenderTexturedWall (line 204):**

```cpp
int rel = rellight + getExtraLight();    // line 206
```

Then the call order is:
1. `SetFog(state, di->Level, di->lightmode, lightlevel, rel, di->isFullbrightScene(), &Colormap, RenderStyle == STYLE_Add);` (line 289)
2. `SetColor(state, di->Level, di->lightmode, lightlevel, rel, di->isFullbrightScene(), Colormap, absalpha);` (line 290)

**Step 3: `SetColor` (hw_setcolor.cpp:37-53):**
```cpp
int hwlightlevel = CalcLightLevel(lightmode, sectorlightlevel, rellight, weapon, cm.BlendFactor);
PalEntry pe = CalcLightColor(lightmode, hwlightlevel, cm.LightColor, cm.BlendFactor);
state.SetColorAlpha(pe, alpha, cm.Desaturation);
if (isSoftwareLighting(lightmode)) state.SetSoftLightLevel(hw_ClampLight(sectorlightlevel + rellight), cm.BlendFactor);
else state.SetNoSoftLightLevel();
```

- `sectorlightlevel` = the rescaled light (from step 1)
- `rellight` = `rellight + getExtraLight()` (from step 2)
- `cm` = `HWWall::Colormap` (the FColormap for this wall)
- `weapon` = false for walls
- Result: `hwlightlevel` → `vColor` (via `CalcLightColor`), `cm.Desaturation` → the desaturation uniform
- For software lighting: `SetSoftLightLevel(hw_ClampLight(sectorlightlevel + rellight), cm.BlendFactor)`

**Step 4: `SetFog` (hw_setcolor.cpp:93-160):**
- Calls `GetFogDensity(Level, lightmode, lightlevel, fogcolor, cmap->FogDensity, cmap->BlendFactor)`
- The `lightlevel` here is the RESCALED light (NOT the final `CalcLightLevel` result).
- Sets `fogcolor` and `fogdensity` on the render state.
- If `r_distance_cull_type > 1`: uses cullcolor logic (see below).
- If fogcolor == 0 and Doom/SoftwareLighting mode: calls `SetShaderLight` for view-distance fade.
- `SetShaderLight` computes `lightfactor` and `lightdist` uniforms.
- Skybox: `if (portalState.inskybox) fogdensity += fogdensity / 2;`

**Cullcolor logic (hw_setcolor.cpp:104-113):**
```cpp
if (r_distance_cull_type > 1)
{
    fogdensity = clamp<int>((int)(19200.0 / Level->culldist), 1, 255);
    if (cmap != nullptr && cmap->FadeColor != 0)
    {
        fogcolor = cmap->FadeColor;
        fogcolor.a = 0;
    }
    else
    {
        fogcolor = Level->cullcolor;
    }
}
```
- `Level->cullcolor` is set from `seg->frontsector->Colormap.FadeColor` at BSP traversal time (hw_bsp.cpp:391): `if (seg->frontsector->Colormap.FadeColor != 0) Level->cullcolor = seg->frontsector->Colormap.FadeColor;`
- In the levelmesh port: the cullcolor is the front sector's FadeColor (from the sector state record), or `gl_cullcolor` if no FadeColor. The fogdensity is `clamp<int>((int)(19200.0 / culldist), 1, 255)`.

**Glow logic (hw_walls.cpp:209-215):**
```cpp
if (flags & HWWall::HWF_GLOW)
{
    state.EnableGlow(true);
    state.SetGlowParams(topglowcolor, bottomglowcolor);
    SetGlowPlanes(state, frontsector->ceilingplane, frontsector->floorplane);
}
```
- The glow planes are the frontsector's ceiling/floor planes.
- `topglowcolor`/`bottomglowcolor` come from `sector_t::GetWallGlow` (p_sectors.cpp:1251) — sector's `GlowColor` + `GlowHeight`.
- In the levelmesh port: the sector record carries color+height; the planes come from the sector state record's `ceilNormal/ceilD` and `floorNormal/floorD`. The main.vp already computes `glowdist` from `uGlowTopPlane`/`uGlowBottomPlane` uniforms; the levelmesh VS will emit these as varyings.
- **Walls only** — no flat glow.

**Desaturation application:**
- `cm.Desaturation` is passed to `state.SetColorAlpha(pe, alpha, cm.Desaturation)`.
- In the fragment shader, desaturation is applied to the final color.
- In the levelmesh port: the `Desaturation` value is from the 9-byte packed colormap byte 4. It is emitted as a varying.

**3D-light (band split) variant (hw_walls.cpp:305-325):**
```cpp
int thisll = (*lightlist)[i].caster != nullptr ? RescaleLightLevel(*(*lightlist)[i].p_lightlevel) : lightlevel;
FColormap thiscm;
thiscm.FadeColor = Colormap.FadeColor;
thiscm.FogDensity = Colormap.FogDensity;
CopyFrom3DLight(thiscm, &(*lightlist)[i]);
SetColor(state, di->Level, di->lightmode, thisll, rel, false, thiscm, absalpha);
if (type != RENDERWALL_M2SNF) SetFog(state, di->Level, di->lightmode, thisll, rel, false, &thiscm, RenderStyle == STYLE_Add);
```
- The band's light replaces the wall's own light.
- The colormap is the 3D light's `extra_colormap` with the wall's own `FadeColor`/`FogDensity` preserved.
- `rel` is unchanged (the wall's fake-contrast rel).
- In the levelmesh port: `band.lightRef >= 0` → use the 12-byte light record's lightlevel + colormap; else use the wall's normal chain.

---

### 2.2 FLAT rendering (hw_flats.cpp)

**Step 1: Compute lightlevel:**

For floors (hw_flats.cpp:515):
```cpp
lightlevel = RescaleLightLevel(frontsector->GetFloorLight());
```
For ceilings (hw_flats.cpp:573):
```cpp
lightlevel = RescaleLightLevel(frontsector->GetCeilingLight());
```
For 3D floors (hw_flats.cpp:449):
```cpp
lightlevel = RescaleLightLevel(*light->p_lightlevel);
```

Where `GetFloorLight`/`GetCeilingLight` = `ABSLIGHTING ? planeLight : ClampLight(lightlevel + planeLight)`.

Note: Flats do NOT use `CalcRelLight` (no fake contrast). The `rel` for flats is just `getExtraLight()`.

**Step 2: In DrawFlat (hw_flats.cpp:295):**
```cpp
int rel = getExtraLight();
SetColor(state, di->Level, di->lightmode, lightlevel, rel, di->isFullbrightScene(), Colormap, alpha);
SetFog(state, di->Level, di->lightmode, lightlevel, rel, di->isFullbrightScene(), &Colormap, false);
```

The same `SetColor`/`SetFog` chain as walls, with:
- `sectorlightlevel` = the rescaled flat light
- `rellight` = `getExtraLight()` (no fake contrast)
- `Colormap` = `frontsector->Colormap` (or the 3D light's colormap for FF_FOG)

**3D floor (FF_FOG) variant (hw_flats.cpp:452-462):**
```cpp
if (rover->flags & FF_FOG)
{
    Colormap.LightColor = light->extra_colormap.FadeColor;
    FlatColor = 0xffffffff;
    AddColor = 0;
    TextureFx = nullptr;
}
else
{
    CopyFrom3DLight(Colormap, light);
    FlatColor = plane.model->SpecialColors[plane.isceiling];
    AddColor = plane.model->AdditiveColors[plane.isceiling];
    TextureFx = &plane.model->planes[plane.isceiling].TextureFx;
}
```

---

## 3. THE VARYING SET main.vp needs

### 3.1 Existing varying declarations (from hw_shaderpatcher.cpp:342-365)

The shader patcher generates these `out` declarations for the vertex shader:

| Name | Type | Required Properties |
|------|------|-------------------|
| `vTexCoord` | `vec4` | always |
| `vColor` | `vec4` | always |
| `pixelpos` | `vec4` | Simple (non-simple) |
| `glowdist` | `vec3` | Simple (non-simple) |
| `gradientdist` | `vec3` | Simple (non-simple) |
| `vWorldNormal` | `vec4` | Simple (non-simple) |
| `vEyeNormal` | `vec4` | Simple (non-simple) |
| `ClipDistanceA` | `vec4` | HasClipDistance |
| `ClipDistanceB` | `vec4` | HasClipDistance |
| `vLightmap` | `vec3` | Simple (non-simple) |

### 3.2 Attribute layout (VInput, hw_shaderpatcher.cpp:337-347)

| Name | Type | Maps to vertex field |
|------|------|---------------------|
| `aPosition` | `vec4` | `LevelMeshVertex.x, y, slotA(z), 1.0` |
| `aTexCoord` | `vec2` | `LevelMeshVertex.u, slotB(v)` |
| `aColor` | `vec4` | per-vertex color (unused for levelmesh) |
| `aVertex2` | `vec4` | second position (for interpolation) |
| `aNormal` | `vec4` | `LevelMeshVertex` normal (from sector state) |
| `aNormal2` | `vec4` | second normal |
| `aLightmap` | `vec3` | `LevelMeshVertex.lmu, lmv, 0` |
| `aBoneWeight` | `vec4` | unused for levelmesh |
| `aBoneSelector` | `uvec4` | unused for levelmesh |

For the levelmesh vertex pool (32-byte `LevelMeshVertex`):
- `aPosition.xyz` = `(x, y, slotA)` where slotA is baked z (static) or vparam (dynamic)
- `aTexCoord.xy` = `(u, slotB)` where slotB is baked v (static) or 0 (dynamic)
- `aLightmap.xy` = `(lmu, lmv)`
- `surfaceIndex` (uint32 at byte 28) is used to fetch the `LevelMeshSurface` record

### 3.3 How main.vp computes worldpos/clippos

From main.vp lines 35-42:
```glsl
vec4 worldcoord = ModelMatrix * mix(parmPosition, aVertex2, uInterpolationFactor);
vec4 eyeCoordPos = ViewMatrix * worldcoord;
gl_Position = ProjectionMatrix * eyeCoordPos;
```

For the levelmesh port: `ModelMatrix` = identity (world-space vertices), `parmPosition` = the vertex's world position (z from the sector state record for dynamic surfaces, or baked for static). `aVertex2`/`uInterpolationFactor` are not used (no skeletal animation).

### 3.4 Varyings the levelmesh VS will ADD

Per the spec (section 6), the levelmesh VS emits all existing varyings PLUS:

| Proposed GLSL name | Type | Source / meaning |
|-------------------|------|-----------------|
| `vLightLevel` | `float` | The `CalcLightLevel` result (0-255), used by main.fp's Doom lighting equation via the existing `uLightLevel` pattern |
| `vFogDensity` | `float` | The `GetFogDensity` result |
| `vFogColor` | `vec4` | The fog color (RGBA; A=0 for regular fog) |
| `vLightFactor` | `float` | The `SetShaderLight` lightfactor (view-distance fade) |
| `vLightDist` | `float` | The `SetShaderLight` lightdist |
| `vDesaturation` | `float` | `cm.Desaturation` (0-31), promoted from the packed colormap |
| `vFogEnabled` | `int` | 0=no fog; +gl_fogmode for black fog; -gl_fogmode for non-black fog (see below) |
| `vGlowTopPlane` | `vec4` | Frontsector ceiling plane (n.x, n.z, n.y, D) — note the xz/y swap for DOOM's coordinate system |
| `vGlowBottomPlane` | `vec4` | Frontsector floor plane (n.x, n.z, n.y, D) |
| `vGlowTopColor` | `vec4` | Top glow color (vec4, A=height) |
| `vGlowBottomColor` | `vec4` | Bottom glow color (vec4, A=height) |

Total: ≈ 12 vec4 slots (48 float units), within GL33's 128-varying limit.

**FogEnabled encoding** (from vk_renderstate.cpp:356-372, as described in the spec):
- 0 = no fog
- +`gl_fogmode` (1 = view-depth fade, 2 = true distance) for black fogcolor
- -`gl_fogmode` for non-black fogcolor

The main.fp branches key off `uFogEnabled` (positive = distance fog active, negative = color fog active, zero = no fog).

---

## 4. THE INPUT RECORD LAYOUTS

### 4.1 `LevelMeshSurface` (64 bytes) — src/common/rendering/levelmesh.h:109-133

```
Offset  Type         Field              Notes
------  -----------  -----------------  ------------------------------------------------
0       uint32_t     textureIndex       FTextureID::GetIndex, or 0xFFFFFFFF
4       uint32_t     regionIndex
8       uint32_t     planeRef[4]        [0]=front floor, [1]=front ceil, [2]=back floor, [3]=back ceil
24      uint32_t     lightRef           sectorIndex<<2 | slot (0=floor,1=ceiling,2=wall)
28      uint32_t     flags              LMF_* flag bits
32      float        vparam[4]          [0] lower unpegged offset, [1] middle window, [2] upper window, [3] reserved
48      int16_t      light              baked static light level (RescaleLightLevel applied)
50      int16_t      tierLight          TierLights[which]
52      int16_t      relSmooth          fake-contrast rel (smooth formula)
54      int16_t      relNonSmooth       fake-contrast rel (non-smooth formula)
56      int16_t      lightlistRef       -1 for statics, index into 12-byte light buffer
58      uint16_t     bandOffset         offset into per-level band table
60      uint16_t     bandCount
62      uint8_t      skyScrollKind      0=sky1, 1=sky2, 2=mist
63      uint8_t      skyScrollKind2     doublesky second layer
```

### 4.2 `ESurfaceFlags` (LMF_*) — src/common/rendering/levelmesh.h:42-60

| Flag | Bit | Value |
|------|-----|-------|
| `LMF_DYNAMIC` | 0 | 1<<0 = 1: dynamic surface (slotA holds vparam) |
| `LMF_SKY` | 1 | 1<<1 = 2: sky surface |
| `LMF_LIGHTMAP` | 2 | 1<<2 = 4: has lightmap UVs |
| `LMF_ISFLAT` | 3 | 1<<3 = 8: a flat (floor/ceiling/3D-floor) |
| `LMF_OPENING_UPPER` | 4 | 1<<4 = 16: upper window present |
| `LMF_OPENING_MIDDLE` | 5 | 1<<5 = 32: middle window present |
| `LMF_OPENING_LOWER` | 6 | 1<<6 = 64: lower window present |
| `LMF_UNPEGGEDLOWER` | 7 | 1<<7 = 128: lower wall does not track front floor |
| `LMF_TIER` | 8-9 | 3<<8 = 0x300: tier 0=upper, 1=middle, 2=lower |
| `LMF_POLYOBJ` | 10 | 1<<10 = 1024: mid wall of a polyobject line |
| `LMF_3DFLOOR` | 12 | 1<<12 = 4096: surface is a 3D floor |
| `LMF_LOADDYN` | 0 | alias for LMF_DYNAMIC |
| `LMF_FAKECONTRAST` | 13 | 1<<13 = 8192: fake-contrast rel pair baked |

### 4.3 `LevelMeshSectorState` (96 bytes) — src/common/rendering/levelmesh_state.h:38-63

```
Offset  Type         Field              Notes
------  -----------  -----------------  ------------------------------------------------
0       float[3]     floorNormal        sec->floorplane normal (nx, ny, nz)
12      float        floorD             sec->floorplane D
16      float[3]     ceilNormal         sec->ceilingplane normal (nx, ny, nz)
28      float        ceilD              sec->ceilingplane D
32      float        floorTexZ          interpolated floor TexZ
36      float        ceilTexZ           interpolated ceiling TexZ
40      int32_t      lightlevel         raw sec->lightlevel
44      int32_t[2]   planeLight[2]      [0] floor, [1] ceiling plane Light offset
52      uint8_t[9]   colormap           packed FColormap (see below)
61      uint8_t[35]  reserved           zero
```

**Packed 9-byte FColormap layout** (levelmesh_state.cpp:42-52, `LM_PackColormap`):
```
Byte 0: cm.LightColor.r
Byte 1: cm.LightColor.g
Byte 2: cm.LightColor.b
Byte 3: cm.BlendFactor
Byte 4: cm.Desaturation
Byte 5: cm.FadeColor.r
Byte 6: cm.FadeColor.g
Byte 7: cm.FadeColor.b
Byte 8: (uint8_t)cm.FogDensity
```

Note: `FColormap::LightColor.a` is the saturation (0=full, 31=b/w) but is NOT stored in the 9-byte pack. The GLSL port does not use it (the HW path never reads saturation from the colormap's alpha). `FColormap::FadeColor.a` is `fadedensity>>1` — also not stored. `FogDensity` is a uint16 in C++ but only the low byte is stored (UDMF clamps to 0-256).

### 4.4 `LevelMeshLightState` (12 bytes) — src/common/rendering/levelmesh_state.h:77-84

```
Offset  Type         Field              Notes
------  -----------  -----------------  ------------------------------------------------
0       int16_t      lightlevel         live 3D light level (*p_lightlevel)
2       uint8_t[9]   colormap           packed FColormap (same 9-byte layout)
11      uint8_t      reserved           zero
```

Packed with `#pragma pack(push, 1)` — no padding.

### 4.5 `planeRef` / `lightRef` encoding (levelmesh.h:69-79)

- `planeRef`: `sectorIndex << 1 | planeBit` where planeBit 0=floor, 1=ceiling. Flats store `LM_PLANEREF_NONE` (=0xFFFFFFFF) in slots [2]/[3].
- `lightRef`: `sectorIndex << 2 | slot` where slot 0=floor, 1=ceiling, 2=wall. Resolved through heightsec transfer-light at build time.

---

## 5. SKY + WARP

### 5.1 Sky scroll formula (spec section 6, r_sky.cpp:146-148)

The per-level sky scroll values are computed each frame:
```cpp
double mstime = ((Level->LocalWorldTimer + ticFrac) * 1000.0) / TICRATE;
Level->hw_sky1pos = (float)(fmod((mstime * Level->skyspeed1), 1024.) * (90. / 256.));
Level->hw_sky2pos = (float)(fmod((mstime * Level->skyspeed2), 1024.) * (90. / 256.));
Level->hw_skymistpos = (float)(fmod((mstime * Level->skymistspeed), 1024.) * (90. / 256.));
```

These are stored as `float hw_sky1pos, hw_sky2pos, hw_skymistpos` in `FLevelLocals` (g_levellocals.h:686).

### 5.2 Sky VS scroll (spec section 6, "Sky: ordinary geometry" contract)

From the spec:
> VS: `u += levelSkyPos[skyScrollKind]` (+ second layer for doublesky), with `levelSkyPos = (hw_sky1pos, hw_sky2pos, hw_skymistpos)` a per-level per-frame uniform triple.

The surface record's `skyScrollKind` field (byte 62) selects which of the three values to add. For doublesky, `skyScrollKind2` (byte 63) drives the second layer.

### 5.3 Sky fadecolor rule (spec section 6, hw_sky.cpp:113-123)

From the spec:
> Sky fadecolor rule (hw_sky.cpp:116-126) as a VS branch on sky surfaces: `r_distance_cull_type > 1` → `FadeColor > 0 ? FadeColor : gl_cullcolor`; else `Level->skyfog > 0` → the sector FadeColor; else none.

The C++ source (hw_sky.cpp:113-123):
```cpp
if (r_distance_cull_type > 1)
{
    fadecolor = FadeColor > 0 ? FadeColor : PalEntry(gl_cullcolor);
}
else if (di->Level->skyfog > 0)
{
    fadecolor = FadeColor;
    fadecolor.a = 0;
}
else fadecolor = 0;
```

### 5.4 Load-dynamic warp (spec section 3)

For dynamic surfaces (`LMF_LOADDYN` flag), the vertex's `slotA` holds a `vparam` and the VS computes:
```
z = TexZ + vparam
```
Where `TexZ` comes from the sector state record (`floorTexZ` or `ceilTexZ` depending on which plane the surface tracks). The `vparam` encodes:
- For unpegged lower walls: `backFloorTexZ - frontFloorTexZ` (baked at build)
- For other dynamic surfaces: 0 (z = TexZ directly)
- The actual z is: `z = planeTexZ + vparam` where `planeTexZ` is from the sector state record.

For load-dynamic walls with unpegged textures, the formula is:
```
z = frontFloorTexZ + vparam[0]
```
where `vparam[0]` was baked as `backFloorTexZ - frontFloorTexZ` at build time, so the wall tracks the front floor while maintaining its offset from the back floor.

---

## 6. FOG/FADECOLOR UNIFORMS

### 6.1 Per-level uniforms the VS needs

| Name | Type | Source (C++ location) | Notes |
|------|------|----------------------|-------|
| `uLevelSkyPos` | `vec3` | `FLevelLocals::hw_sky1pos, hw_sky2pos, hw_skymistpos` (g_levellocals.h:686) | Per-level per-frame; advanced by R_UpdateSky (r_sky.cpp:146-148) |
| `uLightMode` | `int` | `di->lightmode` (HWDrawInfo, from `getRealLightmode` in g_level.cpp:151) | Per-level |
| `uFogDensity` (level) | `float` | `FLevelLocals::fogdensity` | Per-level, for `GetFogDensity` |
| `uOutsideFogDensity` | `float` | `FLevelLocals::outsidefogdensity` | Per-level |
| `uOutsideFog` | `uint` | `FLevelLocals::info->outsidefog` | Per-level; `APART()` and `& 0xffffff` used |
| `uFlags3` | `uint` | `FLevelLocals::flags3` | Per-level; `LEVEL3_NOLIGHTFADE`, `LEVEL3_FORCEFAKECONTRAST` bits |
| `uFlags2` | `uint` | `FLevelLocals::flags2` | Per-level; `LEVEL2_SMOOTHLIGHTING` bit |
| `uFlags` | `uint` | `FLevelLocals::flags` | Per-level; `LEVEL_HASFADETABLE` bit |
| `uSkyfog` | `int` | `FLevelLocals::skyfog` | Per-level; sky fadecolor rule |
| `uDistFogTable` | `float[512]` | `distfogtable[2][256]` (hw_lighting.cpp:32) | Rebuilt on `gl_distfog` change |
| `uCullDist` | `float` | `FLevelLocals::culldist` | Per-level per-frame; for cullcolor fogdensity |
| `uGlCullColor` | `vec4` | `gl_cullcolor` cvar (r_utility.cpp:174) | Global; fallback cullcolor |
| `uRDistanceCullType` | `int` | `r_distance_cull_type` cvar | Global; >1 enables cullcolor fog |
| `uGlFogMode` | `int` | `gl_fogmode` cvar | Global; fog mode 0/1/2 |
| `uRVisibility` | `float` | `r_visibility` cvar | Global; for SetShaderLight |
| `uRExtralight` | `int` | `r_extralight` cvar | Global; for RescaleLightLevel |
| `uWeaponLight` | `int` | `r_viewpoint.extralight * gl_weaponlight` | Per-view; for getExtraLight() |
| `uRFakeContrast` | `int` | `r_fakecontrast` cvar | Global; 0=off, 1=classic, 2=smooth |
| `uWallHorizLight` | `int` | `FLevelLocals::WallHorizLight` | Per-level; smooth fake-contrast |
| `uWallVertLight` | `int` | `FLevelLocals::WallVertLight` | Per-level; smooth fake-contrast |
| `uInsybox` | `bool` | `portalState.inskybox` | Per-draw; skybox fog boost ×1.5 |
| `uSectorStateRing` | `samplerBuffer` | `FLevelMeshState::sectorRing` | 96 B × sectorCount × HW_MAX_PIPELINE_BUFFERS |
| `uLightState` | `samplerBuffer` | `FLevelMeshState::lightData` | 12 B × lightCount |
| `uSectorStateSlot` | `int` | frame's ring slot index | Selects the current frame's sector state copy |

### 6.2 Per-view uniform

| Name | Type | Source |
|------|------|--------|
| `uExtraLight` | `int` | `r_viewpoint.extralight * gl_weaponlight` |

---

## SUMMARY: EXACT CALL ORDER AND PROMOTED VALUES

For a WALL surface:
1. `orglightlevel = frontsector->lightlevel` (raw, from sector state record byte 40)
2. `ll = GetLightLevel(foggy, orglightlevel, tier, false, &rel)` (uses side's Light/TierLights, ABSLIGHTING, fake-contrast)
3. `lightlevel = RescaleLightLevel(ll)` (int→float→int with r_extralight)
4. `rellight = CalcRelLight(ll, orglightlevel, rel)` (pure int)
5. `rel = rellight + getExtraLight()` (int)
6. `hwlightlevel = CalcLightLevel(lightmode, lightlevel, rel, false, cm.BlendFactor)` (int+float mixed)
7. `vColor = CalcLightColor(lightmode, hwlightlevel, cm.LightColor, cm.BlendFactor)` (int → vec3)
8. `vFogDensity = GetFogDensity(Level, lightmode, lightlevel, cm.FadeColor, cm.FogDensity, cm.BlendFactor)` (int → float)
9. `vDesaturation = cm.Desaturation` (int)
10. `vGlowTopPlane/vGlowBottomPlane = frontsector's ceiling/floor planes` (float)
11. `vFogEnabled` = encoded from fogcolor and gl_fogmode

For a FLAT surface:
1. `lightlevel = RescaleLightLevel(GetFloorLight/GetCeilingLight)` (int)
2. `rel = getExtraLight()` (int, no fake contrast)
3. Steps 6-11 same as wall (with `rel` instead of `rellight+getExtraLight()`)

The promoted values to emit as varyings:
- `vColor` (vec4) — from step 7
- `vLightLevel` (float) — `hwlightlevel` from step 6 (for main.fp's Doom equation)
- `vFogDensity` (float) — from step 8
- `vFogColor` (vec4) — from step 8
- `vFogEnabled` (int) — from step 11
- `vLightFactor` (float) — from SetShaderLight (only when black-fog view-distance fade active)
- `vLightDist` (float) — from SetShaderLight
- `vDesaturation` (float) — from step 9
- `vGlowTopPlane` (vec4) — from step 10
- `vGlowBottomPlane` (vec4) — from step 10
- `vGlowTopColor` (vec4) — from sector's glow state
- `vGlowBottomColor` (vec4) — from sector's glow state

---

## START HERE

The first file to open is `src/rendering/hwrenderer/scene/hw_lighting.cpp` (272 lines) — it contains the three core functions (`CalcLightLevel`, `CalcLightColor`, `GetFogDensity`) that form the heart of the port. The header `src/rendering/hwrenderer/scene/hw_lighting.h` has `RescaleLightLevel` and `getExtraLight`. The wall/flat call sites in `hw_walls.cpp:2290` and `hw_flats.cpp:515` show the exact call order. The record layouts in `src/common/rendering/levelmesh.h` and `levelmesh_state.h` define what the VS reads.
