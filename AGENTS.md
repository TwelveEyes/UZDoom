# AGENTS.md

Guidance for AI agents working in this repository.

## Project

UZDoom is a GPLv3+ DOOM source port and continuation of ZDoom/GZDoom: C++20, CMake (>= 3.16), targeting Windows (MSVC/MinGW), macOS, and Linux (clang/GCC), x86_64 and ARM64. It is a large legacy codebase that inherited ZDoom's file layout, naming, and internal identifiers — many "zdoom"/"ZDoom" names are historical, not bugs.

## Building

Dependencies (Linux, per CI): `libsdl2-dev libopenal-dev libwebp-dev libvpx-dev libbz2-dev waylandpp-dev` plus `build-essential ninja-build ccache cmake`. macOS uses Homebrew (`sdl2 openal-soft fluid-synth vulkan-volk molten-vk libvpx ccache`). Windows MSVC uses prebuilt OpenAL/sndfile/curl (see `.github/workflows/continuous_integration.yml` for exact artifacts).

```sh
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug   # or Release / RelWithDebInfo
cmake --build build --config <type>                      # --config only matters for MSVC
cmake --install build --prefix <pfx>                     # optional
```

- The executable CMake target is `zdoom` (legacy name); the produced binary is `uzdoom` (`ZDOOM_EXE_NAME`).
- The game does not run standalone: resource PK3s (`uzdoom.pk3`, `brightmaps.pk3`, `lights.pk3`, `game_support.pk3`, `game_widescreen_gfx.pk3`) are built by the `zipdir` tool and copied next to the binary (`ZDOOM_RESOURCE_DIR`). Run from the build directory. PK3 targets deliberately touch `zipdir` each build, so they always repack.
- LTO is enabled for Release/RelWithDebInfo unless `FORCE_NO_LTO=ON` (CI sets it for MinGW GCC).
- Cross-compiling: `FORCE_CROSSCOMPILE=ON` plus `IMPORT_EXECUTABLES=<path>/ImportExecutables.cmake` from a prior native build.

Frequently relevant CMake options (root + `src/CMakeLists.txt`):
`HAVE_VULKAN=ON`, `DYN_OPENAL=ON`, `BUILD_NONFREE=ON` (commercial-game assets in `wadsrc_extra/nonfree`), `USE_PCH=ON`, `ENABLE_IWYU=ON`, `SEND_ANON_STATS=ON`, `USE_UPDATER=ON` (Windows MSVC only, builds `updater.exe`), `OSX_COCOA_BACKEND=ON` (native Cocoa vs SDL), `NO_STRIP`, `NO_OPENMP`, `PROFILE` (gprof), `WITH_ASAN` (MSVC), `FORCE_INTERNAL_BZIP2` / `FORCE_INTERNAL_CPPDAP`.

The script JIT (asmjit) is x86_64-only (`HAVE_VM_JIT` is set only for x86_64).

## Code style / whitespace (enforced)

- **Tabs**, not spaces, for indentation. The tree was converted to tabs (see `.git-blame-ignore-revs`); do not reintroduce leading spaces. Style: LF endings, final newline, no trailing whitespace — keep new/edited code consistent with the surrounding code by hand. **Do not run `tools/format-spaces.sh` as a verification step** (check rule removed 2026-10-01): it rewrites files in place even with `-c` (only `-d` is a true dry-run) and the tree carries pre-existing violations, so a "check" mutates the working tree.
- `.clang-format`: `BasedOnStyle: Microsoft`, `UseTab: AlignWithSpaces`. Use `// clang-format off/on` sparingly where needed.
- New source files should follow the header template in `CONTRIBUTING.md` (GPLv3+ boilerplate + section banner comments: HEADER FILES, MACROS, TYPES, CODE, etc.).
- `.editorconfig` matches: UTF-8, LF, tab indent, final newline, trimmed trailing whitespace.
- Translatable engine strings go through the `Translation` system (`libraries/Translation`), which syncs with Weblate. Don't ad-hoc string tables for user-facing text.

## Build/test verification

- **There is no unit test suite** (no `add_test` anywhere in project code). Per CONTRIBUTING, correctness is verified by a clean, warning-free build on all supported platforms plus in-game testing. CI builds Debug, Release, and RelWithDebInfo on Windows/macOS/Linux plus GCC/MinGW/ARM variants.
- Appstream metadata is linted in CI: `src/posix/freedesktop/org.zdoom.UZDoom.metainfo.xml` (note the legacy `org.zdoom` app ID).
- IWYU is wired into CMake (`ENABLE_IWYU=ON`, mapping file `tools/iwyu.imp` which redirects SDL subheaders to the superheader `<SDL2/SDL.h>`). CI compiles with `USE_PCH=OFF` because PCH breaks IWYU diagnostics; do the same locally if running IWYU.

## Code generation (generated in the build tree; edit the sources)

- `gamedata/xlat/xlat_parser.y` -> `xlat_parser.c/.h` via `lemon` (built from `tools/lemon`)
- `common/scripting/frontend/zcc-parse.lemon` -> `zcc-parse.c/.h` via `lemon`
- `common/engine/sc_man_scanner.re` -> `sc_man_scanner.h` via `re2c` (built from `tools/re2c`)

