# RE1 Analog 3D Controls

A DLL mod for **Resident Evil 1 PC (Classic REBirth)** that replaces the original tank controls with camera-relative analog stick movement — similar to how modern RE remakes feel.

## What it does

By default, RE1 uses tank controls: forward always means "the direction the playable character is facing". This mod changes that so the **left analog stick moves the playable character relative to the camera** — push up and they walk toward the camera's forward direction, push left and they go left from the camera's perspective, regardless of which way they are facing.

The mod also handles:
- **Camera cuts** — four switchable modes control how the angle is preserved across camera transitions so the character doesn't suddenly reverse direction:
  - **Mode 1 — Stick Buffer** *(default)*: on a cut, the previous camera angle is held while the stick is Y-dominant (forward/backward). Sideways input snaps to the new camera immediately.
  - **Mode 2 — Run Buffer**: same as Mode 1 but buffering only activates when the player is running at the cut. Walking through cuts always uses the new camera.
  - **Mode 3 — Smooth Blend**: on a cut the old angle freezes briefly, then eases into the new angle over a configurable duration.
  - **Mode 4 — Sector Remake**: the stick angle is divided into configurable sectors (default 16 = 22.5° each). The camera angle is latched when the stick first engages and only updates when the stick crosses a sector boundary. On a camera cut the angle update is briefly delayed to avoid direction spikes. Character rotation is smoothed for a fluid, modern feel. Inspired by X4vv's RE2/RE3 upgrade.

## Controls

| Hotkey | Action |
|--------|--------|
| CTRL+1 | Mode 1 — Stick Buffer |
| CTRL+2 | Mode 2 — Run Buffer |
| CTRL+3 | Mode 3 — Smooth Blend |
| CTRL+4 | Mode 4 — Sector Remake |
| CTRL+0 | Toggle analog controls off/on |

Active mode is shown on screen for 5 seconds after switching and persists between sessions via `analog3d.ini` in the game directory.

## Technical overview

The mod is a 32-bit DLL loaded by the Classic REBirth mod loader (configured via `manifest.txt`).

At runtime it:
1. Scans the game's `.text` section for the pad input function using byte-pattern signatures
2. Installs an x86 detour (trampoline hook) to intercept input processing
3. Reads the XInput left stick, computes the camera facing angle from RCUT camera data, and converts stick direction into the correct BAM angle for the playable character
4. Falls back to a polling thread if the scan fails

Angles use the BAM system (Binary Angle Measurement): 4096 units = 360°.

The RE1-Mod-SDK submodule is included as a reference — it is **not required to compile** the mod, but it documents the game's memory layout and structs that were used during development.

## Requirements

- Resident Evil 1 PC — the mod was tested on the **Japanese version from GOG** with the [Classic REBirth](https://classicrebirth.com/index.php/downloads/resident-evil-classic-rebirth/) patch applied
- XInput-compatible controller (Xbox controller or equivalent)

## Build

Requirements: Visual Studio, Windows SDK

```
msbuild analog3d.sln /p:Configuration=Release /p:Platform=x86
```

Or open `analog3d.sln` in Visual Studio and build **Release | x86**.

Output: `Release/analog3d.dll`

## Installation

1. Build the DLL or download a release
2. Create a folder named `mod_xxx` (where `xxx` is your chosen name) in the same directory as the game's `.exe`
3. Place `analog3d.dll` and `manifest.txt` inside that folder
4. Launch the game

## About

This mod was created with the help of various AI tools, primarily **Claude** (by Anthropic). The logic, memory research, and iteration were a collaboration between me and AI — Claude helped write, debug, and refine the code throughout the entire development process.

Special thanks to **X4vv** from the Kroniki Myrtany Discord for help and bug testing during development.

The RE1-Mod-SDK used in this project was created by **[Gemini-Loboto3](https://github.com/Gemini-Loboto3/RE1-Mod-SDK)** and is licensed under [CC BY-NC-ND 4.0](https://creativecommons.org/licenses/by-nc-nd/4.0/).
