#include "Storage.hpp"

#include <cstdio>
#include <fstream>
#include <gdr/gdr.hpp>
#include <iterator>
#include <sstream>

using namespace geode::prelude;

namespace fw {
namespace {

struct Bot : gdr::Replay<Bot, gdr::Input<>> {
    Bot() : Replay("FrameWindowCounter", 1) {}
};

std::filesystem::path dir() {
    auto p = Mod::get()->getSaveDir() / "macros";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

std::string sanitize(std::string s) {
    for (auto& c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    }
    return s;
}

bool writeText(std::filesystem::path const& p, std::string const& text) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f << text;
    return static_cast<bool>(f);
}

std::string listJson(std::vector<int> const& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        s += std::to_string(v[i]);
        if (i + 1 < v.size()) s += ",";
    }
    return s + "]";
}

std::string toJson(Results const& R) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"maxFrames\": " << R.maxFrames << ",\n";
    o << "  \"customSurvival\": " << (R.custom ? "true" : "false") << ",\n";
    o << "  \"customSurvivalFrames\": " << R.customFrames << ",\n";
    o << "  \"clicks\": [\n";
    for (size_t i = 0; i < R.clicks.size(); ++i) {
        auto const& c = R.clicks[i];
        // Keep each click on exactly one line in exactly this field order:
        // loadResults() below parses these lines with sscanf.
        o << "    {\"index\":" << c.index << ",\"frame\":" << c.frame << ",\"hold\":" << c.hold
          << ",\"window\":" << c.window << ",\"passed\":" << listJson(c.passed)
          << ",\"failed\":" << listJson(c.failed) << "}" << (i + 1 < R.clicks.size() ? "," : "") << "\n";
    }
    o << "  ]\n}\n";
    return o.str();
}

std::string toCsv(Results const& R) {
    std::ostringstream o;
    o << "index,frame,hold,window\n";
    for (auto const& c : R.clicks) o << c.index << "," << c.frame << "," << c.hold << "," << c.window << "\n";
    return o.str();
}

std::string toTxt(Results const& R) {
    std::ostringstream o;
    o << "Max frames checked: " << R.maxFrames << "\n";
    for (auto const& c : R.clicks) {
        o << "Click " << (c.index + 1) << " @ frame " << c.frame << ", hold " << c.hold << " frames, window " << c.window << "\n";
    }
    return o.str();
}

} // namespace

std::string levelKey(GJGameLevel* level) {
    int id = level->m_levelID.value();
    if (id > 0) return std::to_string(id);
    return "local_" + sanitize(std::string(level->m_levelName));
}

std::filesystem::path macroPath(std::string const& key) { return dir() / (key + ".gdr2"); }
std::filesystem::path resultsPath(std::string const& key) { return dir() / (key + ".windows.json"); }

bool saveMacro(Macro const& m, std::string const& key, std::string& err) {
    Bot r;
    r.author = std::string(GJAccountManager::get()->m_username);
    r.description = "Recorded with Frame Window Counter";
    r.duration = static_cast<float>(m.duration) / 240.f;
    r.gameVersion = GEODE_COMP_GD_VERSION;
    r.framerate = 240.0;
    r.levelInfo.id = static_cast<uint32_t>(std::max(0, m.levelID));
    r.levelInfo.name = m.levelName;
    for (auto const& in : m.inputs) {
        r.inputs.push_back(gdr::Input<>(in.frame, 1, false, in.down));
    }

    auto res = r.exportData(); // GDR2 returns a result type, adjust here if its API differs
    if (res.isErr()) {
        err = "GDR export failed";
        return false;
    }
    auto bytes = std::move(res).unwrap();
    std::ofstream f(macroPath(key), std::ios::binary);
    if (!f) {
        err = "Could not open macro file for writing";
        return false;
    }
    f.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

bool loadMacro(std::string const& key, Macro& out, std::string& err) {
    auto p = macroPath(key);
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        err = "No macro found for this level. Record one first.";
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    auto res = Bot::importData(bytes); // see note in saveMacro
    if (res.isErr()) {
        err = "Macro file could not be read (GDR import failed)";
        return false;
    }
    auto r = std::move(res).unwrap();

    out = Macro{};
    out.levelID = static_cast<int>(r.levelInfo.id);
    out.levelName = r.levelInfo.name;
    for (auto const& in : r.inputs) {
        if (in.button == 1 && !in.player2) out.inputs.push_back({in.frame, in.down, 1});
    }
    std::stable_sort(out.inputs.begin(), out.inputs.end(), [](Input const& a, Input const& b) { return a.frame < b.frame; });
    uint64_t fromHeader = static_cast<uint64_t>(r.duration * r.framerate + 0.5);
    uint64_t lastFrame = out.inputs.empty() ? 0 : out.inputs.back().frame + 1;
    out.duration = std::max(fromHeader, lastFrame);
    return true;
}

bool saveResults(Results const& r, std::string const& key, std::string& err) {
    auto jp = resultsPath(key);
    if (!writeText(jp, toJson(r))) {
        err = "Could not write results file";
        return false;
    }
    auto extra = Mod::get()->getSettingValue<std::string>("extra-export");
    if (extra == "CSV") writeText(jp.parent_path() / (key + ".windows.csv"), toCsv(r));
    else if (extra == "TXT") writeText(jp.parent_path() / (key + ".windows.txt"), toTxt(r));
    return true;
}

bool loadResults(std::string const& key, Results& out, std::string& err) {
    std::ifstream f(resultsPath(key));
    if (!f) {
        err = "No analysis results for this level. Run Analyze first.";
        return false;
    }
    out = Results{};
    std::string line;
    while (std::getline(f, line)) {
        int iv = 0;
        size_t idx = 0;
        unsigned long long frame = 0;
        int hold = 0, window = 0;
        if (std::sscanf(line.c_str(), " \"maxFrames\": %d", &iv) == 1) {
            out.maxFrames = iv;
        } else if (std::sscanf(line.c_str(), " \"customSurvivalFrames\": %d", &iv) == 1) {
            out.customFrames = iv;
        } else if (std::sscanf(line.c_str(), " {\"index\":%zu,\"frame\":%llu,\"hold\":%d,\"window\":%d", &idx, &frame, &hold, &window) == 4) {
            ClickResult c;
            c.index = idx;
            c.frame = frame;
            c.hold = hold;
            c.window = window;
            out.clicks.push_back(std::move(c));
        }
    }
    if (out.clicks.empty()) {
        err = "Results file is empty or unreadable";
        return false;
    }
    return true;
}

} // namespace fw
