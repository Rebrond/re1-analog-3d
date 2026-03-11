/*
    RE1 PC (Classic REBirth / Mod-SDK) - Camera-relative analog 3D movement

    Hooks the game's pad/input function to translate XInput left stick
    into camera-relative directional keys using BAM angles (4096 = 360??).

    Hook strategy:
      1. Runtime-scan .text section for the function that writes G.Key
      2. Install detour (function-start) or mid-hook (write-site)
      3. If scan fails, fall back to a polling thread

    Addresses from RE1-Mod-SDK re1.h GLOBAL struct:
      G_BASE         = 0xC33090
      G.Key          = G + 0x5680 (0xC38710)
      G.Key_trg      = G + 0x5682 (0xC38712)
      G.Cut_no       = G + 0x5662
      G.Pl_work      = G + 0x2124  (PLAYER_WORK)
      G.pRoom        = G + 0x7B10  (RDT_HEADER*)

    RCUT (from re1.h, 0x2C bytes per entry, inline at RDT_HEADER + 0x94):
      Camera facing = atan2(target.x - pos.x, target.z - pos.z)
      Player target = stick_angle + camera_facing
*/

#include "framework.h"
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed short   s16;
typedef signed int     s32;

/* ================================================================
   ENGINE CONSTANTS & MEMORY LAYOUT
   ================================================================ */
#define G_BASE      0xC33090U
#define BAM_MAX     4096        /* Full circle in BAM units */
#define PI_F        3.14159265f

#define XINPUT_DEADZONE 7849
#define WALK_THRESHOLD  0.35f

   /* RE1 directional key bits (from re1.h) */
#define KEY_FORWARD  0x0001
#define KEY_RIGHT    0x0002
#define KEY_BACKWARD 0x0004
#define KEY_LEFT     0x0008
#define KEY_DIRMASK  (KEY_FORWARD | KEY_RIGHT | KEY_BACKWARD | KEY_LEFT)
#define KEY_AIM      0x0400
#define KEY_READY    0x0100
#define KEY_CONFIRM  0x4000
#define KEY_CANCEL   0x8000
#define KEY_INTERACT_KEEP_MASK (KEY_CONFIRM | KEY_CANCEL)

/* Game memory pointers ??? all offsets relative to G_BASE */
#define G_KEY_ADDR    (G_BASE + 0x5680)     /* 0xC38710 */
#define G_KEYTRG_ADDR (G_BASE + 0x5682)     /* 0xC38712 */
#define G_KEY         ((volatile u16*)G_KEY_ADDR)
#define G_KEY_TRG     ((volatile u16*)G_KEYTRG_ADDR)
#define G_CUT_NO      ((volatile u8*)(G_BASE + 0x5662))
#define G_PL_LIFE     ((volatile s16*)(G_BASE + 0x567E))
#define G_PL_CDIR_Y   ((volatile s16*)(G_BASE + 0x5690))  /* Save-area copy */
#define G_PROOM       ((volatile u32*)(G_BASE + 0x7B10))

/* PLAYER_WORK fields (Pl_work starts at G + 0x2124) */
#define PL_BE_FLG     ((volatile u8*)(G_BASE + 0x2124))   /* Entity active flag */
#define PL_ROUTINE_0  ((volatile u8*)(G_BASE + 0x21A8))   /* State machine major */
#define PL_ROUTINE_1  ((volatile u8*)(G_BASE + 0x21A9))   /* State machine minor */
#define PL_CDIR_X     ((volatile s16*)(G_BASE + 0x2196))  /* Player pitch/tilt */
#define PL_CDIR_Y     ((volatile s16*)(G_BASE + 0x2198))  /* Player facing angle */
#define PL_CDIR_Z     ((volatile s16*)(G_BASE + 0x219A))  /* Player roll/tilt */
#define PL_MAT        ((volatile s16*)(G_BASE + 0x2144))  /* Player root 3x3 matrix */
#define PL_HOKAN_FLG  ((volatile u8*)(G_BASE + 0x21B0))
#define PL_MOVE_NO    ((volatile u8*)(G_BASE + 0x21E0))
#define PL_MOVE_CNT   ((volatile u8*)(G_BASE + 0x21E1))   /* Movement sequence ID */
#define PL_FIELD_C2   ((volatile u16*)(G_BASE + 0x21E6))
#define PL_FIELD_CA   ((volatile s16*)(G_BASE + 0x21EE))
#define PL_ST_FLG     ((volatile u8*)(G_BASE + 0x2200))
#define PL_FIELD_E0   ((volatile u16*)(G_BASE + 0x2204))
#define PL_POS_X      ((volatile s32*)(G_BASE + 0x2158))
#define PL_POS_Z      ((volatile s32*)(G_BASE + 0x2160))

/* RCUT camera structure (from re1.h, 0x2C bytes per entry) */
#pragma pack(push, 1)
struct RCUT {
    u32 pSp;
    u32 pTim;
    s32 View_p[3];  /* Camera position (x, y, z) */
    s32 View_r[3];  /* Camera target   (x, y, z) */
    u32 Zero[2];
    u32 ViewR;
};
#pragma pack(pop)

/* ================================================================
   XINPUT TYPES & LOADER
   ================================================================ */

#pragma pack(push, 1)
typedef struct {
    WORD  wButtons;
    BYTE  bLeftTrigger, bRightTrigger;
    SHORT sThumbLX, sThumbLY;
    SHORT sThumbRX, sThumbRY;
} MY_XINPUT_GAMEPAD;

typedef struct {
    DWORD dwPacketNumber;
    MY_XINPUT_GAMEPAD Gamepad;
} MY_XINPUT_STATE;
#pragma pack(pop)

typedef DWORD(WINAPI* XInputGetStateFn)(DWORD, MY_XINPUT_STATE*);
static XInputGetStateFn g_XInputGetState = NULL;

/* ================================================================
   LOGGING
   Writes analog3d_debug.log to the game directory.
   One "State:" line per state change; burst window around events.
   ================================================================ */

static FILE* g_LogFile = NULL;

static void LogInit() {
    fopen_s(&g_LogFile, "analog3d_debug.log", "w");
    if (g_LogFile)
        fprintf(g_LogFile, "=== RE1 Analog 3D Controls - Debug Log ===\n\n");
}

