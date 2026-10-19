#include "Overlay.hpp"

using namespace geode::prelude;

namespace fw {

WindowOverlay* WindowOverlay::create(int n) {
    auto ret = new WindowOverlay();
    if (ret->init(n)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool WindowOverlay::init(int n) {
    if (!CCNode::init()) return false;

    auto const s = Settings::load();
    m_groups = groupsFor(n);
    m_counts.assign(m_groups.size(), 0);

    float const us = s.overlayScale;
    float const fontScale = 0.5f * us;
    float const rowH = 26.f * us;
    GLubyte const op = static_cast<GLubyte>(255.f * s.overlayOpacity);
    GLubyte const shadowOp = static_cast<GLubyte>(op * 0.6f);
    float const off = 1.5f * us;

    std::vector<std::string> labels;
    float maxW = 0.f;
    for (auto const& g : m_groups) {
        labels.push_back(g.lo == g.hi ? fmt::format("{}:", g.lo) : fmt::format("{}-{}:", g.lo, g.hi));
        auto probe = CCLabelBMFont::create(labels.back().c_str(), "bigFont.fnt");
        maxW = std::max(maxW, probe->getContentSize().width * fontScale);
    }
    float const countX = maxW + 10.f * us;

    auto make = [&](std::string const& text, ccColor3B col, float x, float y, bool shadow) {
        auto l = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        l->setScale(fontScale);
        l->setAnchorPoint({0.f, 0.5f});
        l->setColor(shadow ? ccColor3B{0, 0, 0} : col);
        l->setOpacity(shadow ? shadowOp : op);
        l->setPosition({shadow ? x + off : x, shadow ? y - off : y});
        this->addChild(l, shadow ? 0 : 1);
        return l;
    };

    // Index 0 is the bottom row ("1:"), the highest group is at the top.
    for (size_t i = 0; i < m_groups.size(); ++i) {
        float const y = rowH * static_cast<float>(i) + rowH * 0.5f;
        make(labels[i], m_groups[i].color, 0.f, y, true);
        make(labels[i], m_groups[i].color, 0.f, y, false);
        m_countShadow.push_back(make("0", {255, 255, 255}, countX, y, true));
        m_count.push_back(make("0", {255, 255, 255}, countX, y, false));
    }

    auto win = CCDirector::get()->getWinSize();
    this->setContentSize({countX + 60.f * us, rowH * static_cast<float>(m_groups.size())});
    this->setAnchorPoint({0.f, 0.5f});
    this->setPosition({s.overlayX, win.height * s.overlayYPct / 100.f});
    return true;
}

void WindowOverlay::resetCounts() {
    std::fill(m_counts.begin(), m_counts.end(), 0);
    for (size_t i = 0; i < m_counts.size(); ++i) refresh(i);
}

void WindowOverlay::add(int window) {
    auto idx = static_cast<size_t>(groupIndexFor(window, m_groups));
    ++m_counts[idx];
    refresh(idx);
}

void WindowOverlay::refresh(size_t i) {
    auto str = std::to_string(m_counts[i]);
    m_count[i]->setString(str.c_str());
    m_countShadow[i]->setString(str.c_str());
}

// ---------------------------------------------------------------------------

AnalysisHUD* AnalysisHUD::create(std::function<void()> onCancel) {
    auto ret = new AnalysisHUD();
    if (ret->init(std::move(onCancel))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool AnalysisHUD::init(std::function<void()> onCancel) {
    if (!CCNode::init()) return false;
    m_onCancel = std::move(onCancel);
    auto win = CCDirector::get()->getWinSize();

    m_label = CCLabelBMFont::create("Starting...", "bigFont.fnt");
    m_label->setScale(0.4f);
    m_label->setPosition({win.width / 2.f, win.height - 18.f});
    this->addChild(m_label);

    auto bg = CCLayerColor::create({0, 0, 0, 150}, 240.f, 6.f);
    bg->setPosition({win.width / 2.f - 120.f, win.height - 36.f});
    this->addChild(bg);

    m_bar = CCLayerColor::create({90, 255, 120, 255}, 1.f, 6.f);
    m_bar->setPosition({win.width / 2.f - 120.f, win.height - 36.f});
    this->addChild(m_bar);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    auto btn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Cancel"), this, menu_selector(AnalysisHUD::onCancel));
    btn->setPosition({win.width / 2.f, win.height - 62.f});
    menu->addChild(btn);
    this->addChild(menu);
    return true;
}

void AnalysisHUD::set(std::string const& text, ccColor3B color, float p) {
    m_label->setString(text.c_str());
    m_label->setColor(color);
    m_bar->setContentSize({std::max(1.f, 240.f * std::clamp(p, 0.f, 1.f)), 6.f});
}

void AnalysisHUD::onCancel(CCObject*) {
    Ref<AnalysisHUD> keepAlive(this); // the callback removes this node
    if (m_onCancel) m_onCancel();
}

} // namespace fw
