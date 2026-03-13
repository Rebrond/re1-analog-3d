# Analog 3D Mod — Changelog

All dates are approximate based on session logs.

---

## [0.1.0] — 2026-03-09 — Initial analysis session

- Analyzed RE1-Mod-SDK structure, GLOBAL/PLAYER_WORK/ENEMY_WORK structs, SDK exports
- Analyzed RE2 and RE3 reference implementations for porting patterns
- Established BAM angle system understanding (4096 units = 360°)
- No code changes in this session

---

## [0.2.0] — Initial release (pre-session)

- First working build released to git
- Basic camera-relative analog stick movement via XInput
- Hook-and-intercept pipeline: `ScanForPadFunction()` + JMP trampoline at `0x483527`
- Camera transition buffer: smooth angle blending on `G_CUT_NO` changes
- Walk/run threshold: stick magnitude < 0.35 → walk, ≥ 0.35 → run
- Stairs detection: `field_CA == 7` locks movement axis
- Aiming/menu suppression
- Basic debug log to `analog3d_debug.log`

---

## [0.3.0] — 2026-03-12 (session 1) — Hook death fix

**Problem**: Mod stopped working after room transitions. Every time the player crossed a door into a new room, the analog stick reverted to tank controls permanently.

**Root cause**: REBirth's room-transition code overwrote the JMP detour at `0x483527` that the mod had installed. After the overwrite, `HookedPadFunction()` was no longer being called.

**Fixes**:
- Added `ReinstallHookIfNeeded()` — checks whether the JMP at `0x483527` still points to `MidHookStub`; if not, reinstalls it
- Added guard: no reinstall while `R0 == 0` (unsafe mid-transition state)
- Added hook status logging: `HookCheck: OK` / `HookCheck: DEAD, reinstalling`
- Added SEH (`__try/__except`) around `g_OriginalFn()` call to survive any future hook failures without crashing

**Build system established**:
- `build_and_deploy.bat` — correct platform is `x86` (not `Win32`)
- Auto-copies to `D:\GOG Galaxy\Games\Resident Evil\mod_analog3d\analog3d.dll`

**Status**: Fixed and confirmed working.

---

## [0.4.0] — 2026-03-12 (session 2) — Watchdog thread + bit4 false positive fix

**Problem 1**: `ReinstallHookIfNeeded()` called only from `DoAnalog3D()` — if the hook was dead, `DoAnalog3D()` itself was not being called, so reinstall never ran.

**Fix**: Added background watchdog thread that calls `ReinstallHookIfNeeded()` every 4 ms independently of the main hook path. Hook now self-heals even when completely dead.

**Problem 2**: `STATUS & 0x4` (bit4) stayed stuck after door transitions. This caused mod to remain disabled long after entering a new room even though no cutscene was active.

**Fix**: Treat `0x4` as a real cutscene signal only when `R0 != 1`. During normal gameplay (`R0 == 1`), persistent bit4 is ignored.

**Status**: Fixed and confirmed working.

---

## [0.5.0] — 2026-03-12 (Codex session) — CutsceneGuard state machine

*Note: This session was performed by a separate AI (Codex). It introduced significant new infrastructure but also caused a regression at the end.*

### What was added:

**3-state CutsceneGuard** — replaced the simple bit-check with a suspect → latched → hard state machine to prevent both false positives (stuck bits causing permanent block) and false negatives (short idle gaps between cutscene animations allowing analog to leak through).

**ScriptLatch** — secondary guard for later scripted scenes that don't set the standard cutscene bits. Triggers on `STATUS` patterns like `0x80810000`, `0x80910000`, `0x80930000` with `R0 != 1` and `moveNo == 0`. Releases after a stable gameplay window or room-change epoch.

**Room content fingerprint** (`GetRoomSignature()`) — more reliable room change detection than raw `PROOM` pointer, which could be reused across rooms.

**CineUiState / CineUiGuard** — added `nPlayMovie`, `Movie_usevfw`, `Marni_tile`, `Marni_line` as diagnostic and blocking signals for FMV cutscenes.

**g_RoomChangeEpoch / g_RoomChangeFrames** — ScriptLatch can release after a room change stabilizes (alternate release path: `release=room`).

### Deployments during this session:
- `2026-03-12 22:13` — SHA256 `8069F637...` (CutsceneGuard v1)
- `2026-03-12 22:25` — SHA256 `04B74502...` (ScriptLatch v1)
- `2026-03-12 22:47` — SHA256 `70369F1A...` (ScriptLatch v2)
- `2026-03-12 22:58` — SHA256 `C50A0A05...` (CineUiState added)
- `2026-03-12 23:04` — SHA256 `3BBB5654...` (nPlayMovie demoted to diagnostic only)
- `2026-03-12 23:14` — SHA256 `6DDC9124...` (room-based ScriptLatch release — last known partially-working build)