Never hand-edit the generated `.c`/`.h` outputs; modify the `.y`/`.lemon`/`.re` input.

## Architecture

Two-layer layout: the classic ZDoom top level in `src/` plus a newer platform-neutral `src/common/` layer.

- `src/` top-level files keep classic prefixes: `d_*` (startup, game loop, net), `p_*` (physics, level setup, saves), `r_*` (rendering entry/state), `m_*` (menus), `g_*` (game logic, cvars), `hu_*`/`st_*`/`wi_*` (HUD, status bar, intermission), `am_map` (automap), `doomdef/doomdata/doomtype` (core structs/enums).
- `src/common/` — the modern layer:
  - `common/engine` — events, serialization (DOBJ), input, stats, strings, `printf.h`
  - `common/filesystem` — WAD/PK3 archive reading, path handling
  - `common/platform/{posix,win32}` — OS-specific: `i_system`, audio, video, backends (SDL and native Cocoa under `posix/cocoa`)
  - `common/rendering` — renderer entry, GL/Vulkan loading, render thread
  - `common/scripting` — Fragglescript/ACS: `frontend` (lexer/parser), `vm` (interpreter), `jit` (asmjit, x86_64), `dap` (debug-adapter)
  - `common/objects` — DOBJ type system + GC
  - `common/audio`, `common/console` (cvars, command console), `common/menu`, `common/statusbar`, `common/textures`, `common/fonts`, `common/models`, `common/2d`
- `src/gamedata/` — WAD-level game data: actors (`a_*.cpp`), MAPINFO/UMAPINFO parsing, Dehacked, fonts, keys, xlat parser, gametypes.
- `src/maploader/` — map loading: UDMF, USDF, node building (`glnodes`), specials, legacy-format compatibility. `specs/` holds the UDMF/USDF specs the loader implements.
- `src/rendering/` — `swrenderer` (software), `hwrenderer` (OpenGL/Vulkan scene graph), `2d`.
- `src/widgets/` — modern ZWidget-based UI (launcher window, settings, error window).
- `src/intermission/`, `src/g_statusbar/` — modernized intermission and HUD/status bar.
- `src/scripting/` — game-side scripting glue; `src/posix/` and `src/win32/` are legacy platform code (much moved to `common/platform`); `src/unused` is dead code, not built.
- `src/g_pch.h` — precompiled header applied to all TUs via `target_precompile_headers`; a small exclusion list uses `SKIP_PRECOMPILE_HEADERS`. New TUs get the PCH automatically.
- `libraries/` — vendored: `ZVulkan` (Vulkan renderer backend), `ZMusic` (music), `ZWidget` (UI), `Translation` (i18n; `scripts/compile.py` generates language files into the source tree during build, then deletes them), `abseil`, `asmjit`, `cppdap`, `webp`, `lzma`, `bzip2`, `miniz`, `range_map` (used by the script VM/DAP).
- `wadsrc/`, `wadsrc_bm/`, `wadsrc_lights/`, `wadsrc_extra/`, `wadsrc_widepix/` — game resource sources packaged into the PK3s above. Non-commercial/derived assets belong in `wadsrc_extra`.
- `tools/` — build utilities: `zipdir` (PK3 packager; dependency of all PK3 targets), `lemon`, `re2c`, `myiswalpha`, `format-spaces.sh`.
- `docs/` — in-game documentation installed with the game (e.g. `console.html`). `fm_banks/` and `soundfont/` are music synth assets. `branding/` holds app icons.

## Gotchas

- Legacy naming everywhere: target `zdoom`, `ZDOOM_*` CMake variables, `org.zdoom.UZDoom.*` freedesktop files, and `-DTHIS_IS_GZDOOM` (still defined in `src/CMakeLists.txt` though unused in code). Don't "fix" these without checking they're truly dead.
- PCH + IWYU conflict: build with `USE_PCH=OFF` when running IWYU.
- IWYU expects the SDL superheader `<SDL2/SDL.h>`, not individual `SDL2/SDL_*.h`.
- `tools/update-subtrees.sh` manages `libraries/{ZWidget,ZMusic,Translation,ZVulkan}` as git subtrees; vendor updates go through that script (or `update-subtrees.cmd` on Windows), not manual sync.
- The executable name, PK3 names, and output dir are all overridable (`ZDOOM_EXE_NAME`, `ZDOOM_OUTPUT_DIR`) — assume `uzdoom`/build dir but verify before scripting around them.
- Filenames with brackets in `wadsrc*` confuse CMake IDE listings; `add_pk3()` already works around this — don't add such names without matching that workaround.
- `CONTRIBUTING.md` prohibits AI-generated code contributions to upstream (commit/PR messages included). Flag this to the user before preparing any commit or PR.
- There is no test runner; at minimum verify with a full build (`cmake --build build`) and note that in-game verification is the project's standard.
- `libraries/`, `bin/`, `*/thirdparty/`, `tools/re2c`, `tools/lemon` are exempt from whitespace/formatting enforcement.

## Agent skills

### Issue tracker

Issues live as markdown files under `.scratch/<feature>/` in this repo. See `docs/agents/issue-tracker.md`.

### Triage labels

Default five-role vocabulary: needs-triage, needs-info, ready-for-agent, ready-for-human, wontfix. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `CONTEXT.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
