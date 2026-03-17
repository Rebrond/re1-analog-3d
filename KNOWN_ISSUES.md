# Known Issues

## Active

### Fountain courtyard scripted stair animation (freeze/phase-through)
**Reported by:** External tester
**Location:** Large fountain in the courtyard above the laboratory
**Steps to reproduce:** Trigger the stair descent animation at the fountain
**What happens:** Jill phases through the fountain and freezes
**Root cause:** Unknown — needs a debug log from this scene. The scripted animation likely uses movement sequences outside the detected range (51–54) and the camera fade may not trigger G_LETTERBOX, so neither the stair block nor CutsceneGuard fires. The mod writes analog direction keys during the scripted walk, conflicting with the game's scripted movement.
**Workaround:** Save at the typewriter near the snakes in the tunnel. Disable the mod, go down the stairs, reach the typewriter inside the lab, re-enable the mod.
**Fix requires:** Debug log captured while triggering the fountain animation to identify STATUS, R0, R1, moveCnt values.

---

## Resolved

See [CHANGELOG.md](CHANGELOG.md) for full history of fixed bugs.
