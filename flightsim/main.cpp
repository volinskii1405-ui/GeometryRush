// Flight Sim — 3D-симулятор лёгкого реактивного самолёта на raylib.
// Физика: 6 степеней свободы, аэродинамические коэффициенты, сваливание, шасси с амортизаторами.
// Сигнализация: PULL UP / TERRAIN (GPWS + прогноз по рельефу), OVERSPEED, BANK ANGLE, STALL и др.

#include "Aircraft.h"
#include "Audio.h"
#include "Hud.h"
#include "Scene.h"
#include "Terrain.h"
#include "Warnings.h"

#include "rlgl.h"

using namespace fs;

namespace {

constexpr float PHYSICS_DT = 1.0f / 240.0f;

struct WindPreset { const char* name; float fromDeg, speedKt, gustKt, turbulence; };
const WindPreset kWinds[] = {
    {"CALM", 0, 0, 0, 0},
    {"270/10", 270, 10, 0, 0.3f},
    {"360/15 G25 (crosswind)", 360, 15, 10, 0.8f},
    {"090/25 TURBULENT", 90, 25, 8, 1.6f},
};
constexpr int kWindCount = sizeof(kWinds) / sizeof(kWinds[0]);

const char* kScenarioNames[] = {"", "TAKEOFF  RWY 09", "ILS APPROACH  RWY 09", "MOUNTAINS  LOW LEVEL"};

struct Sim {
    Terrain terrain;
    Scene scene;
    Aircraft aircraft;
    WarningSystem warnings;
    AudioSystem audio;
    Controls ctl;

    CamMode cam = CamMode::Chase;
    int scenario = 1;
    int wind = 0;
    bool paused = false;
    bool help = false;
    bool mouseYoke = false;
    bool gamepad = false;
    float accumulator = 0;
    float time = 0;
    float crashAge = -1;
    float touchdownTimer = 0, touchdownFpm = 0, touchdownCl = 0;
};

Vector3 WindAt(const Sim& s, Vector3 pos)
{
    const WindPreset& w = kWinds[s.wind];
    if (w.speedKt <= 0.0f) return {};
    float agl = pos.y - s.terrain.SurfaceHeight(pos.x, pos.z);
    float layer = 0.65f + 0.35f * SmoothStep(0.0f, 600.0f, agl);   // у земли ветер слабее
    float t = s.time;
    float gust = w.gustKt * (0.5f + 0.5f * sinf(t * 0.37f) * sinf(t * 0.91f + 1.3f));
    float spd = (w.speedKt + gust) * KT_TO_MS * layer;
    float from = w.fromDeg * DEG2RAD;
    Vector3 v{-sinf(from) * spd, 0, cosf(from) * spd};   // воздух движется «от» направления ветра
    float tb = w.turbulence;
    v.x += tb * (sinf(t * 1.3f) + 0.6f * sinf(t * 2.9f + 1.0f) + 0.3f * sinf(t * 6.1f + 2.0f));
    v.y += tb * 0.8f * (sinf(t * 1.7f + 0.5f) + 0.5f * sinf(t * 3.7f + 2.2f) + 0.3f * sinf(t * 7.3f));
    v.z += tb * (sinf(t * 1.1f + 2.0f) + 0.6f * sinf(t * 3.3f) + 0.3f * sinf(t * 5.7f + 1.0f));
    return v;
}

void StartScenario(Sim& s, int which)
{
    s.scenario = which;
    Controls c;
    const float E = Terrain::kAirportElev;
    const float L = Terrain::kRunwayHalfLen;
    switch (which) {
    case 2: {   // заход по ILS: 6 морских миль до точки приземления, на глиссаде 3°
        c.flapsLever = 2;
        c.gearDown = true;
        float dist = 11000.0f;
        float x = -(L - 300.0f) - dist;
        float y = E + 1.75f + dist * tanf(3.0f * DEG2RAD);
        float v = 135.0f * KT_TO_MS * sqrtf(1.225f / AirDensity(y));
        Aircraft::Trim tr = Aircraft::ComputeTrim(v, y, ac::FLAP_DEG[2], true, -3.0f);
        c.throttle = tr.throttle;
        c.trim = tr.trim;
        s.aircraft.Reset({x, y, 0}, 90.0f, v, false, c, tr.alphaDeg - 3.0f, -3.0f);
        break;
    }
    case 3: {   // низко над горами в сторону самой высокой вершины
        c.flapsLever = 0;
        c.gearDown = false;
        Vector3 peak = s.terrain.HighestPoint();
        Vector3 toCenter = Vector3Normalize(Vector3Subtract(Vector3{0, 0, 0}, Vector3{peak.x, 0, peak.z}));
        Vector3 start = Vector3Add(Vector3{peak.x, 0, peak.z}, Vector3Scale(toCenter, 14000.0f));
        float hdg = WrapDeg360(atan2f(peak.x - start.x, -(peak.z - start.z)) * RAD2DEG);
        float y = fmaxf(peak.y * 0.75f, s.terrain.SurfaceHeight(start.x, start.z) + 450.0f);
        float v = 220.0f * KT_TO_MS * sqrtf(1.225f / AirDensity(y));
        Aircraft::Trim tr = Aircraft::ComputeTrim(v, y, 0.0f, false, 0.0f);
        c.throttle = tr.throttle;
        c.trim = tr.trim;
        s.aircraft.Reset({start.x, y, start.z}, hdg, v, false, c, tr.alphaDeg, 0.0f);
        break;
    }
    default: {  // на исполнительном старте ВПП 09, стояночный тормоз включён
        s.scenario = 1;
        c.throttle = 0.0f;
        c.flapsLever = 1;
        c.gearDown = true;
        c.parkingBrake = true;
        c.trim = 0.25f;
        s.aircraft.Reset({-L + 70.0f, E + 1.74f, 0}, 90.0f, 0.0f, true, c);
        break;
    }
    }
    s.ctl = c;
    s.warnings.Reset();
    s.scene.ResetCamera(s.aircraft);
    s.accumulator = 0;
    s.crashAge = -1;
    s.touchdownTimer = 0;
    s.paused = false;
}

float Axis(int pad, int axis, float dead = 0.08f)
{
    float v = GetGamepadAxisMovement(pad, axis);
    if (fabsf(v) < dead) return 0.0f;
    return (v - Sign(v) * dead) / (1.0f - dead);
}

// Клавиатура двигает «штурвал» с конечной скоростью и возвращает его в нейтраль,
// поэтому управление плавное, как у настоящего самолёта, а не «цифровое».
float KeyStick(float current, float input, float dt)
{
    if (input != 0.0f) {
        float rate = (current * input < 0.0f) ? 4.0f : 1.6f;
        return MoveTowards(current, input, rate * dt);
    }
    return MoveTowards(current, 0.0f, 2.2f * dt);
}

void HandleInput(Sim& s, float dt)
{
    Controls& c = s.ctl;
    auto down = [](int k) { return IsKeyDown(k) ? 1.0f : 0.0f; };

    if (IsKeyPressed(KEY_F1)) s.help = !s.help;
    if (IsKeyPressed(KEY_ESCAPE) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) {
        if (s.help) s.help = false;
        else s.paused = !s.paused;
    }
    if (IsKeyPressed(KEY_R)) StartScenario(s, s.scenario);
    if (s.paused || s.aircraft.crashed) {
        if (IsKeyPressed(KEY_ONE)) StartScenario(s, 1);
        if (IsKeyPressed(KEY_TWO)) StartScenario(s, 2);
        if (IsKeyPressed(KEY_THREE)) StartScenario(s, 3);
    }
    if (IsKeyPressed(KEY_C) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP))
        s.cam = (CamMode)(((int)s.cam + 1) % (int)CamMode::Count);
    if (IsKeyPressed(KEY_F8)) s.wind = (s.wind + 1) % kWindCount;
    if (IsKeyPressed(KEY_M)) s.mouseYoke = !s.mouseYoke;
    if (s.paused) return;