static void Log(const char* fmt, ...) {
    if (!g_LogFile) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_LogFile, fmt, args);
    va_end(args);
    fprintf(g_LogFile, "\n");
    fflush(g_LogFile);
}

static void LogClose() {
    if (g_LogFile) {
        fprintf(g_LogFile, "\n=== End of log ===\n");
        fclose(g_LogFile);
        g_LogFile = NULL;
    }
}

/* ================================================================
   HOOK STATE
   ================================================================ */

typedef void(__cdecl* GameFn)(void);

enum HookKind {
    HOOK_KIND_NONE = 0,
    HOOK_KIND_FUNCTION_START,   /* Detour hook at function prologue */
    HOOK_KIND_WRITE_SITE        /* Mid-hook at G.Key write instruction */
};

static GameFn   g_OriginalFn = NULL;
static u8* g_Trampoline = NULL;
static u8* g_MidHookStub = NULL;
static u8* g_HookedAddr = NULL;
static HookKind g_ScannedPadHookKind = HOOK_KIND_NONE;
static bool     g_UsingHook = false;

/* Fallback polling thread (used if hook installation fails) */
#define POLL_SLEEP_MS 4
static HANDLE g_StopEvent = NULL;
static HANDLE g_WorkerThread = NULL;

/* ================================================================
   ANALOG 3D STATE
   ================================================================ */

   /* Camera transition buffer: prevents 180?? flips on camera cuts.
      Keeps using old camera angle while stick is held during a cut change. */
static s16  g_ActiveCamAngle = 0;
static s16  g_LastCutNo = -1;
static bool g_StickWasActive = false;
static u16  g_LastAnalogDir = 0;
static bool g_InteractPassActive = false;
static bool g_LastInteractionFlag = false;
static bool g_HaveTrackedStickDir = false;
static u16  g_TrackedStickDir = 0;
static u16  g_InteractPassDir = 0;
static u16  g_InteractPassKey = 0;
static u8   g_TrackedStickFrames = 0;

/* Stairs handling timing constants */
const DWORD SPECIAL_STATE_LOCK_MS = 60;    /* Hard block after stair animation ends (~2 frames at 30fps) */
const float INTERACT_PASS_ENTRY_MIN_MAG = 0.55f;
const float INTERACT_PASS_HOLD_MIN_MAG = 0.35f;
const u16   INTERACT_PASS_DIR_TOLERANCE = 256; /* ~22.5 degrees */
const u8    INTERACT_PASS_STABLE_FRAMES = 3;

/* Stairs state machine */
static DWORD g_SpecialStateLockUntil = 0;

/* Stairs alignment: locks facing to nearest cardinal axis during traversal */
static bool  g_StairsAlignActive = false;
static bool  g_StairsLockX = false;
static bool  g_StairsLockZ = false;
static bool  g_HaveStableGroundPos = false;
static s32   g_LastStablePosX = 0;
static s32   g_LastStablePosZ = 0;
static bool  g_StairsForcedDirValid = false;
static u16   g_StairsForcedDir = 0;
static bool  g_InteractFacingLockActive = false;
static s16   g_InteractFacingDirX = 0;
static u16   g_InteractFacingDir = 0;
static s16   g_InteractFacingDirZ = 0;
static s16   g_InteractMatrix[9] = { 0 };

enum { SPECIAL_BLOCK_STAIRS = 0x01, SPECIAL_BLOCK_INTERACT = 0x02 };

/* Global game state flags (from re1.h GLOBAL struct, G_BASE+0 and G_BASE+4) */
#define G_STATUS_FLG   ((volatile u32*)(G_BASE + 0x0))
#define G_SYSTEM_FLG   ((volatile u32*)(G_BASE + 0x4))
#define G_FLG5         ((volatile u16*)(G_BASE + 0x7B00))

/* ================================================================
   XINPUT
   ================================================================ */

static bool InitXInput() {
    const char* dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll", NULL };
    for (int i = 0; dlls[i]; i++) {
        HMODULE h = LoadLibraryA(dlls[i]);
        if (h) {
            g_XInputGetState = (XInputGetStateFn)GetProcAddress(h, "XInputGetState");
            if (g_XInputGetState) return true;
        }
    }
    return false;
}

/* Read left analog stick with deadzone. Returns true if controller connected. */
static bool ReadLeftStick(float* outX, float* outY, float* outMag) {
    if (!g_XInputGetState) return false;
    MY_XINPUT_STATE state = {};
    if (g_XInputGetState(0, &state) != 0) return false;

    float lx = (float)state.Gamepad.sThumbLX;
    float ly = (float)state.Gamepad.sThumbLY;
    float mag = sqrtf(lx * lx + ly * ly);

    if (mag < (float)XINPUT_DEADZONE) {
        *outX = *outY = *outMag = 0.0f;
        return true;
    }
    *outX = lx / 32767.0f;
    *outY = ly / 32767.0f;
    *outMag = mag / 32767.0f;
    if (*outMag > 1.0f) *outMag = 1.0f;
    return true;
}

/* ================================================================
   BAM ANGLE UTILITIES
   Binary Angle Measurement: 0=North, 1024=East, 2048=South, 3072=West
   ================================================================ */

   /* Wrap BAM delta to signed range [-2048, +2047] */
static s16 WrapBamDelta(s32 value) {
    value &= (BAM_MAX - 1);
    if (value >= BAM_MAX / 2) value -= BAM_MAX;
    return (s16)value;
}

static s16 AbsBam(s16 v) { return v < 0 ? -v : v; }

/* True if direction is closer to X-axis (East/West) than Z-axis (North/South) */
static bool IsBamMostlyXAxis(u16 dir) {
    s16 toN = AbsBam(WrapBamDelta(dir));
    s16 toS = AbsBam(WrapBamDelta(dir - BAM_MAX / 2));
    s16 toE = AbsBam(WrapBamDelta(dir - BAM_MAX / 4));
    s16 toW = AbsBam(WrapBamDelta(dir - 3 * BAM_MAX / 4));
    return (toE < toW ? toE : toW) < (toN < toS ? toN : toS);
}

