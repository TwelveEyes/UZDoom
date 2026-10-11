# 03: First GL33 draw (static + load-dynamic)

**Spec:** `.scratch/levelmesh-rendering/spec.md` §6 (lighting/shading — the bit-exact light-chain
port) + §7 (draw organization — frame build, sub-range table, pass order, GL33 path). Detail:
`.scratch/levelmesh-rendering/ticket-breakup/digest-05-06.md` (port list, fog/cullcolor/glow, sky)
and `digest-07-08.md` (frame-build placement, the static sub-range table, pass order, GL33 objects)
+ `digest-slices-proposal.md` (slice 03).

**What to build:** `gl_uselevelmesh 1` (path selection at level setup; mid-game toggle applies at
the next map load; cvar 0 = classic, unchanged) draws a whole non-portal level on GL33. The
levelmesh vertex shader carries the bit-exact port of the classic per-surface light chain — the
spec §6 port list: effective light → `RescaleLightLevel` → `CalcRelLight` → `CalcLightLevel` →
`CalcLightColor` → fog/cullcolor/glow; int32 on the integer paths, the C++ op order in float32 on
the float paths, C++ staying the single source of truth — plus the VS warp of load-dynamic
surfaces (scroll/transdoor/alpha) from the sector state buffer, and sky scroll
(`u += levelSkyPos[skyScrollKind]`) + the sky fadecolor rule. The `main.fp` adaptation promotes
the per-draw light/fog/glow/desat values to per-vertex varyings; all per-pixel fragment math stays
untouched. The sector state ring (96-byte records, `HW_MAX_PIPELINE_BUFFERS` slots) is
full-copied every frame (ADR 0003) and every surface fetches its sector's record for shading —
light thinkers mutate sectors without moving geometry. The GL33 backend objects: the vertex pool
as one VBO + one VAO per region, record buffers as `samplerBuffer` (GL 3.3 core has no SSBOs),
existing `FRenderState` machinery. The frame build lands at the classic render-view site inside
the interpolation window: snapshot pack → normal-view cull walk over the static per-(region,
texture, bandIndex) sub-range table with per-sub-range world-AABB frustum cull → compact per-slot
culled draw list. Per-view sprite gather walks the region's actor list (built at load) and feeds
survivors into the EXISTING sprite batch path unchanged. Pass order is 1:1 with the classic
`RenderScene` (sub-ranges → models → decals → SSAO → translucent-border + translucent
`DrawSorted`), with blended level surfaces fed as per-surface entries into the classic quadtree
plane sort. A/B: zero-tolerance GL33 pair on the curated static subset (scanner-confirmed: no
portals, no 3D floors, no scroll/transdoor/alpha sectors — candidates DOOM2 MAP01, MAP27) with
static tick lists restricted to non-movement frames; first bring-up records + human review of diff
clusters into the committed allowlist (ADR 0004). In-game walk with cvar 1: sky scrolls, fog,
light, sprites/models/decals present.

**Blocked by:** 02 (consumes the pool, IBOs, surface records, regions, record layouts).

**Status:** in-progress (draw pass at parity on the static DOOM2 set; allowlist review + in-game walk open)

- [ ] Curation-set A/B GL33 zero-tolerance (static tick lists restricted to non-movement frames); diff clusters recorded at first bring-up and human-reviewed into the committed known-diff allowlist (ADR 0004).
- [x] VS emits all `main.vp` fragment varyings + the promoted per-surface values; light chain formula-for-formula (int32 on the integer paths, C++ op order in float32 on the float paths) with C++ as the source of truth; every surface fetches the sector state record for shading.
- [ ] Frame build runs inside the interpolation window (snapshot pack → cull walk → slot fill); per-frame full copy byte-identical across runs (ADR 0003).
- [x] Frame-build window added to the EXISTING `r_perflog` — extended, not created.
- [x] 3D-light state buffer (12 B/light: live lightlevel + extra colormap) packed into the frame record for 05 to consume.
- [ ] cvar 0 = classic, unchanged; mid-game toggle applies at the next map load; in-game walk with cvar 1 shows sky scroll, fog, light, and sprites/models/decals.

**Note:**
1. GL33-first is the user decision: synchronous, no worker, reuses the existing render-state machinery; the data layer stays backend-neutral so Vulkan (ticket 09) attaches with the same GLSL.
2. Largest slice of the effort (~3–4k lines) — kept whole by user decision because the shader-half/frame-build-half seam admits no independent verification.

## Comments

- 2026-10-11 (sessions 3-7; detail in `../ticket03-drawpass-diagnosis.md`, commits 766f390, 783d66b,
  59c1e41): draw pass brought to parity. Root causes fixed: load-dynamic misdetection (alpha > 0),
  VATTR_TEXCOORD never enabled, light chain vs classic SetColor/SetFog, live (not baked) light
  ingredients, HWWall::Process / DoTexture / DoMidTexture port, mirrored cull yaw, back-face
  culling, per-view re-cull, flat triangulation port, gl_seamless splits, frame animation.
  Static A/B: DOOM2 MAP07 + MAP31 byte-identical, MAP01 4 px; demo A/B MAP01 worst tick 290 px.
  The remaining MAP01 junction pixels need the ADR 0004 human allowlist review
  (`abtest.py record`). Open checkboxes: the allowlist review, frame-build byte-identity proof,
  in-game walk.
- Scope moved out: dynamic lights -> 12 (clustered forward+; `r_dynlights 0` pinned in A/B);
  texture swaps, wall/flat UV transforms (MAP30), material shader variants -> 13; OOB-camera cull
  difference (MAP23 demo) -> 07. Translucent walls/flats, sky walls, missing-texture fills and
  polyobject walls stay on the classic lists, which still run on the levelmesh path.
- Smoke policy (user, 2026-10-11): a clean build is the per-change smoke; A/B only on request.
