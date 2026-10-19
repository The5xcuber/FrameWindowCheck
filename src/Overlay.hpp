#pragma once
#include "Core.hpp"

namespace fw {

// Left side "frame window leaderboard".
class WindowOverlay : public cocos2d::CCNode {
public:
    static WindowOverlay* create(int maxFrames);
    void resetCounts();
    void add(int window);

protected:
    bool init(int maxFrames);
    void refresh(size_t i);

    std::vector<Group> m_groups;
    std::vector<int> m_counts;
    std::vector<cocos2d::CCLabelBMFont*> m_count;
    std::vector<cocos2d::CCLabelBMFont*> m_countShadow;
};

// Progress readout and cancel button shown while analyzing.
class AnalysisHUD : public cocos2d::CCNode {
public:
    static AnalysisHUD* create(std::function<void()> onCancel);
    void set(std::string const& text, cocos2d::ccColor3B color, float progress01);

protected:
    bool init(std::function<void()> onCancel);
    void onCancel(cocos2d::CCObject*);

    std::function<void()> m_onCancel;
    cocos2d::CCLabelBMFont* m_label = nullptr;
    cocos2d::CCLayerColor* m_bar = nullptr;
};

} // namespace fw
