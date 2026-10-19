#pragma once
#include "Core.hpp"
#include "Overlay.hpp"

namespace fw {

enum class Mode { Idle, Record, Replay, Analyze };

// Inputs to feed to the game, in frame order.
struct Schedule {
    std::vector<Input> ev;
    size_t idx = 0;
    void seek(uint64_t frame) {
        uint64_t from = frame > 4 ? frame - 4 : 0; // margin for the input offsets
        idx = std::lower_bound(ev.begin(), ev.end(), from, [](Input const& e, uint64_t f) { return e.frame < f; }) - ev.begin();
    }
};

struct State {
    static State& get();

    Mode mode = Mode::Idle;

    // Our own step counter: index of the physics step that is about to run.
    // Incremented once per GJBaseGameLayer::processCommands while mode != Idle.
    uint64_t frame = 0;
    bool injecting = false;
    int inputOffset = 0;   // frames added to presses when feeding a schedule
    int releaseOffset = 0; // frames added to releases when feeding a schedule

    // Last step delta seen, used for the TPS check.
    bool haveStep = false;
    float lastStepDt = kStepDt;
    float lastTimeWarp = 1.f;

    // Frame at which each checkpoint object was created (for Record truncation).
    std::unordered_map<void*, uint64_t> cpFrames;

    // Record
    std::vector<Input> rec;

    // Replay and Analyze
    Macro macro;
    Results results;
    Schedule sched;
    std::unordered_map<uint64_t, int> windowAt; // click down frame -> window
    geode::Ref<WindowOverlay> overlay;

    // detachNodes=false is for when the PlayLayer is already being destroyed.
    void resetAll(bool detachNodes);
};

} // namespace fw
