# Full Spectrum Warrior: Ten Hammers - Xbox Static Recompilation

> **A native Linux bring-up of the original Xbox build using the xboxrecompforlinux runtime.**

This project recompiles the original Xbox executable for **Full Spectrum Warrior: Ten Hammers** into a native Linux binary. Game files are not included in this repository.

The current bring-up target is a **review copy build dated Dec 5 2005** because it is symbol rich. Its PDB and MAP data let us recover meaningful function names and a source-like tree. If the port reaches a useful playable state, the plan is to evaluate moving to a retail `default.xbe` without throwing away the recovered source layout, runtime work, or game-specific fixes.

## Current Runtime Screenshots

These captures come from the Linux Vulkan renderer, not mockups.

| Press Start | New Profile | Brightness setup |
|---|---|---|
| ![Current Linux Press Start shell render](docs/screenshots/linux-shell-press-start.png) | ![Current Linux New Profile menu render](docs/screenshots/linux-profile-new-profile.png) | ![Current Linux brightness setup render](docs/screenshots/linux-profile-brightness.png) |

The shell/menu screenshots show real `Shell.pak` assets, the real start-menu logo, profile setup menus, shell background/splash textures, and real font draws through Vulkan. Text is now readable, but several transforms and overlay animations are still not retail-correct.

| Direct first-level runtime |
|---|
| ![Current direct first-level runtime frame](docs/screenshots/first-level-runtime-actual.png) |

The first-level screenshot is still black because the direct mission path presents frames from the gameplay loop with `vertices=0`. That is progress over crashing during setup, but it is not visible gameplay yet.

## Current Status

**The Linux port now reaches two important real paths: the shell/profile renderer and the first-level mission loop.**

The normal shell path initializes the Vulkan presentation host, loads `Shell.pak`, resolves PAK-contained resources through the game's `ZeroFile` memory-file system, loads `menu.ui`, selects the shell menu, decodes real shell textures, decodes font atlas data, and submits real RHW UI draws through Vulkan. The verified flow now reaches:

```text
Loading/shell states -> Press Start -> New Profile -> Brightness setup -> post-brightness transition
```

The post-brightness transition now submits the real menu event from state `0x0D`, but it currently routes into net state `0x8A` with no menu CRC. The next shell blocker is identifying the intended state/menu after brightness so the profile flow can continue into profile selection or the main menu normally.

The direct first-level path now loads `BD_startbusdepot.pak`, registers texture tables, object tables, ABKs, Havok descriptors, fonts, meshes, INI/CSV data, and then enters `ProcessGame`. A 30 second run reached more than 2,000 presented frames in the mission loop without crashing.

This is not playable yet. The mission path currently has no populated render scene: `SceneManager` stays at `count=0`, `SceneManager_RenderAll` runs, and the Vulkan host presents native frames, but the draw counters remain at `vertices=0`. The next real gameplay blocker is scene/world population, not presentation.

## Since Last Push

- Added and hardened Linux Vulkan D3D8 presentation paths for native RHW UI draws, frame presentation, and runtime frame dumping.
- Advanced the real shell/menu path: `Shell.pak`, `menu.ui`, UI texture lookup, image controls, line controls, static text, font atlas decode, and location-string fallback all run far enough to render the real shell screen.
- Improved Linux input through SDL/XInput mapping so controller and keyboard fallback paths can drive bring-up.
- Added PAK/memfs/CRC work so resources resolve through the game's real CRC/path flow instead of hardcoded texture substitution.
- Located shell-contained video resources and connected more Bink/video surface plumbing, though the full intro video sequence still needs integration and timing work.
- Fixed the shell font atlas channel mapping so real menu text renders readable.
- Switched shell background strip rendering to game-computed UVs and allowed inverted image-control UV ranges used by the menu animation data.
- Advanced the real profile path through `Press Start`, `New Profile`, and `Brightness`, including a state `0x0D` profile setup transition repair.
- Moved first-level direct mode past earlier setup crashes in PAK loading, object construction, Havok descriptor registration, AI setup, camera update, mission update, and render dispatch.
- Stabilized generated render-cookie construction around clobbered `this`, `ebx`, and ESP state so render setup no longer dies during `XBRenderCookie` construction.
- Added targeted guards and diagnostics across AI, Havok, scene, prop, HUD, particles, sky, sound, script, and manager code so bad generated state is logged and isolated instead of immediately crashing.
- Added actual runtime screenshots under `docs/screenshots/`

## What's Working

- **Linux build** - CMake builds `bin/fsw_th_recomp` from the copied xboxrecompforlinux runtime and generated game tree.
- **Source-like generated tree** - PDB source-file records and MAP object names drive the `src/game/fsw/...` layout, with fallback modules under `src/game/external/`.
- **Xbox runtime model** - the recomp runs against an Xbox-style flat memory layout, kernel thunk bridge, heap allocator, XAPI shims, and guest file descriptor table.
- **Linux path handling** - Xbox paths such as `D:\Chapters\Shell.pak` resolve on a case-sensitive Linux filesystem.
- **PAK/memfs loading** - shell and level PAK resources resolve through in-memory PAK tables with relocated data offsets.
- **Real shell drawing** - the current renderer draws real shell textures, readable menu text, profile setup screens, and animated shell background strips through Vulkan.
- **SDL input bridge** - SDL-backed XInput compatibility detects and maps modern controllers, with keyboard fallback for bring-up.
- **Scripted input bring-up** - `FSW_TH_INPUT_SCRIPT` can drive repeatable shell/profile tests without manual controller input.
- **Direct level loop** - `FSW_TH_FORCE_DIRECT=1 FSW_TH_LEVEL=1` reaches `ProcessGame` and continues presenting frames.

