#include "Core.hpp"

using namespace geode::prelude;

namespace fw {

std::vector<Click> Macro::clicks() const {
    std::vector<Click> out;
    bool held = false;
    Click cur;
    for (size_t i = 0; i < inputs.size(); ++i) {
        auto const& in = inputs[i];
        if (in.player != 1) continue;
        if (in.down && !held) {
            cur = Click{};
            cur.down = in.frame;
            cur.downIdx = i;
            held = true;
        } else if (!in.down && held) {
            cur.up = in.frame;
            cur.upIdx = i;
            out.push_back(cur);
            held = false;
        }
    }
    if (held) {
        cur.up = std::max<uint64_t>(duration, cur.down + 1);
        out.push_back(cur);
    }
    return out;
}

bool heldAt(std::vector<Input> const& ev, uint64_t frame) {
    bool held = false;
    for (auto const& e : ev) {
        if (e.frame >= frame) break;
        if (e.player == 1) held = e.down;
    }
    return held;
}

std::vector<Group> groupsFor(int n) {
    struct Base { int lo, hi; ccColor3B c; };
    static Base const base[] = {
        {1, 1, {255, 107, 107}},   // soft red
        {2, 2, {255, 179, 102}},   // orange
        {3, 3, {255, 224, 102}},   // yellow
        {4, 4, {255, 255, 255}},   // white
        {5, 6, {102, 255, 102}},   // green
        {7, 8, {102, 204, 255}},   // light blue
        {9, 12, {128, 128, 255}},  // periwinkle
        {13, 15, {170, 100, 255}}, // purples
        {16, 18, {190, 100, 235}},
        {19, 21, {210, 100, 215}},
        {22, 25, {230, 100, 200}},
    };
    n = std::clamp(n, 1, 25);
    std::vector<Group> out;
    for (auto const& b : base) {
        if (b.lo > n) break;
        out.push_back({b.lo, std::min(b.hi, n), b.c});
    }
    return out;
}

int groupIndexFor(int window, std::vector<Group> const& g) {
    for (size_t i = 0; i < g.size(); ++i) {
        if (window <= g[i].hi) return static_cast<int>(i);
    }
    return static_cast<int>(g.size()) - 1;
}

Settings Settings::load() {
    auto m = Mod::get();
    Settings s;
    s.maxFrames = std::clamp<int>(m->getSettingValue<int64_t>("max-frames"), 1, 25);
    s.custom = m->getSettingValue<bool>("use-custom-survival");
    s.customFrames = std::clamp<int>(m->getSettingValue<int64_t>("custom-survival-frames"), 1, 240);
    s.fullScan = m->getSettingValue<bool>("full-scan");
    s.skipTps = m->getSettingValue<bool>("skip-tps-check");
    s.budgetMs = std::clamp<int>(m->getSettingValue<int64_t>("analysis-budget-ms"), 2, 40);
    s.overlayX = static_cast<float>(m->getSettingValue<double>("overlay-x"));
    s.overlayYPct = static_cast<float>(m->getSettingValue<double>("overlay-y-percent"));
    s.overlayScale = static_cast<float>(m->getSettingValue<double>("overlay-scale"));
    s.overlayOpacity = static_cast<float>(m->getSettingValue<double>("overlay-opacity"));
    s.extraExport = m->getSettingValue<std::string>("extra-export");
    return s;
}

} // namespace fw
