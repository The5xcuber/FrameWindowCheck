#include "Analyzer.hpp"

#include <chrono>

#include "State.hpp"
#include "Storage.hpp"

using namespace geode::prelude;

namespace fw {

Analyzer& Analyzer::get() {
    static Analyzer a;
    return a;
}

bool Analyzer::begin(PlayLayer* pl, Macro const& macro, std::string const& key, std::string& err) {
    auto const s = Settings::load();
    m_macro = macro;
    std::stable_sort(m_macro.inputs.begin(), m_macro.inputs.end(), [](Input const& a, Input const& b) { return a.frame < b.frame; });
    m_clicks = m_macro.clicks();
    if (m_clicks.empty()) {
        err = "The macro has no clicks.";
        return false;
    }
    m_key = key;
    m_orig = m_macro.inputs;
    m_N = s.maxFrames;
    m_custom = s.custom;
    m_customFrames = s.customFrames;
    m_fullScan = s.fullScan;
    m_budgetMs = s.budgetMs;

    m_offsets.clear();
    m_offsets.push_back(0);
    for (int i = 1; i <= m_N; ++i) m_offsets.push_back(i);
    for (int i = 1; i <= m_N; ++i) m_offsets.push_back(-i);

    // Checkpoint frame per click: N+1 frames before the click so even the
    // earliest shifted press happens after it, then walk back to a frame where
    // the button is not held (a restore does not reliably keep held buttons).
    m_cpFrame.clear();
    m_wanted.clear();
    for (auto const& c : m_clicks) {
        uint64_t f = c.down > static_cast<uint64_t>(m_N) + 1 ? c.down - m_N - 1 : 0;
        while (f > 0 && heldAt(m_orig, f)) --f;
        m_cpFrame.push_back(f);
        m_wanted.insert(f);
    }
    m_lastCp = *m_wanted.rbegin();

    m_results = Results{};
    m_results.maxFrames = m_N;
    m_results.custom = m_custom;
    m_results.customFrames = m_customFrames;
    m_cps.clear();
    m_ci = m_oi = 0;
    m_died = m_finished = false;
    m_hasLast = false;

    auto& S = State::get();
    S.resetAll(true);
    S.inputOffset = static_cast<int>(Mod::get()->getSettingValue<int64_t>("input-offset"));
    S.releaseOffset = static_cast<int>(Mod::get()->getSettingValue<int64_t>("release-offset"));
    pl->togglePracticeMode(true);
    pl->resetLevelFromStart();
    S.mode = Mode::Analyze;
    S.frame = 0;
    S.sched.ev = m_orig;
    S.sched.idx = 0;

    m_hud = AnalysisHUD::create([] { Analyzer::get().cancel(PlayLayer::get(), "Analysis cancelled"); });
    pl->m_uiLayer->addChild(m_hud, 1000);
    m_phase = Phase::Capture;
    return true;
}

void Analyzer::tick(PlayLayer* pl, StepFn const& step) {
    auto const t0 = std::chrono::steady_clock::now();
    while (m_phase != Phase::Idle) {
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms >= m_budgetMs) break;
        if (m_phase == Phase::Capture) stepCapture(pl, step);
        else stepTest(pl, step);
    }
    if (m_phase != Phase::Idle) updateHud();
}

// Phase 1: replay the untouched macro once and snapshot the game right before
// each click (minus the look-back window). Snapshots use the game's own
// practice-mode checkpoint objects.
void Analyzer::stepCapture(PlayLayer* pl, StepFn const& step) {
    auto& S = State::get();
    if (m_died) {
        cancel(pl, fmt::format("The original macro died at frame {} during capture. Re-record it.", S.frame));
        return;
    }
    if (S.frame > m_lastCp || m_finished) {
        if (m_cps.size() < m_wanted.size()) {
            cancel(pl, "The macro ended before all checkpoints were captured.");
            return;
        }
        beginTests(pl);
        return;
    }
    // Taken BEFORE this frame's inputs are fed, so "held" state is as computed in begin().
    if (m_wanted.count(S.frame) && !m_cps.count(S.frame)) {
        auto cp = pl->createCheckpoint();
        if (!cp) {
            cancel(pl, "Could not create a checkpoint.");
            return;
        }
        m_cps[S.frame] = cp;
    }
    if (!step()) cancel(pl, "The game did not advance a physics step.");
}

void Analyzer::beginTests(PlayLayer* pl) {
    m_phase = Phase::Test;
    m_ci = 0;
    startClick(pl);
}

void Analyzer::startClick(PlayLayer* pl) {
    auto const& c = m_clicks[m_ci];
    m_cur = ClickResult{};
    m_cur.index = m_ci;
    m_cur.frame = c.down;
    m_cur.hold = c.hold();
    m_oi = 0;
    startTest(pl);
}