    // ---- штурвал и педали
    float kPitch = down(KEY_S) + down(KEY_DOWN) - down(KEY_W) - down(KEY_UP);
    float kRoll = down(KEY_D) + down(KEY_RIGHT) - down(KEY_A) - down(KEY_LEFT);
    float kYaw = down(KEY_E) - down(KEY_Q);
    kPitch = Clampf(kPitch, -1, 1);
    kRoll = Clampf(kRoll, -1, 1);

    bool padAvail = IsGamepadAvailable(0);
    float gp = 0, gr = 0, gy = 0;
    if (padAvail) {
        gp = Axis(0, GAMEPAD_AXIS_LEFT_Y);
        gr = Axis(0, GAMEPAD_AXIS_LEFT_X);
        float lt = (GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_TRIGGER) + 1.0f) * 0.5f;
        float rt = (GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER) + 1.0f) * 0.5f;
        gy = Clampf(rt - lt + Axis(0, GAMEPAD_AXIS_RIGHT_X), -1, 1);
        if (gp != 0 || gr != 0 || gy != 0) s.gamepad = true;
    }
    if (kPitch != 0 || kRoll != 0 || kYaw != 0) s.gamepad = false;

    if (s.gamepad) {
        c.pitch = gp;
        c.roll = gr;
        c.yaw = gy;
    } else if (s.mouseYoke) {
        Vector2 m = GetMousePosition();
        float cx = GetScreenWidth() * 0.5f, cy = GetScreenHeight() * 0.4f;
        float range = GetScreenHeight() * 0.3f;
        c.pitch = Clampf((m.y - cy) / range, -1, 1);
        c.roll = Clampf((m.x - cx) / range, -1, 1);
        c.yaw = KeyStick(c.yaw, kYaw, dt);
    } else {
        c.pitch = KeyStick(c.pitch, kPitch, dt);
        c.roll = KeyStick(c.roll, kRoll, dt);
        c.yaw = KeyStick(c.yaw, kYaw, dt);
    }

    // ---- РУД
    float thr = down(KEY_LEFT_SHIFT) + down(KEY_RIGHT_SHIFT) + down(KEY_PAGE_UP)
              - down(KEY_LEFT_CONTROL) - down(KEY_RIGHT_CONTROL) - down(KEY_PAGE_DOWN);
    if (padAvail) thr -= Axis(0, GAMEPAD_AXIS_RIGHT_Y) * 1.5f;
    c.throttle = Clampf(c.throttle + Clampf(thr, -1.5f, 1.5f) * 0.45f * dt, 0, 1);
    if (IsKeyPressed(KEY_Z)) c.throttle = 0.0f;
    if (IsKeyPressed(KEY_X)) c.throttle = 1.0f;

    // ---- триммер
    float trim = down(KEY_RIGHT_BRACKET) + down(KEY_HOME) - down(KEY_LEFT_BRACKET) - down(KEY_END);
    if (padAvail) trim += (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP) ? 1.0f : 0.0f)
                        - (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN) ? 1.0f : 0.0f);
    c.trim = Clampf(c.trim + Clampf(trim, -1, 1) * 0.25f * dt, -1, 1);

    // ---- механизация, шасси, тормоза
    if (IsKeyPressed(KEY_F) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) {
        if (c.flapsLever < 3) { ++c.flapsLever; s.audio.PlayClick(); }
    }
    if (IsKeyPressed(KEY_V) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) {
        if (c.flapsLever > 0) { --c.flapsLever; s.audio.PlayClick(); }
    }
    if (IsKeyPressed(KEY_G) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) {
        c.gearDown = !c.gearDown;
        s.audio.PlayClick();
    }
    if (IsKeyPressed(KEY_P)) { c.parkingBrake = !c.parkingBrake; s.audio.PlayClick(); }
    if (IsKeyPressed(KEY_SLASH) || IsKeyPressed(KEY_K) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) {
        c.speedbrake = !c.speedbrake;
        s.audio.PlayClick();
    }
    bool brake = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_B) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    c.brakes = MoveTowards(c.brakes, brake ? 1.0f : 0.0f, 3.0f * dt);
}

} // namespace