/* Snap BAM direction to nearest cardinal axis (N/E/S/W) */
static u16 SnapBamToNearestAxis(u16 dir) {
    const u16 axes[] = { 0, BAM_MAX / 4, BAM_MAX / 2, 3 * BAM_MAX / 4 };
    u16 best = 0;
    s16 bestDelta = 0x7FFF;
    for (int i = 0; i < 4; i++) {
        s16 d = AbsBam(WrapBamDelta((s32)dir - (s32)axes[i]));
        if (d < bestDelta) { bestDelta = d; best = axes[i]; }
    }
    return best;
}

/* ================================================================
   GAME STATE CHECKS
   ================================================================ */

static bool IsInGameplay() {
    return *G_PROOM != 0 && *G_PL_LIFE > 0 && *PL_BE_FLG != 0;
}

/* Player is in a basic movement state (idle/walk/run/turn/quickturn) */
static bool IsNormalMovement() {
    if (*PL_ROUTINE_0 > 1) return false;
    switch (*PL_ROUTINE_1) {
    case 0: case 1: case 2: case 3: case 4: case 0x0C:
        return true;
    default:
        return false;
    }
}

static bool IsAiming() {
    u16 key = *G_KEY;
    return (key & KEY_AIM) || (key & KEY_READY) || (*PL_ROUTINE_1 == 5);
}

static bool IsInteractionFlagActive() {
    /* Empirically from logs: Flg_5 = 0xFF00 during item/object prompt states. */
    return *G_FLG5 == 0xFF00;
}

static bool IsItemPickupFreezeActive() {
    /* Item pickup has distinct phases we must block:
         Pre-pickup (lean/approach): STATUS=0x90000000, PL_ST_FLG bit 0x80 set
         Phase 1 (approach freeze):  STATUS bit 0x0800 set (0x90000800, 0x70000800)
         Phase 2 (full freeze):      STATUS bits 0x8800 set (0x50008800)
       PL_ST_FLG & 0x80 catches the pre-pickup frames before 0x0800 appears.
       STATUS & 0x0800 catches all subsequent phases.
       Neither bit fires during normal movement, stairs, doors, or combat. */
       /* moveCnt==4 is the run-approach animation that fires when the player
          presses confirm while running toward an item. STATUS & 0x0800 and
          PL_ST_FLG & 0x80 do not set until the animation completes, so without
          this check the character rotates freely during the approach frames. */
    return (*G_STATUS_FLG & 0x00000800) != 0 ||
        (*PL_ST_FLG & 0x80) != 0 ||
        (*PL_MOVE_CNT == 4);
}

static void ResetInteractionPassThrough() {
    g_InteractPassActive = false;
    g_LastInteractionFlag = false;
    g_HaveTrackedStickDir = false;
    g_TrackedStickDir = 0;
    g_InteractPassDir = 0;
    g_InteractPassKey = 0;
    g_TrackedStickFrames = 0;
}

static void UpdateInteractionPassThrough(bool haveStick, float mag, float stickX, float stickY) {
    bool interactionFlag = IsInteractionFlagActive();
    bool haveDir = haveStick && mag >= INTERACT_PASS_HOLD_MIN_MAG;
    u16 rawStickDir = 0;

    if (haveDir) {
        float rad = atan2f(stickX, stickY);
        rawStickDir = (u16)(((s16)(rad / PI_F * (BAM_MAX / 2))) & (BAM_MAX - 1));

        if (g_HaveTrackedStickDir &&
            AbsBam(WrapBamDelta((s32)rawStickDir - (s32)g_TrackedStickDir)) <= (s16)INTERACT_PASS_DIR_TOLERANCE) {
            if (g_TrackedStickFrames < 255) g_TrackedStickFrames++;
        }
        else {
            g_HaveTrackedStickDir = true;
            g_TrackedStickDir = rawStickDir;
            g_TrackedStickFrames = 1;
        }
    }
    else {
        g_HaveTrackedStickDir = false;
        g_TrackedStickFrames = 0;
    }

    if (interactionFlag && !g_LastInteractionFlag &&
        haveStick && mag >= INTERACT_PASS_ENTRY_MIN_MAG &&
        g_HaveTrackedStickDir && g_TrackedStickFrames >= INTERACT_PASS_STABLE_FRAMES) {
        g_InteractPassActive = true;
        g_InteractPassDir = g_TrackedStickDir;
        g_InteractPassKey = g_LastAnalogDir ? g_LastAnalogDir : KEY_FORWARD;
    }

    if (g_InteractPassActive && !interactionFlag) {
        g_InteractPassActive = false;
        g_InteractPassKey = 0;
    }

    g_LastInteractionFlag = interactionFlag;
}


/* ================================================================
   DIRECTION KEY HELPERS
   ================================================================ */

   /* Override direction keys, generating edge triggers for newly-pressed keys */
static void ApplyAnalogDirectionKey(u16 dirKey) {
    *G_KEY = (*G_KEY & (u16)~KEY_DIRMASK) | dirKey;
    *G_KEY_TRG = (*G_KEY_TRG & (u16)~KEY_DIRMASK) | (dirKey & (u16)~g_LastAnalogDir);
    g_LastAnalogDir = dirKey;
}

/* During prompts/pickups, preserve only confirm/cancel so latent pad bits
   cannot keep rotating the player while the game freezes movement. */
static void ApplyInteractionInputFilter() {
    *G_KEY &= (u16)KEY_INTERACT_KEEP_MASK;
    *G_KEY_TRG &= (u16)KEY_INTERACT_KEEP_MASK;
    g_LastAnalogDir = 0;
}

static void ResetAnalogState() {
    g_StickWasActive = false;
    g_LastAnalogDir = 0;
}

/* ================================================================
   STAIRS HANDLING
   RE1 uses movement sequences 51-54 for stair traversal. During
   these animations analog input is suppressed to avoid fighting
   the scripted movement. After stairs, a brief ease-in period
   prevents sudden jerky movement.
   ================================================================ */

static void ResetStairsAlignment() {
    g_StairsAlignActive = false;
    g_StairsLockX = g_StairsLockZ = false;
}

static void ResetInteractionFacingLock() {
    g_InteractFacingLockActive = false;
}

