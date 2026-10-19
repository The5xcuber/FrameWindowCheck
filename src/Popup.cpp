#include "Popup.hpp"

#include "Session.hpp"
#include "State.hpp"

using namespace geode::prelude;

namespace fw {

FWPopup* FWPopup::create(PauseLayer* pause) {
    auto ret = new FWPopup();
    if (ret->init(pause)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool FWPopup::init(PauseLayer* pause) {
    constexpr float W = 280.f, H = 250.f;
    if (!Popup::init(W, H)) return false;
    m_pause = pause;
    this->setTitle("Frame Window Counter");

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu);

    bool const recording = State::get().mode == Mode::Record;
    auto addBtn = [&](char const* text, SEL_MenuHandler cb, float y) {
        auto btn = CCMenuItemSpriteExtra::create(ButtonSprite::create(text, 200, true, "goldFont.fnt", "GJ_button_01.png", 40.f, 0.8f), this, cb);
        btn->setPosition({W / 2.f, y});
        menu->addChild(btn);
    };
    addBtn(recording ? "Stop & Save" : "1. Record", menu_selector(FWPopup::onRecord), H - 62.f);
    addBtn("2. Analyze", menu_selector(FWPopup::onAnalyze), H - 108.f);
    addBtn("3. Replay", menu_selector(FWPopup::onReplay), H - 154.f);

    // Max frames stepper
    auto maxFrames = Settings::load().maxFrames;
    m_maxLabel = CCLabelBMFont::create(fmt::format("Max frames: {}", maxFrames).c_str(), "bigFont.fnt");
    m_maxLabel->setScale(0.45f);
    m_maxLabel->setPosition({W / 2.f - 20.f, 36.f});
    m_mainLayer->addChild(m_maxLabel);
    for (int dir : {-1, 1}) {
        auto spr = CCSprite::createWithSpriteFrameName(dir < 0 ? "edit_leftBtn_001.png" : "edit_rightBtn_001.png");
        spr->setScale(0.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FWPopup::onStep));
        btn->setTag(dir);
        btn->setPosition({dir < 0 ? 22.f : W - 62.f, 36.f});
        menu->addChild(btn);
    }

    auto gear = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
    gear->setScale(0.7f);
    auto gbtn = CCMenuItemSpriteExtra::create(gear, this, menu_selector(FWPopup::onSettings));
    gbtn->setPosition({W - 26.f, 36.f});
    menu->addChild(gbtn);
    return true;
}

void FWPopup::onRecord(CCObject*) {
    auto pl = PlayLayer::get();
    if (!pl) return;
    if (State::get().mode == Mode::Record) {
        stopRecord(pl);
        this->onClose(nullptr);
        return;
    }
    this->onClose(nullptr);
    startRecord(pl, m_pause);
}

void FWPopup::onAnalyze(CCObject*) {
    auto pl = PlayLayer::get();
    if (!pl) return;
    Ref<PauseLayer> pause = m_pause;
    this->onClose(nullptr);
    startAnalyze(pl, pause);
}

void FWPopup::onReplay(CCObject*) {
    auto pl = PlayLayer::get();
    if (!pl) return;
    Ref<PauseLayer> pause = m_pause;
    this->onClose(nullptr);
    startReplay(pl, pause);
}

void FWPopup::onSettings(CCObject*) {
    geode::openSettingsPopup(Mod::get(), false);
}

void FWPopup::onStep(CCObject* sender) {
    int dir = static_cast<CCNode*>(sender)->getTag();
    int v = std::clamp(Settings::load().maxFrames + dir, 1, 25);
    Mod::get()->setSettingValue<int64_t>("max-frames", v);
    m_maxLabel->setString(fmt::format("Max frames: {}", v).c_str());
}

} // namespace fw
