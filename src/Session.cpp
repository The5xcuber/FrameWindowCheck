#include "Session.hpp"

#include <cmath>

#include "Analyzer.hpp"
#include "State.hpp"
#include "Storage.hpp"

using namespace geode::prelude;

namespace fw {
namespace {

void alert(std::string const& msg) {
    FLAlertLayer::create("Frame Window Counter", msg, "OK")->show();
}

// Results are only valid at vanilla 240 TPS. The physics step delta handed to
// processCommands is 1/TPS (times time warp), so we read it from there.
bool tpsOk(std::string& msg) {
    auto const s = Settings::load();
    auto& S = State::get();
    if (s.skipTps || !S.haveStep) return true;
    bool dtOk = std::abs(S.lastStepDt * 240.f - 1.f) < 0.01f;
    bool warpOk = std::abs(S.lastTimeWarp - 1.f) < 0.001f;
    if (dtOk && warpOk) return true;
    float tps = S.lastStepDt > 0.f ? 1.f / S.lastStepDt : 0.f;
    msg = fmt::format(
        "Detected about {:.0f} TPS (time warp {:.2f}). Results are only valid at vanilla 240 TPS, so this was blocked. "
        "If the detection is wrong, enable \"Skip TPS check\" in the mod settings.",
        tps, S.lastTimeWarp);
    return false;
}

void truncateRec(uint64_t f) {
    auto& r = State::get().rec;
    r.erase(std::remove_if(r.begin(), r.end(), [f](Input const& e) { return e.frame >= f; }), r.end());
    // GD releases all buttons on respawn, so close a hold that was open at the checkpoint.
    if (heldAt(r, f)) r.push_back({f, false, 1});
}

bool saveRecording(PlayLayer* pl) {
    auto& S = State::get();
    Macro m;
    m.levelID = pl->m_level->m_levelID.value();
    m.levelName = std::string(pl->m_level->m_levelName);
    m.duration = S.frame;
    m.inputs = S.rec;
    std::stable_sort(m.inputs.begin(), m.inputs.end(), [](Input const& a, Input const& b) { return a.frame < b.frame; });
    if (m.clicks().empty()) {
        Notification::create("No clicks were recorded, nothing saved", NotificationIcon::Warning)->show();
        return false;
    }
    std::string err;
    auto key = levelKey(pl->m_level);
    if (!saveMacro(m, key, err)) {
        alert(err);
        return false;
    }
    std::error_code ec;
    std::filesystem::remove(resultsPath(key), ec); // old results belong to the old macro
    Notification::create(fmt::format("Macro saved ({} clicks). Run Analyze next.", m.clicks().size()), NotificationIcon::Success)->show();
    return true;
}

bool macroMatchesLevel(Macro const& m, GJGameLevel* lvl) {
    int id = lvl->m_levelID.value();
    if (id > 0) return m.levelID == id;
    return m.levelName == std::string(lvl->m_levelName);
}

} // namespace

void startRecord(PlayLayer* pl, PauseLayer* pause) {
    std::string msg;
    if (!tpsOk(msg)) return alert(msg);
    auto& S = State::get();
    S.resetAll(true);
    pl->togglePracticeMode(false); // clean single run from frame 0, no checkpoint respawns
    pl->resetLevelFromStart();
    S.mode = Mode::Record;
    S.frame = 0;
    if (pause) pause->onResume(nullptr);
    Notification::create("Recording. Finish the level or Stop & Save from the menu.", NotificationIcon::Info)->show();
}

void stopRecord(PlayLayer* pl) {
    auto& S = State::get();
    if (S.mode != Mode::Record) return;
    saveRecording(pl);
    S.resetAll(true);
}

void startAnalyze(PlayLayer* pl, PauseLayer* pause) {
    std::string msg;
    if (!tpsOk(msg)) return alert(msg);
    Macro m;
    auto key = levelKey(pl->m_level);
    if (!loadMacro(key, m, msg)) return alert(msg);
    if (!macroMatchesLevel(m, pl->m_level)) return alert("This macro was recorded on a different level.");

    if (pause) pause->onResume(nullptr);
    if (!Analyzer::get().begin(pl, m, key, msg)) alert(msg);
}

void startReplay(PlayLayer* pl, PauseLayer* pause) {
    std::string msg;
    auto key = levelKey(pl->m_level);
    Macro m;
    Results r;
    if (!loadMacro(key, m, msg)) return alert(msg);
    if (!macroMatchesLevel(m, pl->m_level)) return alert("This macro was recorded on a different level.");
    if (!loadResults(key, r, msg)) return alert(msg);
    if (r.clicks.size() != m.clicks().size()) return alert("The analysis does not match the macro (different click count). Run Analyze again.");

    auto& S = State::get();
    S.resetAll(true);
    S.inputOffset = static_cast<int>(Mod::get()->getSettingValue<int64_t>("input-offset"));
    S.releaseOffset = static_cast<int>(Mod::get()->getSettingValue<int64_t>("release-offset"));
    S.macro = m;
    S.results = r;
    for (auto const& c : r.clicks) S.windowAt[c.frame] = c.window;
    S.sched.ev = m.inputs;
    S.sched.idx = 0;

    pl->togglePracticeMode(true);
    pl->resetLevelFromStart();
    S.mode = Mode::Replay;
    S.frame = 0;

    S.overlay = WindowOverlay::create(r.maxFrames);
    pl->m_uiLayer->addChild(S.overlay, 900);

    if (pause) pause->onResume(nullptr);
}

void feed(GJBaseGameLayer* gl) {
    auto& S = State::get();
    if (S.mode != Mode::Replay && S.mode != Mode::Analyze) return;
    auto& sc = S.sched;
    while (sc.idx < sc.ev.size()) {
        auto const e = sc.ev[sc.idx];
        int64_t const off = e.down ? S.inputOffset : S.releaseOffset;
        int64_t const at = static_cast<int64_t>(e.frame) + off;
        if (at > static_cast<int64_t>(S.frame)) break;
        ++sc.idx;
        if (at < static_cast<int64_t>(S.frame)) continue;
        S.injecting = true;
        gl->handleButton(e.down, 1, true);
        S.injecting = false;
        if (S.mode == Mode::Replay && e.down && S.overlay) {
            auto it = S.windowAt.find(e.frame);
            if (it != S.windowAt.end()) S.overlay->add(it->second);
        }
    }
}

void onReset(PlayLayer* pl) {
    auto& S = State::get();
    if (S.mode != Mode::Record && S.mode != Mode::Replay) return;

    // After a respawn the run continues from the last checkpoint, or from frame 0 without one.
    uint64_t f = 0;
    auto arr = pl->m_checkpointArray;
    if (arr && arr->count() > 0) {
        auto it = S.cpFrames.find(arr->lastObject());
        if (it != S.cpFrames.end()) f = it->second;
    }
    S.frame = f;

    if (S.mode == Mode::Record) {
        truncateRec(f);
    } else {
        S.sched.seek(f);
        if (S.overlay) {
            S.overlay->resetCounts();
            for (auto const& c : S.results.clicks) {
                if (c.frame < f) S.overlay->add(c.window);
            }
        }
    }
}

void onLevelComplete(PlayLayer* pl) {
    auto& S = State::get();
    if (S.mode == Mode::Record) {
        saveRecording(pl);
        S.mode = Mode::Idle;
        S.rec.clear();
    }
}

} // namespace fw