static void ApplyInteractionFacingLock() {
    if (!g_InteractFacingLockActive) {
        g_InteractFacingDirX = *PL_CDIR_X;
        g_InteractFacingDir = ((u16)*PL_CDIR_Y) & (BAM_MAX - 1);
        g_InteractFacingDirZ = *PL_CDIR_Z;
        for (int i = 0; i < 9; i++)
            g_InteractMatrix[i] = PL_MAT[i];
        g_InteractFacingLockActive = true;
    }

    *PL_CDIR_X = g_InteractFacingDirX;
    *PL_CDIR_Y = (s16)g_InteractFacingDir;
    *G_PL_CDIR_Y = (s16)g_InteractFacingDir;
    *PL_CDIR_Z = g_InteractFacingDirZ;
    for (int i = 0; i < 9; i++)
        PL_MAT[i] = g_InteractMatrix[i];
}

/* Lock player facing to nearest cardinal axis during stair animation */
static void ApplyStairsAlignment() {
    if (!g_StairsAlignActive) {
        u16 facing = ((u16)*PL_CDIR_Y) & (BAM_MAX - 1);
        g_StairsAlignActive = true;
        g_StairsLockX = !IsBamMostlyXAxis(facing);
        g_StairsLockZ = !g_StairsLockX;
        g_StairsForcedDir = SnapBamToNearestAxis(facing);
        g_StairsForcedDirValid = true;
    }
    if (g_StairsForcedDirValid) {
        *PL_CDIR_Y = (s16)g_StairsForcedDir;
        *G_PL_CDIR_Y = (s16)g_StairsForcedDir;
    }
}

/* Returns bitmask of active special-state blocks (e.g. stairs).
   Implements a state machine: animation active ??? lock period ???
   wait for stick neutral ??? ease-in period ??? free. */
static u32 GetSpecialBlockMask(bool haveStick, float mag) {
    u32 mask = 0;
    DWORD now = GetTickCount();

    u8 moveCnt = *PL_MOVE_CNT;

    /* Runtime interaction flag (prompt/object state). */
    if (IsInteractionFlagActive())
        mask |= SPECIAL_BLOCK_INTERACT;

    if (IsItemPickupFreezeActive())
        mask |= SPECIAL_BLOCK_INTERACT;

    /* Movement sequences 51-54 = stair traversal animations.
       Renew the hard timer every frame while the animation is active. */
    if (moveCnt >= 51 && moveCnt <= 54) {
        g_SpecialStateLockUntil = now + SPECIAL_STATE_LOCK_MS;
        mask |= SPECIAL_BLOCK_STAIRS;
    }

    /* SYS flag 0x400000 = stair approach zone (exclusive to stair contexts in logs:
       only appears with move=4/2, 4/3, 4/51, 4/53 and never in other states).
       When the zone is active but animation hasn't started yet (moveCnt < 51),
       the game uses hokan interpolation to align the character with the stair axis.
       If the mod writes a new PL_CDIR_Y every frame from the analog stick, hokan
       restarts each frame and never completes, so the game never fires the animation.
       Blocking here lets ApplyStairsAlignment() snap to the nearest cardinal axis
       and write KEY_FORWARD, giving the game a stable facing to trigger against. */
    if ((*G_SYSTEM_FLG & 0x400000) != 0 && !(mask & SPECIAL_BLOCK_STAIRS)) {
        mask |= SPECIAL_BLOCK_STAIRS;
    }

    /* After the animation ends: keep blocking for exactly SPECIAL_STATE_LOCK_MS
       (~60ms / ~2 frames) to let the game finalize the stair position, then
       release unconditionally regardless of stick state.
       There is no neutral-wait or ease-in: the alignment snapped the player to
       a cardinal axis so there is no direction jump on release, and blocking
       until stick-neutral caused an indefinite freeze when the user held the
       stick through the animation. */
    if (now < g_SpecialStateLockUntil)
        mask |= SPECIAL_BLOCK_STAIRS;

    return mask;
}


/* ================================================================
   DIAGNOSTICS
   Logs key state on every change. Captures STATUS, PL_ST_FLG,
   PL_ROUTINE, PL_MOVE_CNT, hokan, special mask, and stick magnitude
   so pickup/cutscene flag patterns are visible in the log.
   ================================================================ */

static struct {
    float   mag;
    u16     rawKey;
    u32     specialMask;
    u8      r0, r1, moveNo, moveCnt, hokan, st;
    u32     statusFlg, systemFlg;
    s16     cdirY, saveCdirY;
} g_LastDiag = {};
static bool  g_HasDiag = false;
static DWORD g_LastDiagTick = 0;
static DWORD g_DiagBurstUntil = 0;
static u32   g_LastBlockMask = 0;

