#pragma once
#include "Core.hpp"

namespace fw {

// Geode v5 style popup (not templated).
class FWPopup : public geode::Popup {
public:
    static FWPopup* create(PauseLayer* pause);

protected:
    bool init(PauseLayer* pause);
    void onRecord(cocos2d::CCObject*);
    void onAnalyze(cocos2d::CCObject*);
    void onReplay(cocos2d::CCObject*);
    void onSettings(cocos2d::CCObject*);
    void onStep(cocos2d::CCObject* sender);

    geode::Ref<PauseLayer> m_pause;
    cocos2d::CCLabelBMFont* m_maxLabel = nullptr;
};

} // namespace fw
