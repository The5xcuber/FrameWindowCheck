// Frame Window Counter, original implementation (no third party code).
//
// Flow:
//   Record  : play a normal run from 0%. Every button event is stored with its physics tick.
//   Analyze : the recording is replayed many times, each time with ONE event shifted by
//             -N..+N ticks. The window of a click is the number of contiguous shifts
//             (including 0) that still pass. Works for every gamemode because the game
//             itself simulates the result. P1, P2, jump, left and right are all events.
//   Replay  : plays the recording back with the counter rows and a number at each click.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace fw {

    struct Click {
        int id = 0;        // index inside the recorded macro
        int tick = 0;      // physics tick, 240 per second
        int button = 1;    // 1 jump, 2 left, 3 right (platformer)
        bool down = true;
        bool p1 = true;
    };

    enum class Mode { Idle, Recording, Replaying, Scanning };

    inline Mode mode = Mode::Idle;
    inline std::vector<Click> macro;
    inline std::vector<int> widths;      // in ticks, -1 means not scanned
    inline std::vector<char> capped;     // 1 if the scan hit its range limit (show a plus)
    inline size_t replayIdx = 0;
    inline bool injecting = false;       // true only while WE call handleButton
    inline bool inStep = false;          // true while the scan steps one tick
    inline int rowCounts[16] = {};

    // If replayed clicks land one tick early or late compared to your recording,
    // change this to -1 or 1.
    constexpr int kTickOffset = 0;
    constexpr int kProgressTag = 0xF1AB;
    inline constexpr char const* kCounterID = "the5xcuber.framewindow/counter";

    template <class T>
    T opt(char const* key) { return Mod::get()->getSettingValue<T>(key); }

    // ---------------------------------------------------------------- helpers

    inline int shown(int ticks) {
        int step = std::max(1, (int)std::lround(240.0 / (double)opt<int64_t>("count-fps")));
        return std::max(1, (ticks + step - 1) / step);
    }

    inline ccColor3B rowColor(int i) {
        static ccColor3B const c[] = {
            {255, 76, 84}, {255, 150, 60}, {255, 225, 70}, {141, 247, 145}, {90, 220, 255},
            {200, 160, 255}, {255, 255, 255}, {255, 255, 255}, {255, 255, 255}, {255, 255, 255}
        };
        return c[std::min(i, 9)];
    }

    inline std::filesystem::path macroPath() {
        return Mod::get()->getSaveDir() / "last-macro.txt";
    }

    inline void save() {
        std::ofstream f(macroPath());
        if (!f) return;
        for (size_t i = 0; i < macro.size(); i++) {
            auto& c = macro[i];
            int w = i < widths.size() ? widths[i] : -1;
            int cp = i < capped.size() ? capped[i] : 0;
            f << c.tick << ' ' << c.button << ' ' << (int)c.down << ' ' << (int)c.p1 << ' ' << w << ' ' << cp << '\n';
        }
    }

    inline void load() {
        macro.clear(); widths.clear(); capped.clear();
        std::ifstream f(macroPath());
        if (!f) return;
        int t, b, d, p, w, cp;
        while (f >> t >> b >> d >> p >> w >> cp) {
            Click c;
            c.id = (int)macro.size();
            c.tick = t; c.button = b; c.down = d != 0; c.p1 = p != 0;
            macro.push_back(c);
            widths.push_back(w);
            capped.push_back((char)cp);
        }
    }

    inline void clearAll() {
        macro.clear(); widths.clear(); capped.clear();
        std::error_code ec;
        std::filesystem::remove(macroPath(), ec);
    }

    // ---------------------------------------------------------------- counter UI

    inline void refreshCounter(PlayLayer* pl) {
        auto node = pl->getChildByID(kCounterID);
        if (!node) return;
        int n = (int)opt<int64_t>("counter-max");
        for (int i = 0; i < n; i++) {
            if (auto l = typeinfo_cast<CCLabelBMFont*>(node->getChildByTag(100 + i))) {
                l->setString(fmt::format("{}{}f: {}", i + 1, i + 1 == n ? "+" : "", rowCounts[i]).c_str());
            }
        }
    }

    inline void resetCounter(PlayLayer* pl) {
        std::fill(std::begin(rowCounts), std::end(rowCounts), 0);
        if (auto old = pl->getChildByID(kCounterID)) old->removeFromParent();
        if (!opt<bool>("show-counter")) return;
        auto node = CCNode::create();
        node->setID(kCounterID);
        auto win = CCDirector::get()->getWinSize();
        int n = (int)opt<int64_t>("counter-max");
        for (int i = 0; i < n; i++) {
            auto l = CCLabelBMFont::create("", "bigFont.fnt");
            l->setAnchorPoint({0.f, 1.f});
            l->setScale(0.45f);
            l->setPosition({10.f, win.height - 10.f - i * 20.f});
            l->setColor(rowColor(i));
            l->setTag(100 + i);
            node->addChild(l);
        }
        pl->addChild(node, 1000);
        refreshCounter(pl);
    }

    inline void onReplayClick(PlayLayer* pl, Click const& c, size_t idx) {
        if (idx >= widths.size() || widths[idx] < 0) return;
        int n = (int)opt<int64_t>("counter-max");
        int f = shown(widths[idx]);
        bool plus = idx < capped.size() && capped[idx];
        int bucket = std::min(f, n) - 1;
        rowCounts[bucket]++;
        refreshCounter(pl);

        if (!opt<bool>("show-circles")) return;
        auto plr = c.p1 ? pl->m_player1 : pl->m_player2;
        if (!plr) return;
        auto l = CCLabelBMFont::create(fmt::format("{}{}", f, plus ? "+" : "").c_str(), "goldFont.fnt");
        l->setScale(0.6f);
        l->setColor(rowColor(bucket));
        l->setPosition(plr->getPosition() + CCPoint{c.down ? -18.f : 18.f, 22.f});
        pl->m_objectLayer->addChild(l, 1000);
        l->runAction(CCSequence::create(
            CCDelayTime::create(0.35f), CCFadeOut::create(0.4f), CCRemoveSelf::create(), nullptr
        ));
    }

    // ---------------------------------------------------------------- scan state machine

    struct Scan {
        int ci = -1;              // -1 = baseline run, then 0..N-1
        int phase = 1;            // 1 = shifting earlier, 2 = shifting later
        int shift = 0;
        int lo = 0, hi = 0;
        bool capLo = false, capHi = false;

        bool trialActive = false;
        bool died = false, finished = false;
        std::vector<Click> ev;
        size_t idx = 0;
        int passTick = INT_MAX;
        int hardStop = 0;
        int noProgress = 0;
        int trials = 0;

        bool localCheck = true;
        int horizon = 90;
        int range = 12;
        int savedAttempts = 0;
        bool aborted = false;
        bool lateInject = false;   // a click was injected after its tick had passed
        std::string error;
    };
    inline Scan S;

    // Builds the event list for one trial. Returns false if the shift is impossible
    // (negative tick, or it would jump over the same button's neighbouring event).
    inline bool buildTrial(int ci, int shift) {
        S.ev = macro;
        S.passTick = INT_MAX;
        S.hardStop = macro.empty() ? 0 : macro.back().tick + 2400;
        if (ci < 0) return true;

        auto& c = S.ev[ci];
        int nt = c.tick + shift;
        if (nt < 0) return false;

        bool p1 = c.p1;
        int prev = -1, next = INT_MAX;
        for (auto& o : S.ev) {
            if (o.id == c.id || o.button != c.button || o.p1 != c.p1) continue;
            if (o.id < c.id) prev = std::max(prev, o.tick);
            else next = std::min(next, o.tick);
        }
        if (nt <= prev || nt >= next) return false;

        c.tick = nt;
        std::stable_sort(S.ev.begin(), S.ev.end(), [](Click const& a, Click const& b) {
            return a.tick < b.tick;
        });

        if (S.localCheck) {
            int nextTick = nt;
            bool seen = false;
            for (auto& o : S.ev) {
                if (o.id == ci) { seen = true; continue; }
                if (seen && o.p1 == p1) { nextTick = o.tick; break; }
            }
            S.passTick = nextTick + S.horizon;
        }
        return true;
    }

    inline void startClick() {
        S.phase = 1; S.shift = -1; S.lo = S.hi = 0; S.capLo = S.capHi = false;
    }

    inline void finishClick() {
        widths[S.ci] = S.hi - S.lo + 1;
        capped[S.ci] = (S.capLo || S.capHi) ? 1 : 0;
        S.ci++;
        if (S.ci < (int)macro.size()) startClick();
    }

    inline void advance(bool pass) {
        if (S.ci < 0) {
            if (!pass) {
                S.aborted = true;
                S.error = "The unmodified recording does not finish the level. Record a completed run first.";
                return;
            }
            S.ci = 0;
            if (!macro.empty()) startClick();
            return;
        }
        if (S.phase == 1) {
            if (pass) {
                S.lo = S.shift;
                if (-S.shift >= S.range) { S.capLo = true; S.phase = 2; S.shift = 1; }
                else S.shift--;
            } else {
                S.phase = 2; S.shift = 1;
            }
            return;
        }
        if (pass) {
            S.hi = S.shift;
            if (S.shift >= S.range) { S.capHi = true; finishClick(); }
            else S.shift++;
        } else {
            finishClick();
        }
    }

    inline std::string summary() {
        int n = (int)opt<int64_t>("counter-max");
        std::vector<int> h(n, 0);
        int unknown = 0;
        for (size_t i = 0; i < widths.size(); i++) {
            if (widths[i] < 0) { unknown++; continue; }
            h[std::min(shown(widths[i]), n) - 1]++;
        }
        std::string out;
        for (int i = 0; i < n; i++) {
            out += fmt::format("{}{}f: {}\n", i + 1, i + 1 == n ? "+" : "", h[i]);
        }
        if (unknown) out += fmt::format("not scanned: {}\n", unknown);
        out += fmt::format("trials run: {}", S.trials);
        return out;
    }

    inline void removeProgress() {
        if (auto scene = CCDirector::get()->getRunningScene()) {
            if (auto l = scene->getChildByTag(kProgressTag)) l->removeFromParent();
        }
    }

    inline void updateProgress() {
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        auto l = typeinfo_cast<CCLabelBMFont*>(scene->getChildByTag(kProgressTag));
        if (!l) return;
        l->setString(fmt::format("Scanning click {}/{}  (trials: {})",
            std::max(S.ci, 0) + 1, macro.size(), S.trials).c_str());
    }

    inline void beginScan(PlayLayer* pl) {
        S = Scan{};
        S.localCheck = opt<bool>("local-check");
        S.horizon = (int)opt<int64_t>("check-horizon");
        S.range = (int)opt<int64_t>("scan-range");
        S.savedAttempts = pl->m_attempts;
        widths.assign(macro.size(), -1);
        capped.assign(macro.size(), 0);

        mode = Mode::Scanning;
        pl->setVisible(false);

        auto scene = CCDirector::get()->getRunningScene();
        auto win = CCDirector::get()->getWinSize();
        auto l = CCLabelBMFont::create("Scanning...", "bigFont.fnt");
        l->setScale(0.6f);
        l->setPosition({win.width / 2.f, win.height / 2.f});
        l->setTag(kProgressTag);
        scene->addChild(l, 9999);
    }

    inline void endScan(PlayLayer* pl, bool ok) {
        mode = Mode::Idle;
        inStep = false;
        removeProgress();
        pl->setVisible(true);
        pl->m_attempts = S.savedAttempts;
        pl->resetLevel();
        resetCounter(pl);
        if (ok) {
            save();
            FLAlertLayer::create("Scan finished", summary().c_str(), "OK")->show();
        } else {
            FLAlertLayer::create("Scan stopped",
                S.error.empty() ? "Unknown error." : S.error.c_str(), "OK")->show();
        }
    }

    inline void scanTick(PlayLayer* pl) {
        auto start = std::chrono::steady_clock::now();
        auto budget = std::chrono::milliseconds(opt<int64_t>("scan-budget-ms"));

        auto conclude = [&](bool pass) {
            S.trialActive = false;
            advance(pass);
        };

        while (std::chrono::steady_clock::now() - start < budget) {
            if (S.aborted) { endScan(pl, false); return; }

            if (!S.trialActive) {
                if (S.ci >= (int)macro.size()) { endScan(pl, true); return; }
                if (!buildTrial(S.ci, S.shift)) { advance(false); continue; }
                S.died = S.finished = false;
                S.idx = 0;
                S.noProgress = 0;
                S.trialActive = true;
                S.trials++;
                pl->resetLevel();
            }

            int before = pl->m_gameState.m_currentProgress;
            inStep = true;
            pl->update(1.f / 240.f);
            inStep = false;
            int tick = pl->m_gameState.m_currentProgress;

            if (tick - before > 1) {
                S.error = fmt::format("The game advanced {} ticks in one step, so click timing would be unreliable. "
                                      "The step logic needs adjusting for this game version.", tick - before);
                endScan(pl, false);
                return;
            }
            if (S.lateInject) {
                S.error = "A click was injected after its tick had already passed, so results would be unreliable. "
                          "Check kTickOffset and where processCommands is called.";
                endScan(pl, false);
                return;
            }

            if (tick == before) {
                if (++S.noProgress > 8) {
                    S.error = "The game did not advance a physics tick. This game version needs a tweak in scanTick.";
                    endScan(pl, false);
                    return;
                }
            } else {
                S.noProgress = 0;
            }

            if (S.died || (pl->m_player1 && pl->m_player1->m_isDead)) { conclude(false); continue; }
            if (S.finished) { conclude(true); continue; }
            if (S.localCheck && S.ci >= 0 && tick >= S.passTick) { conclude(true); continue; }
            if (tick > S.hardStop) { conclude(false); continue; }
        }
        updateProgress();
    }

    inline void finishRecording() {
        mode = Mode::Idle;
        widths.assign(macro.size(), -1);
        capped.assign(macro.size(), 0);
        save();
        Notification::create(
            fmt::format("Recorded {} inputs. Open pause, then Analyze.", macro.size()),
            NotificationIcon::Success
        )->show();
    }

    inline void resetState() {
        removeProgress();
        mode = Mode::Idle;
        inStep = false;
        injecting = false;
    }

} // namespace fw

