# C1b-fix: levelmesh.vp fetch mapping → corrected sector state layout + glow

## What changed
`wadsrc/static/shaders/glsl/levelmesh.vp` was written against the OLD (pre-fix)
sector state record. The record was re-laid to the locked spec section 4 layout
(see handoff-b-fix.md). This chunk updates the VS to the new layout and wires the
glow colors. The V formula was VERIFIED correct (unchanged) — see below.

## 1. LMSectorState struct (new fields)
Added to the existing struct:
- `vec2 floorScroll; vec2 ceilScroll;`  (UV offsets, applied in VS)
- `float flags;`                        (bit1 floor ABSLIGHTING, bit2 ceil ABSLIGHTING)
- `vec4 glowFloorColor; float glowFloorHeight;`  (ABGR 0-255 + height)
- `vec4 glowCeilColor; float glowCeilHeight;`    (ABGR 0-255 + height)

## 2. LMFetchSector — the canonical 35-float slot order
The 96-byte record re-packs to 35 float32 slots → 9 float4 texels (36 slots, 1
reserved). THIS IS THE CONTRACT chunk D (the host repack) must implement. Field
order matches the C++ LevelMeshSectorState:

| texel | slots  | fields |
|-------|--------|--------|
| 0 | 0-3   | floorPlane[4] (nx,ny,nz,D) |
| 1 | 4-7   | ceilPlane[4]  (nx,ny,nz,D) |
| 2 | 8-11  | floorScroll[2] ceilScroll[2] |
| 3 | 12-15 | lightlevel planeLight[0] planeLight[1] flags |
| 4 | 16-19 | glowFloorColor[4] (ABGR 0-255) |
| 5 | 20-23 | glowFloorHeight glowCeilColor[3] (ABGR) |
| 6 | 24-27 | glowCeilColor[3] glowCeilHeight cmLightColor[2] |
| 7 | 28-31 | cmLightColor[2] cmBlendFactor cmDesaturation cmFadeColor[1] |
| 8 | 32-35 | cmFadeColor[2] cmFogDensity reserved |

`texZ` is set to the plane's D constant (base TexZ for a flat). The C++ bakes
`vparam` from that same base TexZ, so the two stay consistent. The glow color
vec4 is unpacked as (r,g,b,a) 0-255 where a is the 4th byte of the raw PalEntry
(the ~0u disabled sentinel has a=255; a real white color has a=0).

## 3. V formula — VERIFIED CORRECT (unchanged)
The builder (levelmesh.cpp:483-612) bakes `vparam` per tier using the plane's
BASE TexZ (GetPlaneTexZ). The VS reconstructs:
- `z = tracked.texZ + vparam[tier]`  (tracked plane = front ceiling for middle,
  front floor for upper/lower)
- `v = (referenceZ - z) + vparam[tier]`  where referenceZ is the anchor plane:
  - middle (tier 1): front ceiling
  - upper (tier 0): unpegged ? front floor : back ceiling
  - lower (tier 2): unpegged ? back floor : front floor

This EXACTLY matches the C++ `SetWallCoordinates` (hw_walls.cpp:1083-1200):
`tcs[v] = FloatToTexV(-z + texturetop)` where `texturetop = refHeight + yOffset
+ [pegged: renderHeight - flh]` and the vertex `z` is the tracked plane's height.
The `vparam` offsets (unpegged lower: backFloorTexZ - frontFloorTexZ; pegged
upper: backCeilTexZ - frontFloorTexZ; else 0) are baked by the builder at the
same base TexZ the VS uses. VERIFIED: the VS formula is correct for the standard
(flat) case. No change needed.

## 4. Glow wiring
`main()` now fetches the front sector (`LMFetchPlaneSector(surf.planeRef[0])`)
and sets the glow varyings from the record, matching `sector_t::GetWallGlow`
(p_sectors.cpp:1249-1299): (r,g,b) 0-255 with the height in the 4th channel.
Sentinels: c==0 (texture-glow fallback, rgb==0) and c==~0u (disabled, a byte
set) both emit no glow (height 0). The VS cannot do texture glow, so the
fallback is a no-op (acceptable per spec — texture glow is a separate concern).

## 5. LMZeroSectorState
Initialized the new fields (scrolls 0, flags 0, glow colors 0, glow heights 0).

## 6. Validation
- glslangValidator -S vert /tmp/levelmesh_vp_test2.vert → exit 0, no errors/warnings.
- Build: `cmake --build build --config RelWithDebInfo --parallel 3` → clean, exit 0
  (only the pre-existing a_dynlight.h:266 baseline in other TUs).

## 7. Chunk D must-do (host repack)
The host (D chunk) must re-pack the 96-byte LevelMeshSectorState into the 35-float
slot order in section 2 above before uploading to the buffer texture. The raw
glow uint32 (ABGR PalEntry) expands to 4 floats (r,g,b,a) 0-255; the 9 colormap
bytes expand to 9 floats 0-255; the int16/uint16 fields expand to floats. The
buffer is 9 texels wide per sector (not 6 as the old layout assumed).
