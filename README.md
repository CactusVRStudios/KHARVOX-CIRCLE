![KHARVOX: CIRCLE](assets/Kharvox%20Crilce%20Logo%20Banner.png)

# KHARVOX: CIRCLE

A VR conversion mod for **Indiana Jones and the Great Circle**, made by Cactus.
Stereoscopic rendering, tracked motion controllers, physical melee, and a dedicated VR launcher.

> **Hardware requirement: at least 12 GB of VRAM.** Indiana Jones and the Great Circle is a very demanding game, especially in VR. Higher headset resolutions and graphics settings can require considerably more GPU performance and memory. Upscaling and frame generation do not remove this requirement.


[Installation](#installation) · [Controller bindings](#controller-bindings) · [Building from source](#building-from-source) · [Support](#support)

## Installation

1. Download a release ZIP from [Releases](https://github.com/CactusVRStudios/KHARVOX-CIRCLE/releases).
2. Extract the complete archive into a new, writable folder. Keep all included files together; do not run the launcher from inside the ZIP or copy the mod into the game folder.
3. Connect your headset and controllers, then start your OpenXR runtime.
4. Run **CircleLauncher.exe**, select **TheGreatCircle.exe**, and configure your VR options.
5. Start the game through the launcher. Steam and GOG installations are supported for the game builds recognized by the launcher.

## Controller bindings

### Interaction

![CIRCLE interaction controls](assets/circle-controller-interaction-en.png)

### Combat

![CIRCLE combat controls](assets/circle-controller-combat-en.png)

## Instructions

Keep the game window focused while playing. Controller instructions are also available in the launcher and in the main and pause menus.

The VR intro runs before the game. After its first start, **Disable VR Intro** becomes available under **Rendering**. Launcher settings are stored per Windows user and carry over between releases.

Rendering options include upscaling and VR frame generation, with available backends depending on your GPU. Choose fixed frame-generation multipliers, **Auto** for conservative whole-number switching, or **Auto Seamless** for adaptive interpolation. Apply rendering changes before starting the game.

## Beta status

CIRCLE is in active beta development. Compatibility, performance, and some interactions can vary by headset, GPU, runtime, and game version. SteamVR and Virtual Desktop's VDXR have been tested; this does not guarantee every device configuration.

Report problems through [Issues](https://github.com/CactusVRStudios/KHARVOX-CIRCLE/issues), including the mod version, headset, GPU, runtime, and steps to reproduce. Enable logging in the launcher when collecting diagnostic logs; it is off by default.

## Building from source

The Windows build produces the Vulkan/OpenXR renderer, frame-generation components, and the CIRCLE launcher.

The checkout includes the CMake test sources, launcher assets, prebuilt VR intro, and pinned frame-generation runtime DLLs. CMake downloads the remaining SDK dependencies automatically on the first configure. No private development folders are required.

### Requirements

- Windows x64.
- Visual Studio 2022 with **Desktop development with C++**, MSVC x64 tools, and a Windows SDK. The included preset uses `Visual Studio 17 2022`.
- CMake 3.24 or newer, Git, and PowerShell. Run the commands in a Visual Studio Developer PowerShell.
- The Windows .NET Framework 4.x compiler and WPF assemblies used by `tools/build-launcher.ps1`.
- Internet access for the dependencies downloaded and checksum-verified by CMake.

### Libraries and runtime dependencies

| Dependency | Purpose | Setup |
| --- | --- | --- |
| OpenXR loader and headers 1.1.63 | Headset/runtime integration | Downloaded and verified by CMake. |
| Vulkan headers | Vulkan layer integration | Uses an installed Vulkan SDK, or downloads pinned Vulkan-Headers 1.4.357. |
| SafetyHook 0.7.0 with Zydis | Native game hooks | Downloaded and verified by CMake. |
| NVIDIA DLSS SDK 310.9.1 | DLSS frame-generation integration | Fetched automatically from [NVIDIA/DLSS](https://github.com/NVIDIA/DLSS/tree/374959484e79a640feaba44c93ac8cfb0a03f5b5) at the pinned commit. An existing SDK can optionally be supplied with `CIRCLE_DLSS_SDK_ROOT`. |
| FidelityFX SDK 1.1.4 integration | AMD frame-generation backend | Includes the project's headers and matching `third_party/fidelityfx/bin/CircleFidelityFX_DX12.dll`. CMake verifies the expected binary hash; an arbitrary SDK DLL is not a substitute. |
| Community DLSS runtime 0.4.0 | Additional frame-generation backends | Includes pinned RTX 20/30 and RTX 40 payloads under `runtimes/dlss-unlocked-0.4.0/`; packaging verifies them. |
| CIRCLE VR Intro | Intro before game launch | Included as `bin/KHARVOX CIRCLE Intro.exe`; no separate intro build is needed. |

Dependency versions and checksums are defined in `src/circlevr/cmake/`, `cmake/NativeFramegen.cmake`, `CMakeLists.txt`, and `tools/install-community-framegen.ps1`.

### Configure and compile

Clone the repository:

```powershell
git clone https://github.com/CactusVRStudios/KHARVOX-CIRCLE.git
cd KHARVOX-CIRCLE
```

Configure and build:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

The build uses `RelWithDebInfo`. Native runtime output is written to `bin/`, and the launcher is written to `CircleLauncher.exe` in the project root. The launcher can also be rebuilt separately:

```powershell
.\tools\build-launcher.ps1
```

Keep all required runtime files together. The launcher EXE alone is not a complete playable installation.

### Create a playable package

After building, choose an unused release name:

```powershell
.\tools\package-release.ps1 -Name KHARVOX-CIRCLE-Beta-0.1-local
```

The script builds the launcher, verifies the frame-generation payloads, and creates a runtime-only folder and ZIP under `releases/`. It refuses to overwrite an existing release. Developer documentation, tests, logs, and debug symbols are not included in the package.

### Development checks

The CMake test sources and PowerShell checks are included. Run:

```powershell
ctest --test-dir build/x64 -C RelWithDebInfo --output-on-failure
```

Close the game before running the complete suite: launcher handoff tests use a simulated game process. Automated checks do not replace gameplay and headset testing.

## Support

If you enjoy CIRCLE and would like to support development, visit [Cactus VR Studios on Ko-fi](https://ko-fi.com/cactusvrstudios/tip).

[![Support Cactus VR Studios on Ko-fi](assets/ko-fi-support.png)](https://ko-fi.com/cactusvrstudios/tip)