$on_mod(Loaded) {
    fw::load();
}

// ------------------------------------------------------------------ input hooks

class $modify(FWBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto pl = typeinfo_cast<PlayLayer*>(this);
        if (pl) {
            // ignore real input while a macro is driving the game
            if (!fw::injecting && (fw::mode == fw::Mode::Replaying || fw::mode == fw::Mode::Scanning)) return;

            if (!fw::injecting && fw::mode == fw::Mode::Recording) {
                fw::Click c;
                c.id = (int)fw::macro.size();
                c.tick = m_gameState.m_currentProgress + fw::kTickOffset;
                c.button = button;
                c.down = down;
                c.p1 = isPlayer1;
                fw::macro.push_back(c);
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void processCommands(float dt) {
        auto pl = typeinfo_cast<PlayLayer*>(this);
        if (pl && (fw::mode == fw::Mode::Replaying || fw::mode == fw::Mode::Scanning)) {
            bool scanning = fw::mode == fw::Mode::Scanning;
            auto& ev = scanning ? fw::S.ev : fw::macro;
            size_t& idx = scanning ? fw::S.idx : fw::replayIdx;
            int tick = m_gameState.m_currentProgress;

            while (idx < ev.size() && ev[idx].tick <= tick) {
                auto c = ev[idx];
                if (scanning && c.tick < tick) fw::S.lateInject = true;
                fw::injecting = true;
                GJBaseGameLayer::handleButton(c.down, c.button, c.p1);
                fw::injecting = false;
                if (!scanning) fw::onReplayClick(pl, c, idx);
                idx++;
            }
        }
        GJBaseGameLayer::processCommands(dt);
    }
};

// ------------------------------------------------------------------ level hooks

class $modify(FWPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        if (fw::mode == fw::Mode::Recording) {
            // only the final attempt is kept
            fw::macro.clear();
            fw::widths.clear();
            fw::capped.clear();
        }
        if (fw::mode == fw::Mode::Replaying) {
            fw::replayIdx = 0;
            fw::resetCounter(this);
        }
    }

    void update(float dt) {
        if (fw::mode == fw::Mode::Scanning && !fw::inStep) {
            fw::scanTick(this);
            return;
        }
        PlayLayer::update(dt);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (fw::mode == fw::Mode::Scanning) {
            fw::S.died = true;
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        if (fw::mode == fw::Mode::Scanning) {
            fw::S.finished = true;
            return;
        }
        if (fw::mode == fw::Mode::Recording) fw::finishRecording();
        else if (fw::mode == fw::Mode::Replaying) fw::mode = fw::Mode::Idle;
        PlayLayer::levelComplete();
    }

    void startMusic() {
        if (fw::mode == fw::Mode::Scanning) return;
        PlayLayer::startMusic();
    }

    void onQuit() {
        fw::resetState();
        PlayLayer::onQuit();
    }
};

// ------------------------------------------------------------------ pause menu

class $modify(FWPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto win = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setID("the5xcuber.framewindow/menu");
        menu->setPosition({0.f, 0.f});

        auto add = [&](char const* text, SEL_MenuHandler sel, float y) {
            auto spr = ButtonSprite::create(text, "bigFont.fnt", "GJ_button_04.png", 0.7f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
            btn->setPosition({win.width - 70.f, y});
            menu->addChild(btn);
        };
        float y = win.height / 2.f + 70.f;
        add("Record",  menu_selector(FWPause::onFWRecord),  y);
        add("Replay",  menu_selector(FWPause::onFWReplay),  y - 40.f);
        add("Analyze", menu_selector(FWPause::onFWAnalyze), y - 80.f);
        add("Clear",   menu_selector(FWPause::onFWClear),   y - 120.f);
        this->addChild(menu);
    }

    bool fwAllowed(PlayLayer* pl) {
        if (!pl) return false;
        if (pl->m_isPracticeMode) {
            FLAlertLayer::create("Frame Window", "Turn practice mode off first. Runs must start at 0%.", "OK")->show();
            return false;
        }
        return true;
    }

    void onFWRecord(CCObject* sender) {
        auto pl = PlayLayer::get();
        if (!fwAllowed(pl)) return;
        fw::macro.clear(); fw::widths.clear(); fw::capped.clear();
        fw::mode = fw::Mode::Recording;
        this->onRestart(sender);
    }

    void onFWReplay(CCObject* sender) {
        auto pl = PlayLayer::get();
        if (!fwAllowed(pl)) return;
        if (fw::macro.empty()) {
            FLAlertLayer::create("Frame Window", "Nothing recorded yet.", "OK")->show();
            return;
        }
        fw::mode = fw::Mode::Replaying;
        this->onRestart(sender);
    }

    void onFWAnalyze(CCObject* sender) {
        auto pl = PlayLayer::get();
        if (!fwAllowed(pl)) return;
        if (fw::macro.empty()) {
            FLAlertLayer::create("Frame Window", "Nothing recorded yet.", "OK")->show();
            return;
        }
        fw::beginScan(pl);
        this->onResume(sender);
    }

    void onFWClear(CCObject*) {
        fw::clearAll();
        Notification::create("Recording cleared", NotificationIcon::Success)->show();
    }
};
