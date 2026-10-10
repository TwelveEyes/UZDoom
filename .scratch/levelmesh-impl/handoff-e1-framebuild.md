# Handoff E1 — chunk E part 1: cvar, F-level frame build, D_Render integration, GL upload seam, perflog

Status: COMPLETE. Implements ticket 03 chunk E1 (first half of E): the
cvar plumbing, the backend-neutral per-frame cull walk producing the compact
draw list, its D_Render integration inside the interpolation window, the
DFrameBuffer GL upload seam, and the r_perflog extension. Not committed
(per task).

Build result: `cmake --build build --config RelWithDebInfo --parallel 3`
from repo root → `[100%] Built target zdoom`, exit 0. All modified TUs were
force-recompiled fresh with ZERO warnings (only the pre-existing tree
baselines remain: a_dynlight.h:266 anon-enum, d_net.cpp unsequenced/
sign-compare, d_main.cpp:1176 sign-compare — all present before this change).

## What landed

### A) cvar + per-level flag
- The `gl_uselevelmesh` Bool cvar already existed from the A/B tooling chunk
  in `src/common/rendering/hwrenderer/data/hw_cvars.cpp` (line ~50, with a
  "wired in ticket 03" comment). No second definition was added; the comment
  there was updated to describe the latching behavior. (A duplicate CVAR in
  hw_drawinfo.cpp caused a multiple-definition link error and was removed.)
- `FLevelLocals::useLevelMesh` (bool, default false) added in
  `src/g_levellocals.h` next to `levelMeshData`.
- Latched at level setup: `src/maploader/maploader.cpp`, right after
  `screen->SetLevelMeshData(...)` (~line 3267): `Level->useLevelMesh =
  gl_uselevelmesh != 0;` with an EXTERN_CVAR at the top of the file.
- Cleared in `FLevelLocals::ClearLevelData` (`src/p_setup.cpp` ~line 385).
- Mid-game toggle applies at next map load, as specified.

### B) new module: backend-neutral frame build
New files (both GPL banner + section headers per CONTRIBUTING template):
- `src/common/rendering/levelmesh_frame.h`
- `src/common/rendering/levelmesh_frame.cpp`
- registered in `src/CMakeLists.txt` after `common/rendering/levelmesh_state.cpp`.

Contents:
- `struct LevelMeshDrawEntry { uint32_t regionIndex; uint32_t texRangeIndex; }`
  — the compact entry. E2 resolves it via `mesh.regions[r].texRangeOffset/Count`
  + `mesh.texRanges[texRangeIndex]` to get the IBO span + texture.
- `struct FLevelMeshFrame` (member of FLevelLocals, `levelMeshFrame`):
  - `TArray<LevelMeshDrawEntry> drawList`, `uint32_t bound`.
  - `Prepare(const FLevelMesh&)`: static bound = sum of all regions'
    texRangeCounts; idempotent for an unchanged mesh (FLevelLocals is refilled
    in place across map loads, so this handles re-size); `Reserve()` makes
    Build allocation-free.
  - `Build(const FLevelMesh&, const float planes[24]) -> uint32_t`: self-Prepares
    lazily (chosen over calling Prepare from the GL seam: keeps the seam
    trivially no-op on backends without a mesh and keeps the frame object
    self-contained). Walks every region's texRange entries in order, tests each
    2D world AABB against the six frustum planes, appends survivors, clamps the
    array to the count. No allocation after first Build for a given mesh.
  - `EntryCount()` / `Entries()` accessors for E2.
  - `static int NextSlot(uint64_t &counter)` = `counter++ % HW_MAX_PIPELINE_BUFFERS`.
- `void LevelMesh_CalcFrustumPlanes(player_t *pl, float out[24])` — NOTE: the
  type is `player_t` (d_player.h), not "FPlayer" as written in the chunk plan.
  Derivation (documented in code):
  - view matrix = exact HWDrawInfo::SetViewMatrix with mult=1: rotate
    roll, pitch, yaw; translate(vx, -vz*stretch, -vy); scale(-1, stretch, 1).
    Pitch uses the R_SetupFrame pixelstretch-scaled derivation
    (angy/alen → asin); yaw = 270 - mo.Angles.Yaw.Degrees() (the classic
    SetViewAngle offset); position = mo.Pos() + pl->viewz.
  - projection = mono path of VREyeInfo::GetProjection: `perspective(fovy,
    ratio, 5.f, 65536.f)` where ratio/fovratio come from r_viewwindow exactly
    like hw_entrypoint.cpp's main view, and fovy = 2*atan(tan(fov/2)/fovratio).
    fov = `pl->camera`'s (or mo's fallback) GetFOV(I_GetTimeFrac()) with the
    R_SetFOV 5–170 clamp. zNear/zFar are local constants mirroring
    DFrameBuffer::GetZNear/GetZFar to keep the header light (no v_video.h dep).
  - planes = row extraction from VP=P*V (OpenGL convention, clip z in [-1,1]):
    near R3+R4, far R3-R4, left R3+R1, right R3-R1, bottom R3+R2, top R3-R2.
  - null player/mo → all-passing planes (no cull), never crashes.
