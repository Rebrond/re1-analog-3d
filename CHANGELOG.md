# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

---

## [0.9.0] - 2026-03-28

### Added
- **Mode 4 — Sector Remake** (`CTRL+4`) — new camera-cut handling mode inspired by X4vv's upgrade of this mod for RE2/RE3:
  - Stick angle is divided into configurable sectors (default 16 = 22.5° each, set via `sectors` in `analog3d.ini`)
  - Camera angle is latched when the stick first engages from the deadzone; only updates when the stick crosses a sector boundary
  - On a camera cut coinciding with a sector boundary crossing, the angle update is delayed by `camera_update_delay` ms (default 40 ms) to avoid direction spikes
  - Character rotation is smoothed at `rotation_speed_deg` degrees per second (default 50°/s) for a fluid, modern feel
  - Backward key detection disabled in this mode — always uses `KEY_FORWARD` (like RE2/RE3) since rotation smoothing handles large turns gradually
- Three new `analog3d.ini` entries: `sectors`, `rotation_speed_deg`, `camera_update_delay`

### Fixed
- **Mode 4: rotation inconsistency after stick release/re-engage** — `g_RemakeSector` was not reset when the stick entered the deadzone. On re-engagement in the same stick sector, neither sector branch fired and `g_ActiveCamAngle` fell back to BAM zero (north), causing the character to always snap toward north. Fix: reset `g_RemakeSector = -1` on deadzone entry so re-engagement re-latches to the current live camera angle.
- **Mode 4: run animation restarted on camera cuts** — the backward key detection caused `KEY_BACKWARD` during the frames while rotation smoothing was catching up after a pending camera update. The `BACKWARD → FORWARD` transition wrote `KEY_FORWARD` as a new press to `G_KEY_TRG`, restarting the run animation from frame 0 on every cut. Fix: disabled backward detection in Mode 4 entirely.

### Credits
- Mode 4 concept and sector-based logic inspired by **X4vv**'s RE2/RE3 upgrade of this mod

---

## [0.7.5] - 2026-03-17

### Added
- **Three camera-cut handling modes** — ported from x4vv's RE3 mod, switchable in-game with CTRL+1 / CTRL+2 / CTRL+3:
  - **Mode 1 — Stick Buffer** (default): on a camera cut, the previous camera angle is held while the stick is Y-dominant (forward/backward). Evaluated once at cut boundary, not every frame — no per-frame snap artefacts. Sideways input snaps to new camera immediately.
  - **Mode 2 — Run Buffer**: same as mode 1 but buffering only activates when the player is running at the cut. Walking through cuts always snaps to the new camera.
  - **Mode 3 — Smooth Blend**: on a cut the old angle freezes for `freeze_ms` ms, then smoothly blends to the new angle over `blend_ms` ms via smoothstep.
- **`analog3d.ini`** — persistent config written to the game directory on first launch. Stores active mode and Mode 3 timing values (`freeze_ms`, `blend_ms`). Created with commented defaults if absent.
- **Mode persistence in save files** — active mode is stored in the mod's save slot via `Modsdk_load` / `Modsdk_save`; survives game restarts.
- **CTRL+0 kill switch** — toggles analog controls off/on. Useful for bypassing the mod when a scripted sequence conflicts. Direction keys are cleared once on disable, then left untouched so native keyboard control works. CTRL+1/2/3 also re-enables if the mod was disabled.
- **On-screen mode display** — Win32 layered topmost window (`WS_EX_LAYERED | WS_EX_TOPMOST`) overlaid on the game window. Shows the active mode name (or "Controls Disabled / Enabled") for 5 seconds after any hotkey press. Text scales with window height (~5% of height) for readability at any resolution including ultrawide. Drop shadow uses `RGB(1,1,1)` to remain visible against the transparent colorkey (`RGB(0,0,0)`).

---

## [0.7.4] - 2026-03-17

### Fixed
- **Door transitions caused ~12 frames of tank controls** — regression introduced during the Codex session. `STATUS` bits `0x40000000` / `0x02000000` (`hardCut`) were used as a latch trigger with a 12-frame release countdown, causing every door transition to block analog for ~0.4 s. Removed `hardCut` from the latch trigger entirely; it now only blocks for its own direct duration. Added `DOOR_RELEASE_FRAMES = 3` (vs `CUTSCENE_RELEASE_FRAMES = 12`) for latches where letterbox was never observed. `s_letterboxSeenDuringLatch` tracks whether letterbox appeared during a latch to distinguish door transitions from real cutscenes.

---

## [0.7.3] - 2026-03-13

### Added
- `0.7.3/dllmain.cpp` — source snapshot for this version

### Fixed
- **Room numbers in log matched index, not game debug display** — e.g. `room=6` instead of `106`. Changed formula to `(stage + 1) * 100 + room`; stage is 0-indexed internally but the game debug menu displays it 1-based.

### Changed
- `README.md` — added RE1-Mod-SDK attribution (created by Gemini-Loboto3, licensed CC BY-NC-ND 4.0)

---

## [0.7.2] - 2026-03-13

### Added
- `dllmain_logreduced.cpp` — alternate build with 1 MB ring-buffer log cap; wraps write position back to start of data region on overflow, header written once
- `0.7.2/dllmain.cpp` — source snapshot for this version
- `G_LETTERBOX` (`0xC330A4` = `G_BASE + 0x14`) as primary cutscene block signal — letterbox bar height written by `StMask()`, fades 240 → 0 during cutscene end
- `G_CV_A–F` (`0x4CB23C–0x4CB29C`) — 6 Cheat Engine addresses that change during cutscenes, semantics unknown; logged diagnostically via `CutVars:` on any change
- `FLG5 bit 0x0100` transition logging for observation (0 = scripted, 1 = gameplay)
- `CHANGELOG.md`

