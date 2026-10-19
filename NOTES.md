# Frame Window Counter (v0.1.0)

Geode mod for GD 2.2081 / Geode v5.x. Classic mode, player 1 only, vanilla 240 TPS.

## Build
1. Install the Geode CLI and set `GEODE_SDK`.
2. `geode build` (or the usual CMake flow). C++23 is required by Geode v5.
3. Pin the GDR dependency in CMakeLists.txt to a commit hash instead of the branch name.

## How it works
- Record: practice mode, saves `<levelID>.gdr2` (GDR2) in the mod save dir under `macros/`.
  Respawns from practice checkpoints truncate the recorded inputs back to that checkpoint's frame.
- Analyze, phase 1 (capture): replays the untouched macro and takes a checkpoint object N+1 frames
  before each click (moved earlier if the button is held there).
- Analyze, phase 2 (test): for each click, restore its checkpoint, shift press and release by the same
  offset (hold time unchanged), run until the survive rule ends, check for death.
  Order is 0, +1..+N, then -1..-N. By default each side stops at its first failure (the window is the
  contiguous run around 0, so the number is identical). "Full scan" tests everything.
- Window = 1 + (contiguous passes on the + side) + (contiguous passes on the - side), max 2N+1.
- Replay: plays the macro, shows the leaderboard, adds 1 to a row when the replay reaches each click.
  A window larger than the last row (N) is counted in the last row.
- Results: `<levelID>.windows.json` (always) plus CSV or TXT if enabled. Replay reads the JSON.

## Decisions I made that you did not specify
- Windows bigger than N saturate into the last overlay row.
- A click whose unshifted re-simulation fails gets window 0 and is counted in row "1".
- The Analyze HUD pause: pressing pause during analysis cancels it.
- TPS check reads the step delta given to processCommands. A setting can skip it.

## NOT compiled or tested
I could not build this against the Geode SDK here. These are the spots most likely to need edits:
1. `GJBaseGameLayer::processCommands(float)` being called once per physics step, and `handleButton`
   queueing into that same step. If it is per frame instead, frame numbers and the TPS check break.
2. `m_gameState.m_timeWarp`, `PlayLayer::createCheckpoint`, `loadFromCheckpoint`, `resetLevelFromStart`,
   `togglePracticeMode`, `pauseGame(bool)`, `destroyPlayer`, `levelComplete`, `onQuit`: names and signatures.
3. GDR2 API: `exportData()` / `importData()` result type (I used `isErr()` and `unwrap()`), and the
   `gdr::Input<>(frame, button, player2, down)` constructor. Edit only Storage.cpp.
4. Geode v5 `Popup`: `Popup::init(w, h)`, `setTitle`, `onClose`, `m_mainLayer`, `openSettingsPopup`.
5. Node ID `left-button-menu` on PauseLayer, and sprite frames `diffIcon_10_btn_001.png` (extreme demon,
   falls back to 06), `edit_leftBtn_001.png`, `edit_rightBtn_001.png`, `GJ_optionsBtn02_001.png`.
6. Checkpoint restore fidelity: GD checkpoints are not perfect (some triggers, rotation, RNG). Windows
   from restored states can differ from a true from-start replay. Spot check a few clicks by hand.

## Known limitations
- Audio is not muted during analysis (jump or orb sounds may play). Not implemented.
- Ship and other modes are as accurate as the game physics restore, no special handling.
- Dual mode: player 2 is not tracked, so windows in dual sections only reflect player 1.
- Mods that change input timing (Click Between Frames, other bots) will invalidate results. Disable them.