static void LogDiagnostics(float mag, u32 specialMask, u16 rawKey) {
    u8  r0 = *PL_ROUTINE_0, r1 = *PL_ROUTINE_1;
    u8  moveNo = *PL_MOVE_NO, moveCnt = *PL_MOVE_CNT;
    u8  hokan = *PL_HOKAN_FLG, st = *PL_ST_FLG;
    u32 statusFlg = *G_STATUS_FLG, systemFlg = *G_SYSTEM_FLG;
    s16 cdirY = *PL_CDIR_Y, saveCdirY = *G_PL_CDIR_Y;
    u8  smask = (u8)specialMask;

    bool changed = !g_HasDiag ||
        r0 != g_LastDiag.r0 || r1 != g_LastDiag.r1 ||
        moveNo != g_LastDiag.moveNo || moveCnt != g_LastDiag.moveCnt ||
        hokan != g_LastDiag.hokan || st != g_LastDiag.st ||
        statusFlg != g_LastDiag.statusFlg || systemFlg != g_LastDiag.systemFlg ||
        smask != g_LastDiag.specialMask ||
        rawKey != g_LastDiag.rawKey ||
        cdirY != g_LastDiag.cdirY || saveCdirY != g_LastDiag.saveCdirY;

    DWORD now = GetTickCount();
    bool burstTrigger = !g_HasDiag ||
        r0 != g_LastDiag.r0 || r1 != g_LastDiag.r1 ||
        moveNo != g_LastDiag.moveNo || moveCnt != g_LastDiag.moveCnt ||
        st != g_LastDiag.st || smask != g_LastDiag.specialMask ||
        rawKey != g_LastDiag.rawKey;
    if (burstTrigger) g_DiagBurstUntil = now + 350;
    DWORD minInterval = (now < g_DiagBurstUntil) ? 16 : 120;

    bool interesting = mag >= 0.10f || rawKey || specialMask || !g_HasDiag;

    if (changed && interesting && (now - g_LastDiagTick >= minInterval)) {
        Log("State: mag=%.2f key=0x%04X mask=0x%X R0=%u R1=%u move=%u/%u hokan=%u st=0x%02X cdir=%d save=%d STATUS=0x%X SYS=0x%X",
            mag, rawKey, specialMask, r0, r1, moveNo, moveCnt, hokan, st,
            cdirY, saveCdirY, statusFlg, systemFlg);
        g_LastDiagTick = now;
    }

    if (specialMask != g_LastBlockMask) {
        if (specialMask)
            Log("Analog blocked: mask=0x%X move=%u/%u st=0x%02X STATUS=0x%X",
                specialMask, moveNo, moveCnt, st, statusFlg);
        else
            Log("Analog block cleared");
        g_LastBlockMask = specialMask;
    }

    g_LastDiag.mag = mag;
    g_LastDiag.rawKey = rawKey;
    g_LastDiag.specialMask = smask;
    g_LastDiag.r0 = r0; g_LastDiag.r1 = r1;
    g_LastDiag.moveNo = moveNo; g_LastDiag.moveCnt = moveCnt;
    g_LastDiag.hokan = hokan; g_LastDiag.st = st;
    g_LastDiag.statusFlg = statusFlg; g_LastDiag.systemFlg = systemFlg;
    g_LastDiag.cdirY = cdirY; g_LastDiag.saveCdirY = saveCdirY;
    g_HasDiag = true;
}

/* ================================================================
   CAMERA ANGLE
   Compute camera facing as BAM angle from RCUT position???target vector.
   ================================================================ */

static s16 GetCameraAngleY() {
    u32 roomAddr = *G_PROOM;
    if (!roomAddr) return 0;

    u8* pRoom = (u8*)roomAddr;
    u8 nCut = pRoom[1];
    if (nCut == 0) return 0;

    /* RCUT array is inline at RDT_HEADER + 0x94 */
    RCUT* pCuts = (RCUT*)(pRoom + 0x94);
    if (IsBadReadPtr(pCuts, sizeof(RCUT))) return 0;

    u8 cutNo = *G_CUT_NO;
    if (cutNo >= nCut) cutNo = 0;

    RCUT* cam = &pCuts[cutNo];
    if (IsBadReadPtr(cam, sizeof(RCUT))) return 0;

    /* Direction vector from camera position to camera target */
    float dx = (float)(cam->View_r[0] - cam->View_p[0]);
    float dz = (float)(cam->View_r[2] - cam->View_p[2]);

    float rad = atan2f(dx, dz);
    s16 bam = (s16)(rad / PI_F * (BAM_MAX / 2));
    /* -90?? correction for RE1's coordinate system */
    return (s16)((bam - BAM_MAX / 4) & (BAM_MAX - 1));
}

/* ================================================================
   CORE ANALOG 3D LOGIC
   Called every frame after the game reads raw pad input.
   Converts analog stick ??? camera-relative movement:
     1. Read stick & compute camera facing angle
     2. Handle camera cuts (buffered angle to prevent 180?? flips)
     3. Handle special states (stairs, aiming, confirm actions)
     4. Combine stick angle + camera angle ??? target BAM direction
     5. Write direction keys and player facing to game memory
   ================================================================ */

static void DoAnalog3D() {
    if (!IsInGameplay()) {
        ResetStairsAlignment();
        ResetInteractionFacingLock();
        ResetInteractionPassThrough();
        g_HaveStableGroundPos = false;
        g_StairsForcedDirValid = false;
        return;
    }

    /* Cutscene guard: STATUS bit 0x4 is exclusively set during all cutscene
       frames. During cutscenes the game scripts character movement via G_KEY
       directly (scripted walk, approach-to-target, etc.). Any key filtering
       or direction write from our mod will break those scripts.
       PL_ST_FLG & 0x80 fires during scripted approach segments (same bit as
       pre-pickup approach), so SPECIAL_BLOCK_INTERACT would trigger and strip
       direction keys, causing the character to never reach its target.
       Solution: complete passthrough - touch nothing. */
    if (*G_STATUS_FLG & 0x00000004) {
        ResetAnalogState();
        return;
    }

    float stickX = 0, stickY = 0, mag = 0;
    bool haveStick = ReadLeftStick(&stickX, &stickY, &mag);
    bool aiming = IsAiming();
    bool normalMove = IsNormalMovement();
    UpdateInteractionPassThrough(haveStick, mag, stickX, stickY);
    u32 specialMask = GetSpecialBlockMask(haveStick, mag);
    LogDiagnostics(haveStick ? mag : 0.0f, specialMask, *G_KEY);

    /* Track last stable ground position (used for stairs reference) */
    if (!specialMask && normalMove && !aiming) {
        g_LastStablePosX = *PL_POS_X;
        g_LastStablePosZ = *PL_POS_Z;
        g_HaveStableGroundPos = true;
        g_StairsForcedDirValid = false;
    }

    if (!(specialMask & SPECIAL_BLOCK_INTERACT))
        ResetInteractionFacingLock();

    /* During object/item interaction: restore the facing captured on the
       first blocked frame and strip gameplay input down to confirm/cancel. */
    if (specialMask & SPECIAL_BLOCK_INTERACT) {
        ApplyInteractionFacingLock();
        ApplyInteractionInputFilter();
        ResetAnalogState();
        return;
    }

    /* During stair animation: suppress analog and lock facing to nearest axis */
    if (specialMask & SPECIAL_BLOCK_STAIRS) {
        ApplyStairsAlignment();
        ApplyAnalogDirectionKey(0);
        ResetAnalogState();
        return;
    }
    ResetStairsAlignment();


    if (aiming) return;

    /* When not in a normal movement state (R0=8 during interactions, R0=2 during damage, etc.),
       actively clear direction keys. Without this, keys written by the mod in the previous
       frame persist in G_KEY, and the game uses them to rotate the character even though
       movement is blocked ??? causing unwanted rotation during item pickups, door animations, etc. */
    if (!normalMove) {
        ApplyAnalogDirectionKey(0);
        ResetAnalogState();
        return;
    }

    if (!haveStick) return;

    s16 currentCam = GetCameraAngleY();
    s16 currentCut = (s16)*G_CUT_NO;

    /* Camera transition buffer: on camera cut, keep old angle while stick
       is held to prevent the character from flipping direction */
    if (currentCut != g_LastCutNo) {
        if (!g_StickWasActive) g_ActiveCamAngle = currentCam;
        g_LastCutNo = currentCut;
    }
    else if (!g_StickWasActive) {
        g_ActiveCamAngle = currentCam;
    }

    /* Stick released: adopt new camera angle */
    if (mag < 0.01f) {
        if (g_StickWasActive) {
            g_ActiveCamAngle = currentCam;
            g_StickWasActive = false;
        }
        g_LastAnalogDir = 0;
        return;
    }

    /* Only buffer camera angle for forward/back (Y-dominant) movement.
       Sideways (X-dominant) always uses current camera angle. */
    float absX = stickX < 0 ? -stickX : stickX;
    float absY = stickY < 0 ? -stickY : stickY;
    if (absY >= absX) {
        g_StickWasActive = true;
    }
    else {
        g_ActiveCamAngle = currentCam;
        g_StickWasActive = false;
    }

    /* Convert stick direction to BAM, add camera facing */
    float rad = atan2f(stickX, stickY);
    s16 stickBAM = (s16)(rad / PI_F * (BAM_MAX / 2));
    u16 targetDir = (u16)((stickBAM + g_ActiveCamAngle) & (BAM_MAX - 1));

    /* If target is >90° behind current facing, walk backward instead of forward.
       This prevents the movement state machine from stalling on 180° turns. */
    u16 currentDir = ((u16)*PL_CDIR_Y) & (BAM_MAX - 1);
    s16 turnDelta = WrapBamDelta((s32)targetDir - (s32)currentDir);
    u16 moveKey = (turnDelta > BAM_MAX / 4 || turnDelta < -BAM_MAX / 4)
        ? KEY_BACKWARD : KEY_FORWARD;

    /* Write player facing (both live entity and save-area copy) */
    *PL_CDIR_Y = (s16)targetDir;
    *G_PL_CDIR_Y = (s16)targetDir;

    /* Apply movement key or clear if below walk threshold */
    ApplyAnalogDirectionKey(mag >= WALK_THRESHOLD ? moveKey : 0);
}