## Current Blockers

- [ ] Finish shell/menu transforms and animation timing so the logo, splash strip, loading screen, Press Start, profile flow, and menu options match the Xbox presentation.
- [ ] Route the post-brightness profile setup transition. State `0x0D` now submits event `2`, but the Linux path currently lands in net state `0x8A` with `menu_crc=00000000`.
- [ ] Finish Bink intro playback from PAK-contained assets and route it into the real shell timeline.
- [ ] Replace temporary XACT/audio bypasses with a real Xbox audio path after gameplay rendering is further along.
- [ ] Fix first-level world/scene population. The direct path reaches `InitLevel`, `ConstructObjects`, and `ProcessGame`, but `SceneManager` still has zero scene objects and the renderer receives no gameplay vertices.
- [ ] Route or repair the real `LoadWLD`/`CGameWorld_LoadWLD`/static-prop setup path; current logs do not show the expected WLD/static prop population in direct mode.
- [ ] Reduce noisy bring-up diagnostics once each subsystem is stable enough for public testing.

## Current Findings

| Area | Finding |
|------|---------|
| **Build provenance** | The Dec 5 2005 review copy remains the best bring-up target because the PDB/MAP data gives useful source paths and function names. |
| **Source layout** | PDB DBI source-file records are good enough to reconstruct most game modules under a readable `src/game/fsw/...` tree. |
| **PAK resources** | `Shell.pak` contains shell UI resources and video assets; level PAKs contain object, texture, Havok, mesh, and script-side data. |
| **CRC/resource lookup** | CRCs are calculated by the game path at runtime; the current work uses those calculated CRCs to resolve loaded PAK resources rather than mapping "CRC X means texture Y" by hand. |
| **Menu rendering** | Real textures and readable fonts are reaching Vulkan; remaining shell work is mostly transform/animation/state routing rather than placeholder rendering. |
| **Profile flow** | Press Start reaches New Profile, New Profile reaches Brightness, and Brightness now submits a real state event. The next bad transition is state `0x8A` with no menu CRC. |
| **Bink/video** | The host-side Bink decode path and video-surface plumbing are partly in place, but the intro sequence is not yet a verified end-to-end shell flow. |
| **Render-cookie bug** | Generated code around `XBRenderCookie` construction could clobber `this`, `ebx`, and ESP; targeted repair keeps render-cookie setup alive for now. |
| **First-level blocker** | Direct mode reaches the mission loop, but WLD/static prop setup does not appear to populate `SceneManager`, leaving gameplay presentation at `vertices=0`. |
| **Audio** | XACT wave/sound-bank work is intentionally not the current priority; some paths are bypassed so rendering and gameplay bring-up can continue. |

## How It Works

```text
default.xbe + symbols
    |
    v
PDB/MAP-aware layout tools -> source-like generated tree
    |
    v
x86 -> C static recompilation
    |
    v
CMake + native compiler
    |
    v
Runtime: Xbox memory + kernel bridge + D3D8/Vulkan + SDL input
```

## Target Build

| Field | Value |
|-------|-------|
| **Title** | Full Spectrum Warrior: Ten Hammers |
| **Platform** | Xbox |
| **Current bring-up build** | Review copy |
| **Build date** | Dec 5 2005 |
| **Why this build** | Symbol-rich PDB and MAP data |
| **Generated source files** | 1,458 |
| **Emitted functions** | 46,875 |
| **PDB-style source-tree files** | 1,179 |
| **Fallback external files** | 279 |
| **Game files** | Local only, gitignored |

## Project Structure

```text
FullSpectrumWarriorTHXBOX/
|-- README.md
|-- CMakeLists.txt
|-- docs/
|   |-- screenshots/
|   `-- source-layout.md
|-- include/
|-- src/
|   |-- apu/
|   |-- audio/
|   |-- d3d/
|   |-- game/
|   |   |-- fsw/
|   |   |-- external/
|   |   `-- recomp/
|   |-- input/
|   |-- kernel/
|   |-- nv2a/
|   `-- platform/
|-- templates/
|-- tools/        # local/private helper tools, gitignored
`-- game_files/   # local game files, gitignored
```

## Building

### Linux Prerequisites

- CMake 3.20+
- Python 3.10+
- SDL2 development package
- Vulkan loader and headers
- Original Xbox game files placed locally under `game_files/`

### Linux Build Steps

```bash
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/linux --target fsw_th_recomp -j$(nproc)
```

Run from the repo root:

```bash
bin/fsw_th_recomp game_files/default.xbe game_files
```

Useful bring-up commands:

```bash
# Real shell/menu path
FSW_TH_DISABLE_MENU_FALLBACK=1 tools/run_host.sh

# Direct Chapter 1 path
FSW_TH_FORCE_DIRECT=1 FSW_TH_LEVEL=1 FSW_TH_DISABLE_MENU_FALLBACK=1 tools/run_host.sh

# Dump a Vulkan frame to /tmp/xboxrecomp_vulkan_frame.ppm
XBOXRECOMP_DUMP_VULKAN_FRAME=120 FSW_TH_DISABLE_MENU_FALLBACK=1 tools/run_host.sh
```