int main()
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "Flight Sim");
    SetWindowMinSize(960, 600);
    SetExitKey(KEY_NULL);
    SetTargetFPS(144);
    rlSetClipPlanes(1.0, 60000.0);

    static Sim s;
    s.audio.Init();
    s.scene.Init();
    s.terrain.Generate(1337u);
    s.terrain.BuildMeshes(s.scene.LitShader());
    StartScenario(s, 1);

    bool quit = false;
    while (!WindowShouldClose() && !quit) {
        float dt = fminf(GetFrameTime(), 0.1f);
        HandleInput(s, dt);
        if (s.paused && IsKeyPressed(KEY_Q)) quit = true;

        if (!s.paused) {
            s.time += dt;
            s.accumulator += dt;
            bool wasCrashed = s.aircraft.crashed;
            while (s.accumulator >= PHYSICS_DT) {
                s.aircraft.Step(PHYSICS_DT, s.ctl, s.terrain, WindAt(s, s.aircraft.pos));
                s.accumulator -= PHYSICS_DT;
            }
            if (s.aircraft.touchdown) {
                s.aircraft.touchdown = false;
                s.touchdownFpm = s.aircraft.touchdownFpm;
                s.touchdownCl = fabsf(s.aircraft.pos.z);
                s.touchdownTimer = 6.0f;
                s.audio.PlayTouchdown(fabsf(s.touchdownFpm) / 600.0f);
            }
            if (s.aircraft.crashed && !wasCrashed) {
                s.crashAge = 0;
                s.audio.PlayCrash();
            }
            if (s.crashAge >= 0) s.crashAge += dt;
            s.touchdownTimer = fmaxf(s.touchdownTimer - dt, 0.0f);
            s.warnings.Update(s.aircraft, s.terrain, dt);
        }
        s.audio.Update(s.aircraft, s.warnings, s.paused);
        s.scene.crashAge = s.crashAge;
        s.scene.UpdateCamera(s.aircraft, s.cam, dt);

        BeginDrawing();
        ClearBackground(Color{150, 180, 215, 255});
        s.scene.Draw(s.aircraft, s.terrain, s.cam, s.time, WindAt(s, s.aircraft.pos));

        HudInfo info;
        info.cam = s.cam;
        info.mouseYoke = s.mouseYoke;
        info.gamepad = s.gamepad;
        info.paused = s.paused;
        info.showHelp = s.help;
        info.windPreset = s.wind;
        info.windName = kWinds[s.wind].name;
        info.scenarioName = kScenarioNames[s.scenario];
        info.time = s.time;
        info.lastTouchdownFpm = s.touchdownFpm;
        info.touchdownMsgTimer = s.touchdownTimer;
        info.touchdownCenterline = s.touchdownCl;
        DrawHud(s.aircraft, s.ctl, s.warnings, s.terrain, s.scene.camera, info);
        DrawFPS(GetScreenWidth() - 90, GetScreenHeight() - 20);
        EndDrawing();
    }

    s.terrain.Unload();
    s.scene.Unload();
    s.audio.Shutdown();
    CloseWindow();
    return 0;
}
