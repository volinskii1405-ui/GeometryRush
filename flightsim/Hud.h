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

// Перегрузка глазами пилота: grey 0..1 — сужение поля зрения и потемнение (1 — потеря зрения),
// red 0..1 — «красная пелена» при отрицательной перегрузке, frost 0..1 — иней на стекле.
void DrawVisionEffects(float grey, float red, float frost);

void DrawHud(const Aircraft& a, const Controls& c, const WarningSystem& w, const Terrain& t,
             const Camera3D& cam, const HudInfo& info);
