#pragma once
#include "Scene.h"

#include <string>
#include <vector>

class Aircraft;
class WarningSystem;
class Terrain;
class Autopilot;
class CalloutSystem;
struct Controls;

struct HudInfo {
    CamMode cam = CamMode::Chase;
    bool mouseYoke = false;
    bool gamepad = false;
    bool paused = false;
    bool showHelp = false;
    const char* windName = "";
    float time = 0;
    float lastTouchdownFpm = 0;
    float touchdownMsgTimer = 0;
    float touchdownCenterline = 0;
    const Autopilot* ap = nullptr;
    const CalloutSystem* callouts = nullptr;
    // Панель задания (слева сверху): заголовок и строки цели/прогресса.
    const char* missionTitle = nullptr;
    std::vector<std::string> missionLines;
};

void DrawHud(const Aircraft& a, const Controls& c, const WarningSystem& w, const Terrain& t,
             const Camera3D& cam, const HudInfo& info);
