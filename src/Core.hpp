#pragma once
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace fw {

inline constexpr float kStepDt = 1.f / 240.f;
inline constexpr size_t kNone = static_cast<size_t>(-1);

// One button event. v1 only uses player 1, but the field is kept so player 2
// can be added later without changing the data layout.
struct Input {
    uint64_t frame = 0;
    bool down = false;
    uint8_t player = 1;
};

struct Click {
    uint64_t down = 0;
    uint64_t up = 0;
    uint8_t player = 1;
    size_t downIdx = kNone; // index into Macro::inputs
    size_t upIdx = kNone;   // kNone if the button was never released
    int hold() const { return static_cast<int>(up - down); }
};

struct Macro {
    int levelID = 0;
    std::string levelName;
    uint64_t duration = 0;
    std::vector<Input> inputs; // must be sorted by frame
    std::vector<Click> clicks() const;
};

struct ClickResult {
    size_t index = 0;
    uint64_t frame = 0;
    int hold = 0;
    int window = 0; // 0 means the unshifted click itself did not survive when re-simulated
    std::vector<int> passed;
    std::vector<int> failed;
};

struct Results {
    int maxFrames = 12;
    bool custom = false;
    int customFrames = 12;
    std::vector<ClickResult> clicks;
};

struct Group {
    int lo, hi;
    cocos2d::ccColor3B color;
};

struct Settings {
    int maxFrames = 12;
    bool custom = false;
    int customFrames = 12;
    bool fullScan = false;
    bool skipTps = false;
    int budgetMs = 12;
    float overlayX = 8.f;
    float overlayYPct = 50.f;
    float overlayScale = 0.6f;
    float overlayOpacity = 1.f;
    std::string extraExport = "None";
    static Settings load();
};

// True if the button is held right before `frame` is processed (events with
// frame < `frame` applied). `ev` must be sorted.
bool heldAt(std::vector<Input> const& ev, uint64_t frame);

// Leaderboard rows for a given max frame setting.
std::vector<Group> groupsFor(int maxFrames);
// Row index for a window size. Windows larger than the last row saturate into it.
int groupIndexFor(int window, std::vector<Group> const& groups);

} // namespace fw