// Restore the snapshot for this click and schedule the shifted input.
// The hold time is kept identical: press and release move by the same offset.
void Analyzer::startTest(PlayLayer* pl) {
    auto& S = State::get();
    auto const& c = m_clicks[m_ci];
    int const o = m_offsets[m_oi];
    uint64_t const cpF = m_cpFrame[m_ci];

    auto it = m_cps.find(cpF);
    if (it == m_cps.end()) {
        cancel(pl, "Internal error: missing checkpoint.");
        return;
    }

    int64_t const sd = static_cast<int64_t>(c.down) + o;
    int64_t const su = static_cast<int64_t>(c.up) + o;
    if (sd < static_cast<int64_t>(cpF)) { // cannot be placed before the snapshot
        finishTest(pl, false);
        return;
    }

    pl->loadFromCheckpoint(it->second);
    S.frame = cpF;
    S.sched.ev = shifted(m_ci, o);
    S.sched.seek(cpF);
    m_died = m_finished = false;

    if (m_custom) {
        m_endFrame = static_cast<uint64_t>(sd + m_customFrames);
    } else {
        // Survive until the next ORIGINAL click (or the macro end), and at least through our own release.
        int64_t next = m_ci + 1 < m_clicks.size() ? static_cast<int64_t>(m_clicks[m_ci + 1].down) : static_cast<int64_t>(m_macro.duration);
        m_endFrame = static_cast<uint64_t>(std::max<int64_t>(next, su + 1));
    }
}

void Analyzer::stepTest(PlayLayer* pl, StepFn const& step) {
    auto& S = State::get();
    if (m_died) { finishTest(pl, false); return; }
    if (m_finished || S.frame >= m_endFrame) { finishTest(pl, true); return; }
    if (S.frame > m_macro.duration + 600) { finishTest(pl, true); return; } // safety net
    if (!step()) cancel(pl, "The game did not advance a physics step.");
}

void Analyzer::finishTest(PlayLayer* pl, bool pass) {
    int const o = m_offsets[m_oi];
    (pass ? m_cur.passed : m_cur.failed).push_back(o);
    m_lastPass = pass;
    m_hasLast = true;

    size_t next = m_oi + 1;
    if (!m_fullScan && !pass) {
        // The window is the contiguous run around 0, so one failure per side is enough.
        if (o > 0) next = static_cast<size_t>(m_N) + 1; // jump to -1
        else next = m_offsets.size();                   // offset 0 or a negative offset failed
    }
    m_oi = next;
    if (m_oi >= m_offsets.size()) finishClick(pl);
    else startTest(pl);
}

void Analyzer::finishClick(PlayLayer* pl) {
    auto has = [&](int v) { return std::find(m_cur.passed.begin(), m_cur.passed.end(), v) != m_cur.passed.end(); };
    int p = 0, q = 0;
    while (has(p + 1)) ++p;
    while (has(-(q + 1))) ++q;
    m_cur.window = has(0) ? 1 + p + q : 0; // at most 2N + 1
    m_results.clicks.push_back(m_cur);

    if (++m_ci >= m_clicks.size()) finish(pl);
    else startClick(pl);
}

void Analyzer::finish(PlayLayer* pl) {
    std::string err;
    auto n = m_results.clicks.size();
    bool ok = saveResults(m_results, m_key, err);
    cleanup(pl);
    if (ok) Notification::create(fmt::format("Analysis done: {} clicks", n), NotificationIcon::Success)->show();
    else FLAlertLayer::create("Frame Window Counter", err, "OK")->show();
}

void Analyzer::cancel(PlayLayer* pl, std::string const& why) {
    if (!active()) return;
    cleanup(pl);
    Notification::create(why, NotificationIcon::Warning)->show();
}

void Analyzer::cleanup(PlayLayer* pl) {
    auto& S = State::get();
    m_phase = Phase::Idle;
    S.mode = Mode::Idle;
    S.sched = Schedule{};
    m_cps.clear();
    if (m_hud) {
        m_hud->removeFromParent();
        m_hud = nullptr;
    }
    if (pl) {
        pl->resetLevelFromStart();
        pl->pauseGame(false);
    }
}

void Analyzer::forget() {
    m_phase = Phase::Idle;
    m_cps.clear();
    m_hud = nullptr;
}

void Analyzer::updateHud() {
    if (!m_hud) return;
    auto& S = State::get();
    if (m_phase == Phase::Capture) {
        float p = m_lastCp ? 0.1f * static_cast<float>(S.frame) / static_cast<float>(m_lastCp) : 0.1f;
        m_hud->set("Capturing checkpoints...", {255, 255, 255}, p);
        return;
    }
    float p = 0.1f + 0.9f * static_cast<float>(m_ci) / static_cast<float>(m_clicks.size());
    auto text = fmt::format("Click {}/{}  offset {:+d}  last: {}", m_ci + 1, m_clicks.size(), m_offsets[m_oi],
                            !m_hasLast ? "-" : (m_lastPass ? "PASS" : "FAIL"));
    ccColor3B col = !m_hasLast ? ccColor3B{255, 255, 255} : (m_lastPass ? ccColor3B{120, 255, 120} : ccColor3B{255, 110, 110});
    m_hud->set(text, col, p);
}

std::vector<Input> Analyzer::shifted(size_t c, int o) const {
    auto ev = m_orig;
    auto const& k = m_clicks[c];
    auto move = [&](size_t i) {
        if (i == kNone) return;
        int64_t f = static_cast<int64_t>(ev[i].frame) + o;
        ev[i].frame = f < 0 ? 0 : static_cast<uint64_t>(f);
    };
    move(k.downIdx);
    move(k.upIdx);
    std::stable_sort(ev.begin(), ev.end(), [](Input const& a, Input const& b) { return a.frame < b.frame; });
    return ev;
}

} // namespace fw