### Regression — final Codex build:

The last tightening pass added very strict release conditions:
- `(PL_ST_FLG & 0x80) == 0`
- `(PL_BE_FLG & 0x10) != 0`
- `R0 == 1`, `R1 == 0`, `moveNo == 0`, `moveCnt == 0` all required simultaneously

**Result**: Mod stopped activating at all. Effectively permanent tank controls. User reported: *"Mod całkowicie teraz nie działa, jest ciągle tank controls."*

Deployed: `2026-03-12 23:34` — SHA256 `87BA768D...` — **marked BAD**

---

## [0.6.0] — 2026-03-13 (session 1) — Regression fix + letterbox integration

### Fix 1: Running breaks mod (r1PreCut false trigger)

**Problem**: Holding the run button caused `R1` to flicker 0→1→0 every few frames when `moveNo=4` (running animation). Each flicker fired the `r1PreCut` condition, latching CutsceneGuard. Mod blocked during running.

**Fix**: Added `&& (*PL_MOVE_NO == 0)` to the `r1PreCut` condition. Pre-cutscene door walk-ins have `moveNo=0`; running is `moveNo=4`. The two states are now distinguished.

### Fix 2: Door to room 4 kills mod (s_ignoreStuckBit4 not firing while latched)

**Problem**: After a hardCut latch (from the door cutscene), entering room 4 set `STATUS bit4` permanently. `s_ignoreStuckBit4` was inside the `else if (!latched)` branch — it never fired while latched, so bit4 was never dismissed and the mod never recovered.

**Fix**: Moved `s_ignoreStuckBit4 = true` into the bit4 counter section which runs unconditionally. Stuck bit4 is now detectable and dismissable even during an active latch.

### Fix 3: G_LETTERBOX as primary cutscene signal

**Discovery**: User found via Cheat Engine that `0x00C330A4` (`G_BASE + 0x14`) is the letterbox bar height written by `StMask()`. Value: 0 = no bars, 240 = full bars. Fades back to 0 via `sub byte ptr [00C330A4],10` at `0x004846D3`.

**Integration**:
- `#define G_LETTERBOX ((volatile u8*)(G_BASE + 0x14))`
- `letterbox > 0` → immediate CutsceneGuard latch
- `stableGameplay` check requires `!letterbox` before release countdown begins
- Letterbox transitions logged: `Letterbox: N -> M`
- Old heuristics (hardCut, bit4, r1PreCut) kept as belt-and-suspenders

### Diagnostic additions:
- **G_CV_A–F** (`0x4CB23C–0x4CB29C`) — 6 addresses found via Cheat Engine that change during cutscenes. Spaced 0x18 apart (likely array of structs). Semantics unknown. Logged via `CutVars:` on any change.
- **FLG5 bit 0x0100** — transitions logged for observation. 0 during scripted states, 1 during gameplay.

**Status**: Built and deployed 2026-03-13. Not yet fully confirmed — further testing needed.

---

## [0.6.1] — 2026-03-13 (session 2) — 1 MB log cap variant

**What**: Created `dllmain_logreduced.cpp` as a copy of `dllmain.cpp` with a capped ring-buffer logger.

**Why**: `analog3d_debug.log` could grow without bound during long play sessions. User wanted a variant that caps at 1 MB.

**How**: Added `LogWriteRaw()` that wraps write position back to start of data region when next write would exceed 1 MB. Header written once; subsequent wraps overwrite old data. `LogInit()` opens in `w+b` mode.

**Observations from that run**: Log was only ~58 KB (490 lines, 52 heartbeats). The 1 MB ceiling was not actually reached. The wrap-on-full behavior was not exercised.

**Note**: User initially reported mod stopped working after deploying this DLL, then retracted: *"Zapomnij o problemie, moja wina."* (Forget about it, my fault.) The reduced logger was not confirmed broken.

**File**: `dllmain_logreduced.cpp` / `Release\dllmain_logreduced.dll`

The 1 MB cap logic was later merged into the main `dllmain.cpp` logger.

---

## [0.7.0] — 2026-03-13 (session 3) — 1-frame direction snap fix

**Problem**: During rapid camera cut changes, the player's direction would snap by ~180° for one frame, then return to the correct direction. Visible as a single-frame lurch/twitch when the analog stick was held and a camera cut fired.

