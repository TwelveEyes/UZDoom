# 01: Tear down the old levelmesh implementation

**Map:** `../map.md`: Locked decisions ("Existing code"), Standing constraints.
**Type:** task
**Status:** ready-for-agent
**Blocked by:** None

## What to build

Remove the GL33 levelmesh implementation (commits `ef6499f`..`7dac2c3`). Keep:

- the A/B capture mode (`d_abcapture.*`, the `i_time.cpp` / `d_net.cpp` / `d_main.cpp` capture
  hooks);
- `tools/abtest.py` and `tools/abtest/`;
- `r_perflog`;
- the `gl_uselevelmesh` cvar;
- the unrelated fixes made in the same period (the `SetActiveShader` rebind fix in `gl_shader.cpp`
  and the Vulkan present clamp `45b2ba4`, both outside the range).

The levelmesh-specific changes span 36 files under `src/` and `wadsrc/`
(`git diff --stat ef6499f^ HEAD -- src wadsrc`). The baseline for those files is `8cf10772`.
Specifically:

- Delete `src/common/rendering/levelmesh{,_state,_frame}.{h,cpp}`,
  `src/common/rendering/gl/gl_levelmesh.{h,cpp}`, and
  `wadsrc/static/shaders/glsl/levelmesh{.vp,.fp,_light.glsl}`, plus their `CMakeLists.txt` entries.
- Revert the hooks in:
  - `d_main.cpp` (`D_Render` frame build);
  - `hw_drawinfo.cpp` (`RenderScene` branch);
  - `hw_drawlist.{h,cpp}` (`DrawPolyobjWalls`, `DrawHackFlats`);
  - `gl_framebuffer.{h,cpp}`, `gl_renderstate.{h,cpp}`, `gl_shader.{h,cpp}`, and
    `hw_renderstate.h` (`LevelMeshDrawParams`, `DrawLevelMesh`);
  - `hw_shaderpatcher.{h,cpp}` (the effect entry, `prelude`, `glonly`) and `vk_shader.cpp`;
  - `v_video.h` seams, `g_levellocals.h` fields, `p_setup.cpp`, `maploader.cpp`, `textures.h`;
  - the `hw_clock.{h,cpp}` `lmfb` / `lmdc` perflog columns. Also update `parse_perflog.py` if it
    reads them.
- Restore `DoomLevelMesh` to its classic builder (the `DoomLevelMesh(FLevelLocals&)` constructor
  is still in the tree) so `VkRaytrace` keeps its input.
- Keep `gl_uselevelmesh`, but make it a no-op until ticket 08, and fix its comment in
  `hw_cvars.cpp`.
- Update the stale references in `tools/abtest/README.md`, `tools/abtest/manifest.md`, and
  `tools/abtest.py` (docstring, `r_dynlights` pin comment) to point at `.scratch/levelmesh-impl/`
  and the new ADRs.

## Acceptance

- [ ] Clean RelWithDebInfo build. No new warnings in touched files.
- [ ] `git diff 8cf10772 -- src wadsrc` shows only intentional leftovers (the cvar and its comment),
      each listed in the ticket's Comments.
- [ ] Classic A/B self-check (classic vs classic, Vulkan + GL33, static tick list) is byte-identical.
      The capture tooling still works.
- [ ] `VkRaytrace` smoke run (a lightmapped map, or the raytrace path on DOOM2 MAP01) runs clean.
- [ ] Map load no longer builds or uploads anything levelmesh-related.

## Notes

The audit (`../audit-2026-10-11.md` §5) lists the defects being removed. Do not carry over any of
that code into later tickets: ticket 06 rebuilds the data layer from scratch.

## Comments