### Fixed
- **1-frame direction snap on camera cuts** — removed X-dominant camera override that reset `g_ActiveCamAngle` to the new camera mid-stick-hold when `|stickX| > |stickY|`, causing a visible direction lurch for one frame at cut boundaries. `g_StickWasActive` now set unconditionally while stick is held; camera angle updates only on stick release.
- **~1 second mod disable when crossing room doors** — added fast-track condition: when bit4 first appears during the release countdown (hardCut cleared, no letterbox, `normalNow = true`), `s_ignoreStuckBit4` is set immediately instead of waiting the full 15-frame suspect window. Reduces effective door delay from ~0.9 s to ~0.4 s.
- **Running breaks mod** — `r1PreCut` condition now gated on `*PL_MOVE_NO == 0`. Running sets `moveNo = 4`; pre-cutscene door walk-ins use `moveNo = 0`. Prevents R1 flicker during running from latching CutsceneGuard.
- **Door to room 4 permanently kills mod** — `s_ignoreStuckBit4 = true` moved from inside `else if (!latched)` branch to the unconditional bit4 counter section. Stuck bit4 can now be detected and dismissed even during an active latch.

---

## [0.5.0] - 2026-03-12 (Codex session)

> **Regression notice**: the final build from this session broke the mod completely — analog never activated, permanent tank controls. Treat SHA256 `87BA768D...` as bad. The last known partially-working build from this session is `6DDC9124...` (23:14 deployment).

### Added
- 3-state `CutsceneGuard` state machine (suspect → latched → hard) replacing simple bit-check; prevents false positives from stuck bits and false negatives during short idle gaps between cutscene animations
- `ScriptLatch` — secondary guard for scripted sequences that don't set standard cutscene bits; triggers on STATUS patterns `0x80810000` / `0x80910000` / `0x80930000` with `R0 != 1` and `moveNo == 0`
- `GetRoomSignature()` — content hash of room data for reliable room change detection (raw `PROOM` pointer could be reused across rooms)
- `g_RoomChangeEpoch` / `g_RoomChangeFrames` — ScriptLatch alternate release path after room change stabilizes (`release=room`)
- `CineUiState` / `CineUiGuard` — reads `nPlayMovie`, `Movie_usevfw`, `Marni_tile`, `Marni_line` as FMV cutscene signals

### Fixed
- ScriptLatch no longer holds indefinitely after room transitions — room-change epoch provides an alternate release path

### Removed
- `nPlayMovie` as a hard block signal — caused mod to stay disabled during normal gameplay; demoted to diagnostic only

### Regression introduced
- Final tightening pass added `(PL_ST_FLG & 0x80) == 0` and `(PL_BE_FLG & 0x10) != 0` as release requirements; over-constrained activation, mod never enabled

---

## [0.4.0] - 2026-03-12

### Added
- Background watchdog thread polling `ReinstallHookIfNeeded()` every 4 ms — hook now self-heals even when `DoAnalog3D()` itself is not being called

### Fixed
- **`STATUS & 0x4` (bit4) false positive after door transitions** — bit4 now treated as a cutscene signal only when `R0 != 1`; during normal gameplay (`R0 == 1`) persistent bit4 is ignored

---

## [0.3.0] - 2026-03-12

### Added
- `ReinstallHookIfNeeded()` — checks whether the JMP detour at `0x483527` still points to `MidHookStub`; reinstalls if overwritten by REBirth during room transitions
- Hook status logging: `HookCheck: OK` / `HookCheck: DEAD, reinstalling`
- SEH (`__try/__except`) around `g_OriginalFn()` call to survive hook failures without crashing
- Guard: no hook reinstall while `R0 == 0` (unsafe mid-transition state)

### Fixed
- **Mod stops working after room transitions** — REBirth was overwriting the JMP detour at `0x483527` during every room load; hook now reinstalls itself automatically

---

## [0.2.0] - 2026-03-05 — Initial release

### Added
- Camera-relative analog stick movement via XInput left stick
- Hook-and-intercept pipeline: `ScanForPadFunction()` byte-pattern scan + JMP trampoline at `0x483527`
- BAM angle system (4096 units = 360°) for player facing direction
- Camera transition buffer: blends from old to new camera angle on `G_CUT_NO` change to prevent 180° direction flips
- Walk/run threshold: stick magnitude < 0.35 → walk, ≥ 0.35 → run
- Stairs detection: `field_CA == 7` locks movement axis
- Aiming and menu suppression
- Debug logging to `analog3d_debug.log`
- RE1-Mod-SDK integration (5-function export interface)

---

[Unreleased]: https://github.com/Rebrond/re1-analog-3d/compare/v0.9.0...HEAD
[0.9.0]: https://github.com/Rebrond/re1-analog-3d/compare/v0.7.5...v0.9.0
[0.7.5]: https://github.com/Rebrond/re1-analog-3d/compare/v0.7.2...v0.7.5
[0.7.2]: https://github.com/Rebrond/re1-analog-3d/compare/v0.5.0...v0.7.2
[0.5.0]: https://github.com/Rebrond/re1-analog-3d/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/Rebrond/re1-analog-3d/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/Rebrond/re1-analog-3d/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/Rebrond/re1-analog-3d/releases/tag/v0.2.0