**Root cause analysis**: Log showed `cdir` jumping from 2441 to 1796 (a ~645 BAM / ~57° snap) at lines 561–562 with no room change event. Traced to the X-dominant branch in `ApplyAnalog3D()`:

```cpp
// OLD CODE — caused the bug
float absX = stickX < 0 ? -stickX : stickX;
float absY = stickY < 0 ? -stickY : stickY;
if (absY >= absX) {
    g_StickWasActive = true;
} else {
    g_ActiveCamAngle = currentCam;   // <-- reset camera buffer mid-hold
    g_StickWasActive = false;
}
```

When `|stickX| > |stickY|` (stick pushed mostly sideways), `g_ActiveCamAngle` was reset to the new camera and `g_StickWasActive` was set false. On the very next frame, the camera buffer re-initialized to the new camera, bypassing the cut smoothing for one frame and causing the snap.

**Fix**: Removed the X-dominant branch entirely. `g_StickWasActive = true` unconditionally while stick is held. Camera angle update only happens on stick release (magnitude < 0.01).

```cpp
// NEW CODE — comment explains the reasoning
/* Buffer camera angle for all stick directions while stick is held.
   The old X-dominant override (sideways always used currentCam) caused
   1-frame direction snaps: when the stick briefly crossed the X=Y
   boundary near a camera cut, g_ActiveCamAngle would snap to the new
   camera for one frame, producing a visible direction lurch.
   Camera angle update now only happens on stick release (mag < 0.01). */
g_StickWasActive = true;
```

**Trade-off**: After releasing and re-pressing the stick, the new camera is picked up immediately. The old stick-held-through-cut behavior is now the only case where the old camera persists — which is the desired behavior anyway.

**User feedback**: *"This trade-off is actually very good, controls are now much more natural."*

**Status**: Fixed and confirmed working.

---

## [0.7.1] — 2026-03-13 (session 3) — Door disable fast-track fix

**Problem**: Entering certain rooms (notably room 4) caused the mod to disable for approximately 1 second before re-enabling, even though no real cutscene was playing.

**Root cause analysis**: Log from the 0.7.0 run showed the cross-door transition at lines 1508–1516:
- Lines 1508–1513: `hardCut` fires (door cutscene — STATUS contains 0x40000000/0x02000000)
- Line 1514: `STATUS=0x9000000C` — bit4 appears (room 4 permanent flag)
- Line 1516: `s_ignoreStuckBit4 = true` fires, CutsceneGuard releases

The delay came from `BIT4_SUSPECT_FRAMES = 15` — the mod had to wait 15 frames of bit4 being present before declaring it "stuck" (permanent, not a real cutscene). Combined with `CUTSCENE_RELEASE_FRAMES = 12`, total effective delay was 27 frames ≈ 0.9 seconds.

**Fix**: Added `fastTrackStuck` condition to bypass the 15-frame wait when bit4 first appears during the release countdown:

```cpp
bool fastTrackStuck = s_cutsceneLatched && !hardCut && !letterbox && normalNow;
if (!s_ignoreStuckBit4 && (fastTrackStuck || s_bit4Frames > BIT4_SUSPECT_FRAMES) && normalNow)
    s_ignoreStuckBit4 = true;
```

When `fastTrackStuck == true`: bit4 appears during release countdown → immediately mark it stuck → only `CUTSCENE_RELEASE_FRAMES (12)` delay remains ≈ 0.4 seconds.

**Status**: Built and deployed. Awaiting user confirmation.

---

## [0.7.2] — 2026-03-13 — Snapshot

- Snapshot of source and DLL taken at this version
- Files saved to `0.7.2/dllmain.cpp` and `0.7.2/analog3d.dll`
- All fixes in 0.7.0 and 0.7.1 are included

---

## Known remaining issues / future investigation

- **1-second door disable** (0.7.1): Reduced from ~0.9s to ~0.4s. May still be noticeable. Could further reduce `CUTSCENE_RELEASE_FRAMES` or eliminate it for the fast-track path.
- **CutVars (G_CV_A–F)**: Semantics of the 6 CE addresses still unknown. Need to identify which one is the most reliable / earliest cutscene indicator.
- **FLG5 bit 0x0100**: Behavioral role not yet characterized.
- **ScriptLatch**: Not triggered in recent test runs with G_LETTERBOX active. May be redundant or may still be needed for specific scripted sequences not covered by letterbox.
- **nPlayMovie / Movie_usevfw / Marni**: Diagnostic only — not yet proven necessary given letterbox coverage.
