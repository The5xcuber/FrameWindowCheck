#pragma once
#include "Core.hpp"
#include "Overlay.hpp"

namespace fw {

using StepFn = std::function<bool()>; // runs one physics step, returns true if the step counter advanced

class Analyzer {
public:
    static Analyzer& get();

    bool active() const { return m_phase != Phase::Idle; }

    // Resets the level to the start in practice mode and starts the two phase analysis.
    bool begin(PlayLayer* pl, Macro const& macro, std::string const& key, std::string& err);

    // Called from the PlayLayer::update hook instead of the real update while active.
    void tick(PlayLayer* pl, StepFn const& step);

    // Called from the destroyPlayer / levelComplete hooks while active.
    void onDeath() { m_died = true; }
    void onComplete() { m_finished = true; }

    void cancel(PlayLayer* pl, std::string const& why);
    // Drop everything without touching nodes (the layer is being destroyed).
    void forget();

private:
    enum class Phase { Idle, Capture, Test };

    void stepCapture(PlayLayer* pl, StepFn const& step);
    void stepTest(PlayLayer* pl, StepFn const& step);
    void beginTests(PlayLayer* pl);
    void startClick(PlayLayer* pl);
    void startTest(PlayLayer* pl);
    void finishTest(PlayLayer* pl, bool pass);
    void finishClick(PlayLayer* pl);
    void finish(PlayLayer* pl);
    void cleanup(PlayLayer* pl);
    void updateHud();
    std::vector<Input> shifted(size_t clickIdx, int offset) const;

    Phase m_phase = Phase::Idle;

    Macro m_macro;
    std::string m_key;
    std::vector<Input> m_orig;
    std::vector<Click> m_clicks;
    std::vector<int> m_offsets; // 0, +1..+N, -1..-N
    int m_N = 12;
    bool m_custom = false;
    int m_customFrames = 12;
    bool m_fullScan = false;
    int m_budgetMs = 12;

    // Phase 1
    std::vector<uint64_t> m_cpFrame; // per click
    std::set<uint64_t> m_wanted;
    uint64_t m_lastCp = 0;
    std::map<uint64_t, geode::Ref<CheckpointObject>> m_cps;

    // Phase 2
    size_t m_ci = 0;
    size_t m_oi = 0;
    ClickResult m_cur;
    Results m_results;
    uint64_t m_endFrame = 0;
    bool m_lastPass = true;
    bool m_hasLast = false;

    // Set by hooks during a test
    bool m_died = false;
    bool m_finished = false;

    geode::Ref<AnalysisHUD> m_hud;
};

} // namespace fw