/* ================================================================
   HOOK INFRASTRUCTURE
   x86 detour hooking: copy function prologue to a trampoline,
   overwrite original with JMP to our code. Trampoline executes
   the saved prologue then JMPs back to the rest of the original.
   ================================================================ */

   /* Write a 5-byte relative JMP instruction */
static void WriteJmp(u8* from, void* to) {
    from[0] = 0xE9;
    *(s32*)(from + 1) = (s32)((u8*)to - (from + 5));
}

/* Hook callback: call original pad function, then apply analog override */
static void __cdecl HookedFn() {
    if (g_OriginalFn) g_OriginalFn();

    __try {
        DoAnalog3D();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

/* ---- x86 instruction length scanner ----
   Determines how many bytes of prologue we need to copy to make room
   for our 5-byte JMP without splitting any instruction. */
static int DeterminePatchSize(u8* target) {
    /* Push imm32 sequence: 0x68 xx xx xx xx (5 bytes each) */
    int i = 0;
    while (target[i] == 0x68 && i < 20) i += 5;
    if (i > 0) return i;

    /* Standard MSVC prologue: push ebp / mov ebp,esp [/ sub esp,N] */
    if (target[0] == 0x55 && target[1] == 0x8B && target[2] == 0xEC)
        return (target[3] == 0x83) ? 6 : 5;

    /* push ecx / mov eax,[addr] */
    if (target[0] == 0x51 && target[1] == 0xA1) return 6;

    /* Generic instruction-by-instruction scan */
    int total = 0;
    while (total < 5 && total < 20) {
        u8 op = target[total];
        if (op >= 0x50 && op <= 0x5F) { total += 1; continue; }  /* push/pop reg */
        if (op == 0x90) { total += 1; continue; }  /* nop */
        if (op == 0xA1 || op == 0xA3) { total += 5; continue; }  /* mov eax,[addr] */
        if (op == 0x83) { total += 3; continue; }  /* arith r/m, imm8 */
        if (op == 0x81) { total += 6; continue; }  /* arith r/m, imm32 */
        if (op >= 0xB8 && op <= 0xBF) { total += 5; continue; }  /* mov reg, imm32 */
        if (op == 0xE8 || op == 0xE9) { total += 5; continue; }  /* call/jmp rel32 */

        /* 0x66 prefix (16-bit operand) */
        if (op == 0x66) {
            u8 op2 = target[total + 1];
            if (op2 == 0xA1 || op2 == 0xA3) { total += 6; continue; }
            if (op2 >= 0xB8 && op2 <= 0xBF) { total += 4; continue; }
            if (op2 == 0x89 || op2 == 0x8B || op2 == 0xC7) {
                u8 modrm = target[total + 2];
                int mod = (modrm >> 6) & 3, rm = modrm & 7;
                int len = 3;
                if (mod == 0 && rm == 5) len += 4;
                else { if (rm == 4) len++; if (mod == 1) len++; else if (mod == 2) len += 4; }
                if (op2 == 0xC7) len += 2;  /* immediate word */
                total += len; continue;
            }
        }

        /* ModR/M instructions: mov, xor, test */
        if (op == 0x8B || op == 0x89 || op == 0x33 || op == 0x31 || op == 0x85) {
            u8 modrm = target[total + 1];
            int mod = (modrm >> 6) & 3, rm = modrm & 7;
            int len = 2;
            if (mod == 0 && rm == 5) len += 4;
            else if (rm == 4) len++;
            if (mod == 1) len++; else if (mod == 2) len += 4;
            total += len; continue;
        }

        /* 2-byte instructions with immediate byte */
        if (op == 0x24 || op == 0x0C || op == 0x34 || op == 0xA8) { total += 2; continue; }
        break;
    }
    return (total >= 5) ? total : 0;
}

/* Install a standard function-start detour hook */
static bool InstallHookAt(u8* target, const char* /*name*/) {
    DWORD oldProtect;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;


    /* Already a JMP (chained hook from REBirth): just redirect it */
    if (target[0] == 0xE9) {
        s32 rel = *(s32*)(target + 1);
        g_OriginalFn = (GameFn)(target + 5 + rel);
        WriteJmp(target, (void*)HookedFn);
        VirtualProtect(target, 16, oldProtect, &oldProtect);
        g_HookedAddr = target;
        return true;
    }

    /* Determine how many prologue bytes to relocate */
    int prologueSize = DeterminePatchSize(target);
    if (prologueSize == 0) {
        VirtualProtect(target, 16, oldProtect, &oldProtect);
        return false;
    }

    /* Allocate trampoline: copied prologue + JMP back to original */
    g_Trampoline = (u8*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!g_Trampoline) {
        VirtualProtect(target, 16, oldProtect, &oldProtect);
        return false;
    }

    memcpy(g_Trampoline, target, prologueSize);

    /* Fix relative CALL/JMP offsets in the relocated prologue */
    for (int i = 0; i < prologueSize; ) {
        u8 op = g_Trampoline[i];
        if (op == 0xE8 || op == 0xE9) {
            s32 oldRel = *(s32*)(g_Trampoline + i + 1);
            u8* absAddr = target + i + 5 + oldRel;
            *(s32*)(g_Trampoline + i + 1) = (s32)(absAddr - (g_Trampoline + i + 5));
            i += 5;
        }
        else { i++; }
    }

    WriteJmp(g_Trampoline + prologueSize, target + prologueSize);
    g_OriginalFn = (GameFn)g_Trampoline;

    /* Overwrite target: JMP to our hook, NOP remaining bytes */
    WriteJmp(target, (void*)HookedFn);
    for (int i = 5; i < prologueSize; i++) target[i] = 0x90;

    VirtualProtect(target, 16, oldProtect, &oldProtect);
    g_HookedAddr = target;
    return true;
}

/* Install a mid-function hook at a G.Key write site.
   Stub layout: [original bytes] ??? PUSHFD/PUSHAD ??? CALL DoAnalog3D ??? POPAD/POPFD ??? JMP back */
static bool InstallMidHookAt(u8* target, const char* /*name*/) {
    DWORD oldProtect;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;


    if (target[0] == 0xE9) {
        VirtualProtect(target, 16, oldProtect, &oldProtect);
        return false;
    }

    int patchSize = DeterminePatchSize(target);
    if (patchSize == 0) {
        VirtualProtect(target, 16, oldProtect, &oldProtect);
        return false;
    }

    g_MidHookStub = (u8*)VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!g_MidHookStub) {
        VirtualProtect(target, 16, oldProtect, &oldProtect);
        return false;
    }

    /* Build stub: original bytes ??? save regs ??? call DoAnalog3D ??? restore ??? JMP back */
    u8* p = g_MidHookStub;
    memcpy(p, target, patchSize); p += patchSize;
    *p++ = 0x9C;   /* pushfd */
    *p++ = 0x60;   /* pushad */
    *p++ = 0xE8;   /* call DoAnalog3D */
    *(s32*)p = (s32)((u8*)DoAnalog3D - (p + 4)); p += 4;
    *p++ = 0x61;   /* popad */
    *p++ = 0x9D;   /* popfd */
    WriteJmp(p, target + patchSize);

    /* Patch original code with JMP to stub */
    WriteJmp(target, g_MidHookStub);
    for (int n = 5; n < patchSize; n++) target[n] = 0x90;

    VirtualProtect(target, 16, oldProtect, &oldProtect);
    g_HookedAddr = target;
    return true;
}

/* ================================================================
   RUNTIME PAD FUNCTION SCANNER
   Scans the game's .text section for instructions that WRITE to
   G.Key (0xC38710) or G.Key_trg (0xC38712). Scores each write
   site and picks the best candidate for hooking.

   Scoring heuristic:
     300 = register write to Key_trg at function tail (pop+ret follows)
     150 = register write to Key_trg (mid-hook candidate)
     120 = register write to Key (mid-hook candidate)
      30 = immediate write to Key (non-zero value)
      10 = immediate write to Key (zero/clear)
       5 = immediate write to Key_trg
   ================================================================ */

   /* Walk backwards from a code address to find the containing function's start.
      MSVC pads between functions with 0xCC (int3) bytes. */
static u8* FindFuncStart(u8* codeAddr, u8* sectionStart) {
    for (u8* p = codeAddr - 1; p > sectionStart && p > codeAddr - 4096; p--) {
        if (*p == 0xCC && p[1] != 0xCC) return p + 1;
        if (*p == 0xC3 && p > sectionStart) {
            u8* next = p + 1;
            while (*next == 0xCC && next < codeAddr) next++;
            if (next < codeAddr && next <= p + 16) return next;
        }
    }
    return NULL;
}

static u8* ScanForPadFunction() {
    HMODULE hExe = GetModuleHandle(NULL);
    u8* base = (u8*)hExe;

    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { return NULL; }
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) { return NULL; }

    /* Find the first executable section (.text) */
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    u8* textStart = NULL;
    u32 textSize = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            textStart = base + sec[i].VirtualAddress;
            textSize = sec[i].Misc.VirtualSize;
            break;
        }
    }
    if (!textStart || textSize < 16) { return NULL; }

    /* Build little-endian byte patterns for G.Key and G.Key_trg addresses */
    u8 keyLE[4], trgLE[4];
    for (int i = 0; i < 4; i++) {
        keyLE[i] = (u8)(G_KEY_ADDR >> (i * 8));
        trgLE[i] = (u8)(G_KEYTRG_ADDR >> (i * 8));
    }

    u8* bestCandidate = NULL;
    int bestScore = -1;
    g_ScannedPadHookKind = HOOK_KIND_NONE;

    /* Scan all bytes for references to G.Key or G.Key_trg */
    for (u32 i = 3; i < textSize - 12; i++) {
        u8* pos = textStart + i;
        bool isKey = (memcmp(pos, keyLE, 4) == 0);
        bool isTrg = (memcmp(pos, trgLE, 4) == 0);
        if (!isKey && !isTrg) continue;

        /* Classify instruction: look at bytes before the address to determine
           if this is a WRITE instruction (mov [addr], reg/imm) */
        u8* instrStart = NULL;
        bool isRegWrite = false, isImmWrite = false;

        /* 66 A3 [addr] = mov word [addr], ax */
        if (pos >= textStart + 2 && pos[-2] == 0x66 && pos[-1] == 0xA3) {
            isRegWrite = true; instrStart = pos - 2;
        }
        /* 66 89 xx [addr] = mov word [addr], reg (ModR/M with disp32) */
        else if (pos >= textStart + 3 && pos[-3] == 0x66 && pos[-2] == 0x89 && (pos[-1] & 0xC7) == 0x05) {
            isRegWrite = true; instrStart = pos - 3;
        }
        /* 66 C7 05 [addr] imm16 = mov word [addr], immediate */
        else if (pos >= textStart + 3 && pos[-3] == 0x66 && pos[-2] == 0xC7 && pos[-1] == 0x05) {
            isImmWrite = true; instrStart = pos - 3;
        }
        /* A3 [addr] = mov [addr], eax (32-bit) */
        else if (pos >= textStart + 1 && pos[-1] == 0xA3) {
            isRegWrite = true; instrStart = pos - 1;
        }
        /* 89 xx [addr] = mov [addr], reg (ModR/M) */
        else if (pos >= textStart + 2 && pos[-2] == 0x89 && (pos[-1] & 0xC7) == 0x05) {
            isRegWrite = true; instrStart = pos - 2;
        }

        if (!instrStart) continue;  /* Not a write, skip */

        /* Score this write site */
        int score = 0;
        u8* candidate = NULL;
        HookKind kind = HOOK_KIND_NONE;

        if (isRegWrite && isTrg) {
            /* Register write to Key_trg: strong candidate for mid-hook */
            score = 150; candidate = instrStart; kind = HOOK_KIND_WRITE_SITE;
            /* Bonus if this is at the tail of the function (pop+ret follows) */
            bool pop1 = instrStart[7] >= 0x58 && instrStart[7] <= 0x5F;
            bool ret1 = instrStart[8] == 0xC3 || instrStart[8] == 0xC2;
            bool pop2 = instrStart[8] >= 0x58 && instrStart[8] <= 0x5F;
            bool ret2 = instrStart[9] == 0xC3 || instrStart[9] == 0xC2;
            if ((pop1 && ret1) || (pop1 && pop2 && ret2)) score = 300;
        }
        else if (isRegWrite && isKey) {
            score = 120; candidate = instrStart; kind = HOOK_KIND_WRITE_SITE;
        }
        else if (isImmWrite && isKey) {
            u16 imm = *(u16*)(instrStart + 7);
            score = (imm == 0 || imm == 0xFFFF || imm == 0x00FF) ? 10 : 30;
            candidate = FindFuncStart(instrStart, textStart); kind = HOOK_KIND_FUNCTION_START;
        }
        else if (isImmWrite && isTrg) {
            score = 5;
            candidate = FindFuncStart(instrStart, textStart); kind = HOOK_KIND_FUNCTION_START;
        }

        if (candidate && score > bestScore) {
            bestCandidate = candidate;
            g_ScannedPadHookKind = kind;
            bestScore = score;
        }
    }

    return bestCandidate;
}

