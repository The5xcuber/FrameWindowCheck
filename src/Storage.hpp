#pragma once
#include "Core.hpp"

namespace fw {

std::string levelKey(GJGameLevel* level);
std::filesystem::path macroPath(std::string const& key);
std::filesystem::path resultsPath(std::string const& key);

// Macros use the GDR2 format (.gdr2).
bool saveMacro(Macro const& m, std::string const& key, std::string& err);
bool loadMacro(std::string const& key, Macro& out, std::string& err);

// Results are always written as JSON (Replay reads it back). The extra export
// (CSV or TXT) is written next to it if enabled in settings.
bool saveResults(Results const& r, std::string const& key, std::string& err);
bool loadResults(std::string const& key, Results& out, std::string& err);

} // namespace fw
