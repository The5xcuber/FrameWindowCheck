#pragma once
#include "Core.hpp"

namespace fw {

// pause may be null. When given, it is resumed so the user lands in the level.
void startRecord(PlayLayer* pl, PauseLayer* pause);
void stopRecord(PlayLayer* pl);
void startAnalyze(PlayLayer* pl, PauseLayer* pause);
void startReplay(PlayLayer* pl, PauseLayer* pause);

// Hook entry points
void feed(GJBaseGameLayer* gl);       // called at the start of every physics step
void onReset(PlayLayer* pl);          // after PlayLayer::resetLevel
void onLevelComplete(PlayLayer* pl);  // before PlayLayer::levelComplete

} // namespace fw
