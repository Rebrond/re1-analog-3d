# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

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

[Unreleased]: https://github.com/Rebrond/re1-analog-3d/compare/v0.7.2...HEAD
[0.7.2]: https://github.com/Rebrond/re1-analog-3d/compare/v0.5.0...v0.7.2
[0.5.0]: https://github.com/Rebrond/re1-analog-3d/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/Rebrond/re1-analog-3d/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/Rebrond/re1-analog-3d/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/Rebrond/re1-analog-3d/releases/tag/v0.2.0
