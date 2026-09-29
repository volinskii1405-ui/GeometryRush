// Settings.cpp
#include "Settings.h"
#include "config.h"
#include "IconRenderer.h"

#include <algorithm>
#include <fstream>

namespace {
std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
int ToInt(const std::string& s, int fallback) {
    try { return std::stoi(s); } catch (...) { return fallback; }
}
} // namespace

bool Settings::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        if (key == "cube") cubeIcon = ToInt(val, 0);
        else if (key == "ball") ballIcon = ToInt(val, 0);
        else if (key == "ship") shipIcon = ToInt(val, 0);
        else if (key == "primary") primary = ToInt(val, primary);
        else if (key == "secondary") secondary = ToInt(val, secondary);
        else if (key == "trail") trail = ToInt(val, 1) != 0;
        else if (key.rfind("best.", 0) == 0) bestPercent[key.substr(5)] = std::clamp(ToInt(val, 0), 0, 100);
    }
    // защита от мусора в файле
    cubeIcon  = std::clamp(cubeIcon, 0, icons::CUBE_COUNT - 1);
    ballIcon  = std::clamp(ballIcon, 0, icons::BALL_COUNT - 1);
    shipIcon  = std::clamp(shipIcon, 0, icons::SHIP_COUNT - 1);
    primary   = std::clamp(primary, 0, icons::PALETTE_SIZE - 1);
    secondary = std::clamp(secondary, 0, icons::PALETTE_SIZE - 1);
    return true;
}

bool Settings::Save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "# Geometry Rush settings\n";
    f << "cube=" << cubeIcon << "\n";
    f << "ball=" << ballIcon << "\n";
    f << "ship=" << shipIcon << "\n";
    f << "primary=" << primary << "\n";
    f << "secondary=" << secondary << "\n";
    f << "trail=" << (trail ? 1 : 0) << "\n";
    for (const auto& kv : bestPercent) f << "best." << kv.first << "=" << kv.second << "\n";
    return (bool)f;
}

int Settings::Best(const std::string& levelId) const {
    auto it = bestPercent.find(levelId);
    return it == bestPercent.end() ? 0 : it->second;
}

void Settings::ReportProgress(const std::string& levelId, int percent) {
    int& b = bestPercent[levelId];
    b = std::max(b, std::clamp(percent, 0, 100));
}

int& Settings::IconFor(int mode) {
    switch ((PlayerMode)mode) {
        case PlayerMode::Ball: return ballIcon;
        case PlayerMode::Ship: return shipIcon;
        default: return cubeIcon;
    }
}
