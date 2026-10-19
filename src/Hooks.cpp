#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Analyzer.hpp"
#include "Popup.hpp"
#include "Session.hpp"
#include "State.hpp"

using namespace geode::prelude;

static bool isPlay(GJBaseGameLayer* g) {
    auto pl = PlayLayer::get();
    return pl && static_cast<GJBaseGameLayer*>(pl) == g;
}

// ---------------------------------------------------------------------------
// Frame stepping, input injection, and the analysis driver
// ---------------------------------------------------------------------------
class $modify(FWBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (isPlay(this)) {
            auto& S = fw::State::get();
            // While replaying or analyzing only our injected inputs reach the game.
            if ((S.mode == fw::Mode::Replay || S.mode == fw::Mode::Analyze) && !S.injecting) return;
            if (S.mode == fw::Mode::Record && !S.injecting && button == 1 && isPlayer1) {
                S.rec.push_back({S.frame, down, 1});
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // Runs once per physics step. Half ticks are not counted (observed: frame advances by 1 per full tick).
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        bool const play = isPlay(this);
        auto& S = fw::State::get();
        bool const count = play && !isHalfTick;
        if (count) {
            S.haveStep = true;
            S.lastStepDt = dt;
            S.lastTimeWarp = m_gameState.m_timeWarp;
            fw::feed(this);
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (count && S.mode != fw::Mode::Idle) ++S.frame;
    }

    // One physics step. update(dt) may produce zero or more steps depending on the
    // game's internal accumulator, so retry until our own counter moves.
    bool fwStep() {
        auto& S = fw::State::get();
        auto const before = S.frame;
        for (int i = 0; i < 4 && S.frame == before; ++i) GJBaseGameLayer::update(fw::kStepDt);
        return S.frame != before;
    }

    void update(float dt) {
        auto& A = fw::Analyzer::get();
        if (A.active() && isPlay(this)) {
            // Real time is ignored: we step the simulation ourselves, as many steps as the time budget allows.
            A.tick(PlayLayer::get(), [this] { return this->fwStep(); });
            return;
        }
        GJBaseGameLayer::update(dt);
    }
};

// ---------------------------------------------------------------------------
// PlayLayer: death/complete interception, respawn handling, cleanup
// ---------------------------------------------------------------------------
class $modify(FWPlayLayer, PlayLayer) {
    struct Fields {
        ~Fields() {
            fw::Analyzer::get().forget();
            fw::State::get().resetAll(false);
        }
    };

    void resetLevel() {
        PlayLayer::resetLevel();
        fw::onReset(this);
    }

    CheckpointObject* createCheckpoint() {
        auto cp = PlayLayer::createCheckpoint();
        if (cp) fw::State::get().cpFrames[cp] = fw::State::get().frame;
        return cp;
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        auto& A = fw::Analyzer::get();
        if (A.active()) {
            // Fast stepping trips GD's hidden anticheat spike. That kill is not a real hazard, ignore it.
            if (obj && obj == m_anticheatSpike) return;
            A.onDeath(); // no death animation or respawn during a test
            return;
        }
        PlayLayer::destroyPlayer(player, obj);
    }

    void levelComplete() {
        auto& A = fw::Analyzer::get();
        if (A.active()) {
            A.onComplete();
            return;
        }
        fw::onLevelComplete(this);
        PlayLayer::levelComplete();
    }

    void pauseGame(bool b) {
        auto& A = fw::Analyzer::get();
        if (A.active()) { // pausing during analysis cancels it
            A.cancel(this, "Analysis cancelled");
            return;
        }
        PlayLayer::pauseGame(b);
    }

    void onQuit() {
        fw::Analyzer::get().forget();
        fw::State::get().resetAll(true);
        PlayLayer::onQuit();
    }
};

// ---------------------------------------------------------------------------
// Pause menu button (left side)
// ---------------------------------------------------------------------------
class $modify(FWPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        NodeIDs::provideFor(this);

        // Extreme demon face. Falls back to the generic demon face if the frame name is wrong.
        auto spr = CCSprite::createWithSpriteFrameName("diffIcon_10_btn_001.png");
        if (!spr || spr->getContentSize().width < 1.f) spr = CCSprite::createWithSpriteFrameName("diffIcon_06_btn_001.png");
        spr->setScale(0.9f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FWPauseLayer::onFrameWindow));
        btn->setID("fw-button"_spr);

        if (auto menu = this->getChildByID("left-button-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            // Fallback: own menu on the left edge.
            auto win = CCDirector::get()->getWinSize();
            auto menu = CCMenu::create();
            menu->setPosition({0.f, 0.f});
            btn->setPosition({28.f, win.height / 2.f});
            menu->addChild(btn);
            this->addChild(menu, 10);
        }
    }

    void onFrameWindow(CCObject*) {
        if (auto p = fw::FWPopup::create(this)) p->show();
    }
};