/* ================================================================
   FALLBACK: POLLING THREAD
   If hooking fails, poll XInput every 4ms in a background thread.
   Less accurate than hooking (not synced to game frame) but works.
   ================================================================ */

static DWORD WINAPI AnalogWorkerThread(LPVOID) {
    while (WaitForSingleObject(g_StopEvent, POLL_SLEEP_MS) == WAIT_TIMEOUT) {
        __try { DoAnalog3D(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return 0;
}

static bool StartWorker() {
    if (g_WorkerThread) return true;
    g_StopEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!g_StopEvent) return false;
    DWORD tid;
    g_WorkerThread = CreateThread(NULL, 0, AnalogWorkerThread, NULL, 0, &tid);
    if (!g_WorkerThread) { CloseHandle(g_StopEvent); g_StopEvent = NULL; return false; }
    return true;
}

static void StopWorker() {
    if (g_StopEvent) SetEvent(g_StopEvent);
    if (g_WorkerThread) {
        WaitForSingleObject(g_WorkerThread, 3000);
        CloseHandle(g_WorkerThread); g_WorkerThread = NULL;
    }
    if (g_StopEvent) { CloseHandle(g_StopEvent); g_StopEvent = NULL; }
}

/* ================================================================
   DLL ENTRY POINT & MOD-SDK EXPORTS
   ================================================================ */

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LogInit();
        InitXInput();
    }
    else if (reason == DLL_PROCESS_DETACH) {
        LogClose();
        StopWorker();
        if (g_Trampoline) { VirtualFree(g_Trampoline, 0, MEM_RELEASE);  g_Trampoline = NULL; }
        if (g_MidHookStub) { VirtualFree(g_MidHookStub, 0, MEM_RELEASE); g_MidHookStub = NULL; }
    }
    return TRUE;
}

extern "C" {
    __declspec(dllexport) void Modsdk_init() {}

    __declspec(dllexport) void Modsdk_post_init() {

        /* Strategy 1: scan for pad function and hook it */
        u8* padFunc = ScanForPadFunction();
        if (padFunc) {
            bool ok = (g_ScannedPadHookKind == HOOK_KIND_WRITE_SITE)
                ? InstallMidHookAt(padFunc, "PadWriteSite(scanned)")
                : InstallHookAt(padFunc, "PadFunction(scanned)");
            if (ok) {
                g_UsingHook = true;
                return;
            }
        }
        else {
        }

        /* Strategy 2: polling thread fallback */
        if (StartWorker()) {
            g_UsingHook = false;
        }
        else {
        }
    }

    __declspec(dllexport) void Modsdk_close() { StopWorker(); }
    __declspec(dllexport) void Modsdk_load(unsigned char*, size_t, size_t) {}
    __declspec(dllexport) void Modsdk_save(unsigned char*& dst, size_t& size) { dst = NULL; size = 0; }
}