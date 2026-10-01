#pragma once
#include "Scene.h"

class Aircraft;
class WarningSystem;
class Terrain;
struct Controls;

struct HudInfo {
    CamMode cam = CamMode::Chase;
    bool mouseYoke = false;
    bool gamepad = false;
    bool paused = false;
    bool showHelp = false;
    int windPreset = 0;
    const char* windName = "";
    const char* scenarioName = "";
    float time = 0;
    float lastTouchdownFpm = 0;
    float touchdownMsgTimer = 0;
    float touchdownCenterline = 0;
};

void DrawHud(const Aircraft& a, const Controls& c, const WarningSystem& w, const Terrain& t,
             const Camera3D& cam, const HudInfo& info);