- Cull test `LMAABBInFrustum`: the eight corners of the extruded box
  (x/y min/max × z in {0, 16384}); culled iff EVERY corner fails ≥1 plane —
  exact for a convex box under half-space tests, so it can only ever cull,
  never keep a box that pokes in. zMax=16384 is the generous fixed bound from
  the chunk plan (LM_CULL_ZMAX).

### C) D_Render integration (`src/d_main.cpp`, ~line 576)
- File-scope `static uint64_t LevelMeshSlotCounter`; slot computed ONCE per
  D_Render call, before the level loop (lockstep across levels), guarded by
  `interpolate && (gamestate == GS_LEVEL || gamestate == GS_TITLELEVEL)` and
  "any level has useLevelMesh && levelMeshData". `lmSlot` = -1 otherwise.
- Per-level block inserted between `DoInterpolations` and
  `P_FindParticleSubsectors` (the interpolation window, after the snapshot is
  valid): under `lmSlot >= 0 && Level->useLevelMesh && Level->levelMeshData`:
  1. `Clocker c(LevelMeshFrame);` (glcycle_t, single exit)
  2. `state.PackSnapshot(*Level, slot)`
  3. `LevelMesh_CalcFrustumPlanes(&players[consoleplayer], planes)`
  4. `lm_drawentries = levelMeshFrame.Build(*levelMeshData, planes)`
  5. `if (screen) screen->UploadLevelMeshSlot(Level, slot)`
- cvar-0 path: lmSlot stays -1 and the block is skipped entirely; no new code
  runs, frame is byte-for-byte classic.

### D) DFrameBuffer seam
- `src/common/rendering/v_video.h`: `virtual void UploadLevelMeshSlot(
  FLevelLocals *level, int slot) {}` (base no-op) + global forward decl
  `struct FLevelLocals;`.
- `src/common/rendering/gl/gl_framebuffer.{h,cpp}`: override calls
  `GLLevelMesh->UploadSectorSlot(slot, state.GetSectorSlot(slot))` +
  `GLLevelMesh->UploadLightData(state.GetLightData(), state.LightDataSize())`,
  guarded on `GLLevelMesh != nullptr && IsRecordsReady()`. Uses `::FLevelLocals`
  in the header (the class lives inside `namespace OpenGLRenderer`).

### E) perflog (`src/common/rendering/hwrenderer/data/hw_clock.{h,cpp}`)
- `extern glcycle_t LevelMeshFrame;` + `extern int lm_drawentries;`.
- ResetProfilingData resets both.
- r_perflog F-line: `lmfb=<ms> lmdc=<entries>` added after `dcms=` in both the
  header comment line and the fprintf. lmdc is the LAST level's count when
  several levels run the path (perflog convenience, not authoritative).

## Notes for chunk E2 (draw pass)
- The draw list lives in `FLevelLocals::levelMeshFrame` (EntryCount/Entries).
  It is built every frame before `action()` (RenderView); the F-level draw
  pass can run anywhere after that, e.g. at a classic-path seam, reading only
  the F-level list + FLevelMesh + GLLevelMesh textures. No per-surface state
  reads are needed by the cull walk; all dynamic data is in the GPU records.
- The GL upload seam already lands sector ring slot + light data every frame;
  E2's draw just binds and dispatches.
- `lm_drawentries` (r_perflog lmdc) shows per-frame visible sub-range count.

## Residual risks / decisions
- Cull uses the UNINTERPOLATED player origin/angles (D_Render runs after
  DoInterpolations, but AActor pos is only updated by the playsim; the classic
  view interpolates via the render viewpoint). Sub-tic movement difference at
  most; the all-corners-outside test cannot cull a box that pokes in, so the
  practical risk is keeping (not dropping) a marginally out-of-frustum box for
  one sub-tic — no visible artifact possible.
- `lm_drawentries` with multiple enabled levels: last level wins (documented).
- Slot counter is shared across all levels by design (lockstep, auditable);
  each level's ring is independent and slots are full overwrites.
- fovy/fovratio duplication: if VREyeInfo::GetProjection or hw_entrypoint's
  main-view ratio logic changes, the cull frustum must track it (comment in
  code). The mono path only; VR/shifted views do not use the levelmesh path
  yet.
