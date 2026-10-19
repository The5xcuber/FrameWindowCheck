#include "State.hpp"

namespace fw {

State& State::get() {
    static State s;
    return s;
}

void State::resetAll(bool detachNodes) {
    mode = Mode::Idle;
    frame = 0;
    injecting = false;
    cpFrames.clear();
    rec.clear();
    macro = Macro{};
    results = Results{};
    sched = Schedule{};
    windowAt.clear();
    if (overlay) {
        if (detachNodes) overlay->removeFromParent();
        overlay = nullptr;
    }
}

} // namespace fw
