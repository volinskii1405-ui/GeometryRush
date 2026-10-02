// Flight Sim — 3D-симулятор самолёта на raylib.
// Три самолёта (бизнес-джет, «Сессна», лайнер), физика с 6 степенями свободы, отказы, топливо,
// автопилот, GPWS и другие предупреждения, погода и время суток, задания со звёздами.

#include "Aircraft.h"
#include "Audio.h"
#include "Autopilot.h"
#include "Config.h"
#include "Effects.h"
#include "Hud.h"
#include "Map.h"
#include "Menu.h"
#include "Missions.h"
#include "Scene.h"
#include "Terrain.h"
#include "UiText.h"
#include "Warnings.h"
#include "World.h"

#include "rlgl.h"

using namespace fs;
using ui::L;

namespace {

constexpr float PHYSICS_DT = 1.0f / 240.0f;

struct WindPreset { float fromDeg, speedKt, gustKt, turbulence; };
const WindPreset kWinds[] = {{0, 0, 0, 0}, {270, 10, 0, 0.3f}, {360, 15, 10, 0.8f}, {90, 25, 8, 1.6f}};
constexpr int kWindCount = sizeof(kWinds) / sizeof(kWinds[0]);
const char* WindName(int w)
{
    switch (w) {
    case 0: return L("calm", "штиль");
    case 1: return "270/10";
    case 2: return "360/15G25";
    default: return L("090/25 turb.", "090/25 болт.");
    }
}

struct Sim {
    Terrain terrain;
    Scene scene;
    Effects fx;
    Aircraft aircraft;
    WarningSystem warnings;
    CalloutSystem callouts;
    Autopilot ap;
    AudioSystem audio;
    Controls ctl;
    Environment env;
    Config cfg;
    Menu menu;
    MissionRun run;
    NavMap map;
    std::vector<Vector2> trail;   // пройденный путь для карты
    float trailTimer = 0;
    FlightSetup setup;

    bool flying = false;       // идёт полёт (иначе — фон меню)
    bool paused = false;
    bool help = false;
    bool mouseYoke = false;
    float gGrey = 0, gRed = 0;   // перегрузка глазами пилота
    bool gamepad = false;
    CamMode cam = CamMode::Chase;
    int wind = 0;
    float accumulator = 0;
    float time = 0, menuTime = 0;
    float crashAge = -1, finishAge = -1;
    float touchdownTimer = 0, touchdownFpm = 0, touchdownCl = 0;
    bool quit = false;
};

// Термики: солнце греет сушу, тёплый воздух поднимается столбами диаметром 300–600 м
// (2–4 м/с в ядре, вокруг — слабое опускание). Над морем их нет, вверху их «срезает» облачность.
float ThermalLift(const Sim& s, Vector3 pos)
{
    const float k = s.env.thermals;
    if (k <= 0.0f) return 0.0f;
    const float ground = s.terrain.GroundHeight(pos.x, pos.z);
    if (ground < 2.0f) return 0.0f;
    const float agl = pos.y - ground;
    const float top = Clampf(s.env.clouds ? s.env.cloudBase - ground : 1400.0f, 300.0f, 1800.0f);
    if (agl <= 0.0f || agl > top) return 0.0f;
    const float cell = 1400.0f;
    const int ci = (int)floorf(pos.x / cell), cj = (int)floorf(pos.z / cell);
    float lift = 0.0f;
    for (int dj = -1; dj <= 1; ++dj)
        for (int di = -1; di <= 1; ++di) {
            unsigned h = (unsigned)(ci + di) * 73856093u ^ (unsigned)(cj + dj) * 19349663u;
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            float r1 = (h & 1023) / 1023.0f, r2 = ((h >> 10) & 1023) / 1023.0f, r3 = ((h >> 20) & 1023) / 1023.0f;
            if (r3 < 0.35f) continue;   // в этой клетке термика нет
            float cx = (ci + di + 0.2f + 0.6f * r1) * cell, cz = (cj + dj + 0.2f + 0.6f * r2) * cell;
            // Термик «дышит»: усиливается и слабеет за несколько минут.
            float strength = (1.0f + 2.5f * (r3 - 0.35f) / 0.65f) *
                             (0.6f + 0.4f * sinf(s.time * 2.0f * PI / (300.0f + 200.0f * r1) + r2 * 6.28f));
            float radius = 170.0f + 150.0f * r2;
            float dx = pos.x - cx, dz = pos.z - cz;
            float d2 = (dx * dx + dz * dz) / (radius * radius);
            lift += strength * (expf(-d2) - 0.25f * expf(-d2 / 6.0f));
        }
    return lift * k * SmoothStep(20.0f, 250.0f, agl) * (1.0f - SmoothStep(top * 0.75f, top, agl));
}

Vector3 WindAt(const Sim& s, Vector3 pos)
{
    float lift = ThermalLift(s, pos);
    // В термике слегка болтает.
    Vector3 thermal{0.25f * lift * sinf(s.time * 2.3f + pos.x * 0.01f), lift, 0.25f * lift * sinf(s.time * 1.9f + pos.z * 0.01f)};
    const WindPreset& w = kWinds[s.wind];
    if (w.speedKt <= 0.0f) return thermal;
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
    return Vector3Add(v, thermal);
}

// Свободный полёт из выбранных в меню параметров.
FlightSetup FreeSetup(const Config& cfg)
{
    FlightSetup s;
    s.aircraft = (AircraftKind)(cfg.freeAircraft % (int)AircraftKind::Count);
    s.airport = cfg.freeAirport % world::AirportCount();
    s.targetAirport = s.airport;
    s.time = (TimeOfDay)cfg.freeTime;
    s.weather = (WeatherKind)cfg.freeWeather;
    s.wind = cfg.freeWind;
    s.temp = cfg.freeTemp % 3;
    s.fuelFrac = 0.25f * (cfg.freeFuel % 4 + 1);
    s.fail = cfg.freeFailures ? FailPlan::Random : FailPlan::None;
    s.goal = GoalKind::None;
    const Runway& rw = world::GetAirport(s.airport).rwy;
    int st = cfg.freeStart % 5;
    if (st < 2) {
        s.start = StartKind::Runway;
        s.end = st;
    } else if (st < 4) {
        s.start = StartKind::Final;
        s.end = st - 2;
        s.finalNm = world::GetAirport(s.airport).main ? 6.0f : 3.0f;
    } else {
        s.start = StartKind::Air;
        Vector3 d = rw.Dir(0);
        Vector3 p = Vector3Subtract(rw.center, Vector3Scale(d, 6000.0f));
        s.airPos = {p.x, rw.center.y * M_TO_FT + 3000.0f, p.z};
        s.airHdg = rw.Course(0);
        s.airSpeedKt = GetAircraftType(s.aircraft).cruiseKt;
    }
    return s;
}

// Температура за бортом и облачность — для обледенения и остывания тормозов.
void SyncEnvironment(Sim& s)
{
    Aircraft& a = s.aircraft;
    a.oat = s.env.OatAt(a.pos.y);
    a.inCloud = s.env.InCloud(a.pos.y);
    a.inRain = s.env.rain && a.pos.y < s.env.cloudTop;
    a.qnh = s.env.qnh;
    a.isaDev = s.env.IsaDev();
}

void StartFlight(Sim& s, const FlightSetup& setup)
{
    const bool restart = &setup == &s.setup;   // R — тот же полёт заново: маршрут сохраняется
    s.setup = setup;
    s.aircraft.SetType(setup.aircraft);
    const AircraftType& t = s.aircraft.Type();
    s.env = Environment::Make(setup.time, setup.weather, setup.temp);
    s.scene.SetEnvironment(s.env);
    s.fx.SetEnvironment(s.env);
    s.fx.Clear();
    s.wind = setup.wind;
    s.run.Start(setup, s.terrain);

    const Runway& rw = world::GetAirport(setup.airport).rwy;
    const float mass = t.emptyMass + t.payload + (setup.fuelFrac < 0 ? t.defaultFuel : t.maxFuel * setup.fuelFrac);
    const float fuel = setup.fuelFrac < 0 ? -1.0f : t.maxFuel * setup.fuelFrac;
    Controls c;
    // Плотность с учётом давления и температуры дня — для балансировки при старте в воздухе.
    auto rhoScale = [&](float altM) {
        float tIsa = 288.15f - 0.0065f * altM;
        return (s.env.qnh / 1013.25f) * tIsa / (tIsa + s.env.IsaDev());
    };
    // Высотомер: на земле и на заходе выставлено давление аэродрома (QNH), в полёте — стандартное 1013.
    c.baro = setup.start == StartKind::Air ? 1013.0f : roundf(s.env.qnh);
    switch (setup.start) {
    case StartKind::Runway: {
        Vector3 d = rw.Dir(setup.end);
        Vector3 p = Vector3Add(rw.Threshold(setup.end), Vector3Scale(d, rw.halfLen > 1000 ? 70.0f : 40.0f));
        c.flapsLever = 1;
        c.parkingBrake = true;
        c.trim = t.kind == AircraftKind::Prop ? 0.1f : 0.25f;
        s.aircraft.Reset({p.x, rw.center.y + t.restHeight, p.z}, rw.Course(setup.end), 0.0f, true, c, 0, 0, fuel);
        s.ap.Reset(rw.Course(setup.end), roundf((rw.center.y * M_TO_FT + 3000.0f) / 100.0f) * 100.0f, t.climbKt, true, false);
        break;
    }
    case StartKind::Final: {
        const bool main = world::GetAirport(setup.airport).main;
        const float glide = main ? 3.0f : 3.5f, tdz = main ? 300.0f : 150.0f;
        Vector3 d = rw.Dir(setup.end);
        float dist = setup.finalNm * 1852.0f;
        Vector3 p = Vector3Subtract(rw.Threshold(setup.end), Vector3Scale(d, dist));
        float y = rw.center.y + t.restHeight + (dist + tdz) * tanf(glide * DEG2RAD);
        bool gearFail = setup.fail == FailPlan::GearHydraulics;
        bool flapsJam = setup.fail == FailPlan::FlapsJam;
        c.flapsLever = flapsJam ? 0 : 2;
        c.gearDown = !gearFail;
        float v = (t.approachKt + (flapsJam ? 45.0f : 10.0f)) * KT_TO_MS * sqrtf(1.225f / (AirDensity(y) * rhoScale(y)));
        Aircraft::Trim tr = Aircraft::ComputeTrim(t, mass, v, y, t.flapDeg[c.flapsLever], c.gearDown, -glide, rhoScale(y));
        c.throttle = tr.throttle;
        c.trim = tr.trim;
        s.aircraft.Reset({p.x, y, p.z}, rw.Course(setup.end), v, false, c, tr.alphaDeg - glide, -glide, fuel);
        s.ap.Reset(rw.Course(setup.end), roundf(y * M_TO_FT / 100.0f) * 100.0f, t.approachKt + (flapsJam ? 45.0f : 10.0f), false, rw.ils);
        break;
    }
    case StartKind::Air: {
        Vector3 p = setup.airPos;
        float hdg = setup.airHdg;
        float yM = p.y * FT_TO_M;
        if (setup.goal == GoalKind::Gates && !s.run.Gates().empty()) {
            const MissionRun::Gate& g = s.run.Gates().front();
            p = Vector3Subtract(g.pos, Vector3Scale(g.dir, 3500.0f));
            yM = g.pos.y;
            hdg = WrapDeg360(atan2f(g.dir.x, -g.dir.z) * RAD2DEG);
        }
        yM = fmaxf(yM, s.terrain.SurfaceHeight(p.x, p.z) + 250.0f);
        c.flapsLever = 0;
        c.gearDown = false;
        float v = setup.airSpeedKt * KT_TO_MS * sqrtf(1.225f / (AirDensity(yM) * rhoScale(yM)));
        Aircraft::Trim tr = Aircraft::ComputeTrim(t, mass, v, yM, 0, !t.retractableGear, 0, rhoScale(yM));
        c.throttle = tr.throttle;
        c.trim = tr.trim;
        s.aircraft.Reset({p.x, yM, p.z}, hdg, v, false, c, tr.alphaDeg, 0, fuel);
        // Заданная высота — по показаниям высотомера (установлено 1013, а не давление района).
        s.ap.Reset(hdg, roundf((yM * M_TO_FT + (c.baro - s.env.qnh) * 27.3f) / 100.0f) * 100.0f, setup.airSpeedKt, false, false);
        break;
    }
    }
    if (!restart) s.ap.ClearRoute();
    s.trail.clear();
    s.trailTimer = 0;
    s.ctl = c;
    SyncEnvironment(s);
    for (float& b : s.aircraft.brakeTemp) b = s.aircraft.oat;
    s.warnings.Reset();
    s.callouts.Reset();
    s.scene.ResetCamera(s.aircraft);
    s.accumulator = 0;
    s.time = 0;
    s.crashAge = s.finishAge = -1;
    s.touchdownTimer = 0;
    s.gGrey = s.gRed = 0;
    s.paused = false;
    s.help = false;
    s.flying = true;
    s.mouseYoke = s.cfg.mouseYoke;
    s.menu.Open(MenuScreen::None);
    s.audio.StopAll();
    s.warnings.Update(s.aircraft, s.terrain, 0.0f);
}

// Фон меню: самолёт на перроне главного аэродрома в ясный день.
void ShowMenu(Sim& s, MenuScreen screen)
{
    FlightSetup bg;
    bg.aircraft = (AircraftKind)(s.cfg.freeAircraft % (int)AircraftKind::Count);
    StartFlight(s, bg);
    s.flying = false;
    s.menu.Open(screen);
    s.menuTime = 0;
}

float Axis(int pad, int axis, float dead = 0.08f)
{
    float v = GetGamepadAxisMovement(pad, axis);
    if (fabsf(v) < dead) return 0.0f;
    return (v - Sign(v) * dead) / (1.0f - dead);
}

// Клавиатура двигает «штурвал» с конечной скоростью и возвращает его в нейтраль.
float KeyStick(float current, float input, float dt)
{
    if (input != 0.0f) {
        float rate = (current * input < 0.0f) ? 4.0f : 1.6f;
        return MoveTowards(current, input, rate * dt);
    }
    return MoveTowards(current, 0.0f, 2.2f * dt);
}

void HandleFlightInput(Sim& s, float dt)
{
    Controls& c = s.ctl;
    Aircraft& a = s.aircraft;
    auto down = [](int k) { return IsKeyDown(k) ? 1.0f : 0.0f; };

    if (IsKeyPressed(KEY_F1)) s.help = !s.help;
    if (IsKeyPressed(KEY_R)) {
        StartFlight(s, s.setup);
        return;
    }
    if (IsKeyPressed(KEY_C) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP))
        s.cam = (CamMode)(((int)s.cam + 1) % (int)CamMode::Count);
    if (IsKeyPressed(KEY_F8)) s.wind = (s.wind + 1) % kWindCount;
    if (IsKeyPressed(KEY_M)) s.mouseYoke = !s.mouseYoke;

    // ---- штурвал и педали
    float kPitch = Clampf(down(KEY_S) + down(KEY_DOWN) - down(KEY_W) - down(KEY_UP), -1, 1);
    float kRoll = Clampf(down(KEY_D) + down(KEY_RIGHT) - down(KEY_A) - down(KEY_LEFT), -1, 1);
    float kYaw = down(KEY_E) - down(KEY_Q);

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

    // Любое заметное движение штурвала отключает автопилот (как усилие на штурвале).
    if (s.ap.apOn) {
        bool manual = kPitch != 0 || kRoll != 0 || (s.gamepad && (fabsf(gp) > 0.3f || fabsf(gr) > 0.3f));
        if (s.mouseYoke && !s.map.open && !s.gamepad) {
            Vector2 m = GetMousePosition();
            float range = GetScreenHeight() * 0.3f;
            manual = manual || fabsf(m.y - GetScreenHeight() * 0.4f) > range * 0.6f || fabsf(m.x - GetScreenWidth() * 0.5f) > range * 0.6f;
        }
        if (manual) s.ap.DisconnectAp("");
    }

    if (s.ap.apOn) {
        c.yaw = KeyStick(c.yaw, kYaw, dt);   // штурвалом и триммером управляет автопилот
    } else if (s.gamepad) {
        c.pitch = gp;
        c.roll = gr;
        c.yaw = gy;
    } else if (s.mouseYoke && !s.map.open) {
        Vector2 m = GetMousePosition();
        float range = GetScreenHeight() * 0.3f;
        c.pitch = Clampf((m.y - GetScreenHeight() * 0.4f) / range, -1, 1);
        c.roll = Clampf((m.x - GetScreenWidth() * 0.5f) / range, -1, 1);
        c.yaw = KeyStick(c.yaw, kYaw, dt);
    } else {
        c.pitch = KeyStick(c.pitch, kPitch, dt);
        c.roll = KeyStick(c.roll, kRoll, dt);
        c.yaw = KeyStick(c.yaw, kYaw, dt);
    }

    // ---- РУД и реверс (Ctrl на малом газе после касания)
    float thr = down(KEY_LEFT_SHIFT) + down(KEY_RIGHT_SHIFT) + down(KEY_PAGE_UP) - down(KEY_LEFT_CONTROL) - down(KEY_RIGHT_CONTROL) -
                down(KEY_PAGE_DOWN);
    if (padAvail) thr -= Axis(0, GAMEPAD_AXIS_RIGHT_Y) * 1.5f;
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    c.reverse = ctrl && c.throttle <= 0.001f && a.onGround && a.Type().hasReverser;
    if (s.ap.athrOn && (thr != 0 || IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_X))) s.ap.DisconnectAthr();
    c.throttle = Clampf(c.throttle + Clampf(thr, -1.5f, 1.5f) * 0.45f * dt, 0, 1);
    if (IsKeyPressed(KEY_Z)) c.throttle = 0.0f;
    if (IsKeyPressed(KEY_X)) {
        c.throttle = 1.0f;
        if (!a.onGround) s.ap.Toga(a);   // уход на второй круг
    }

    // ---- автопилот: режимы и задатчики (удержание клавиши — быстрее)
    float ra = s.warnings.radioAltFt;
    if (IsKeyPressed(KEY_T)) s.ap.ToggleAp(a, ra);
    if (IsKeyPressed(KEY_Y)) s.ap.ToggleAthr(a);
    if (IsKeyPressed(KEY_L)) s.ap.ToggleApp();
    if (IsKeyPressed(KEY_U)) s.ap.ToggleVs(a);
    if (IsKeyPressed(KEY_O)) s.ap.fdOn = !s.ap.fdOn;
    auto step = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
    if (step(KEY_NINE)) s.ap.hdgBug = WrapDeg360(s.ap.hdgBug - 1.0f);
    if (step(KEY_ZERO)) s.ap.hdgBug = WrapDeg360(s.ap.hdgBug + 1.0f);
    if (step(KEY_MINUS)) s.ap.altTarget = fmaxf(s.ap.altTarget - 100.0f, 0.0f);
    if (step(KEY_EQUAL)) s.ap.altTarget = fminf(s.ap.altTarget + 100.0f, 30000.0f);
    if (step(KEY_COMMA)) s.ap.spdTarget = fmaxf(s.ap.spdTarget - 5.0f, 50.0f);
    if (step(KEY_PERIOD)) s.ap.spdTarget = fminf(s.ap.spdTarget + 5.0f, 350.0f);
    if (step(KEY_SEMICOLON)) s.ap.vsTarget = fmaxf(s.ap.vsTarget - 100.0f, -4000.0f);
    if (step(KEY_APOSTROPHE)) s.ap.vsTarget = fminf(s.ap.vsTarget + 100.0f, 4000.0f);

    // ---- триммер
    float trim = down(KEY_RIGHT_BRACKET) + down(KEY_HOME) - down(KEY_LEFT_BRACKET) - down(KEY_END);
    if (padAvail)
        trim += (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP) ? 1.0f : 0.0f) - (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN) ? 1.0f : 0.0f);
    if (trim != 0 && s.ap.apOn) s.ap.DisconnectAp("");   // ручной триммер отключает AP
    c.trim = Clampf(c.trim + Clampf(trim, -1, 1) * 0.25f * dt, -1, 1);

    // ---- механизация, шасси, тормоза, аварийные действия
    if (IsKeyPressed(KEY_F) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) {
        if (c.flapsLever < 3) { ++c.flapsLever; s.audio.PlayClick(); }
    }
    if (IsKeyPressed(KEY_V) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) {
        if (c.flapsLever > 0) { --c.flapsLever; s.audio.PlayClick(); }
    }
    if ((IsKeyPressed(KEY_G) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) && a.Type().retractableGear) {
        c.gearDown = !c.gearDown;
        s.audio.PlayClick();
    }
    if (IsKeyPressed(KEY_H) && a.Type().retractableGear) {   // аварийный выпуск шасси
        c.altGear = true;
        c.gearDown = true;
        s.audio.PlayClick();
    }
    if (IsKeyPressed(KEY_J)) {   // пожарный кран: горящий двигатель, иначе — отказавший
        int target = -1;
        for (int i = 0; i < a.Type().engineCount; ++i)
            if (a.engines[i].fire) target = i;
        if (target < 0)
            for (int i = 0; i < a.Type().engineCount; ++i)
                if (a.engines[i].failed && !a.engines[i].shutdown) target = i;
        if (target >= 0) c.fireHandle[target] = true;
        s.audio.PlayClick();
    }
    if (IsKeyPressed(KEY_P)) { c.parkingBrake = !c.parkingBrake; s.audio.PlayClick(); }
    if (IsKeyPressed(KEY_I)) { c.antiIce = !c.antiIce; s.audio.PlayClick(); }   // противообледенительная система
    if (IsKeyPressed(KEY_N)) s.map.open = !s.map.open;
    if (IsKeyPressed(KEY_F2)) { s.ap.ToggleNav(a); s.audio.PlayClick(); }
    // Высотомер: F5/F6 — давление ±1 гПа, F7 — стандартное 1013 (STD) / давление района (QNH).
    if (step(KEY_F5)) c.baro = fmaxf(c.baro - 1.0f, 940.0f);
    if (step(KEY_F6)) c.baro = fminf(c.baro + 1.0f, 1060.0f);
    if (IsKeyPressed(KEY_F7)) c.baro = fabsf(c.baro - 1013.0f) < 0.5f ? roundf(s.env.qnh) : 1013.0f;
    if (IsKeyPressed(KEY_SLASH) || IsKeyPressed(KEY_K) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) {
        c.speedbrake = !c.speedbrake;
        s.audio.PlayClick();
    }
    bool brake = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_B) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    c.brakes = MoveTowards(c.brakes, brake ? 1.0f : 0.0f, 3.0f * dt);
}

void UpdateFlight(Sim& s, float dt)
{
    s.time += dt;
    s.accumulator += dt;
    bool wasCrashed = s.aircraft.crashed;
    bool touchdown = false;
    SyncEnvironment(s);
    while (s.accumulator >= PHYSICS_DT) {
        s.ap.Update(s.aircraft, s.warnings.radioAltFt, s.ctl, PHYSICS_DT);
        s.aircraft.Step(PHYSICS_DT, s.ctl, s.terrain, WindAt(s, s.aircraft.pos));
        s.accumulator -= PHYSICS_DT;
        if (s.aircraft.touchdown) {
            s.aircraft.touchdown = false;
            touchdown = true;
            s.run.OnTouchdown(s.aircraft);
        }
    }
    if (touchdown) {
        s.touchdownFpm = s.aircraft.touchdownFpm;
        int ap = world::NearestAirport(s.aircraft.pos.x, s.aircraft.pos.z);
        s.touchdownCl = fabsf(world::GetAirport(ap).rwy.Local(s.aircraft.pos.x, s.aircraft.pos.z).y);
        s.touchdownTimer = 6.0f;
        s.scene.Kick(Clampf(fabsf(s.touchdownFpm) / 700.0f, 0.15f, 1.0f));
        s.audio.PlayTouchdown(fabsf(s.touchdownFpm) / 600.0f, sqrtf(s.aircraft.vel.x * s.aircraft.vel.x + s.aircraft.vel.z * s.aircraft.vel.z));
    }
    if (s.aircraft.crashed && !wasCrashed) {
        s.crashAge = 0;
        s.audio.PlayCrash();
    }
    if (s.crashAge >= 0) s.crashAge += dt;
    {   // Перегрузка: при +4…6.5 G кровь отливает от глаз — сужается поле зрения, потом темнеет;
        // при −1.5…−3 G — «красная пелена». Нужно несколько секунд, чтобы наступило, и чуть быстрее проходит.
        float g = s.aircraft.crashed ? 1.0f : s.aircraft.gLoad;
        float tg = SmoothStep(4.0f, 6.5f, g), tr = SmoothStep(1.5f, 3.0f, -g);
        s.gGrey = MoveTowards(s.gGrey, tg, (tg > s.gGrey ? 0.45f : 0.7f) * dt);
        s.gRed = MoveTowards(s.gRed, tr, (tr > s.gRed ? 0.6f : 0.8f) * dt);
    }
    s.touchdownTimer = fmaxf(s.touchdownTimer - dt, 0.0f);
    s.trailTimer -= dt;
    if (s.trailTimer <= 0.0f && !s.aircraft.crashed && Vector3Length(s.aircraft.vel) > 2.0f) {
        s.trailTimer = 2.0f;
        if (s.trail.size() > 4000) s.trail.erase(s.trail.begin(), s.trail.begin() + 1000);
        s.trail.push_back({s.aircraft.pos.x, s.aircraft.pos.z});
    }
    s.warnings.Update(s.aircraft, s.terrain, dt);
    s.callouts.Update(s.aircraft, s.warnings.radioAltFt, s.ctl, dt);
    s.fx.Update(s.aircraft, s.env, WindAt(s, s.aircraft.pos), dt, touchdown);
    s.run.Update(s.aircraft, s.warnings, s.terrain, dt);

    // Итоги задания — через пару секунд после завершения.
    if (s.run.Finished() && s.finishAge < 0) {
        s.finishAge = 0;
        if (s.run.Success() && s.setup.mission >= 0) {
            const std::string id = MissionList()[s.setup.mission].id;
            int stars = s.run.Stars();
            if (stars > s.cfg.Stars(id)) {
                s.cfg.stars[id] = stars;
                s.cfg.Save();
            }
        }
    }
    if (s.finishAge >= 0) {
        s.finishAge += dt;
        if (s.finishAge > (s.aircraft.crashed ? 3.0f : 1.5f) && s.menu.Screen() == MenuScreen::None) {
            s.menu.Open(MenuScreen::Results);
            s.menuTime = 0;
        }
    }
    // Авария в свободном полёте.
    if (!s.run.Active() && s.crashAge > 3.0f && s.menu.Screen() == MenuScreen::None) s.menu.Open(MenuScreen::Crash);
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
    s.cfg.Load();
    ui::ru = s.cfg.ru;
    ui::LoadFonts();
    s.audio.Init();
    SetMasterVolume(s.cfg.volume / 100.0f);
    s.scene.Init();
    s.fx.Init();
    s.terrain.Generate(1337u);
    s.terrain.BuildMeshes(s.scene.LitShader());
    s.scene.BuildWorld(s.terrain);
    s.map.Build(s.terrain);
    ShowMenu(s, MenuScreen::Main);

    while (!WindowShouldClose() && !s.quit) {
        float dt = fminf(GetFrameTime(), 0.1f);
        s.menuTime += dt;

        bool menuOpen = s.menu.Screen() != MenuScreen::None;
        if (s.flying && !menuOpen) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                if (s.map.open) s.map.open = false;
                else if (s.help) s.help = false;
                else {
                    s.paused = true;
                    s.menu.Open(MenuScreen::Pause);
                }
            } else {
                HandleFlightInput(s, dt);
            }
        }
        if (s.flying && !s.paused && s.menu.Screen() == MenuScreen::None) UpdateFlight(s, dt);
        else if (s.flying && s.menu.Screen() == MenuScreen::Results) s.fx.Update(s.aircraft, s.env, {}, dt, false);

        bool sounding = s.flying && !s.paused && s.menu.Screen() != MenuScreen::Results;
        if (s.ap.apOffSound) {
            s.ap.apOffSound = false;
            if (sounding) s.audio.PlayApDisconnect();
        }
        s.audio.Update(s.aircraft, s.ctl, s.warnings, s.callouts, !sounding);
        s.scene.crashAge = s.crashAge;
        s.scene.mouseLocked = s.map.open;
        if (s.flying && s.map.open && s.menu.Screen() == MenuScreen::None) s.map.Update(s.ap, s.aircraft);
        if (s.flying) s.scene.UpdateCamera(s.aircraft, s.cam, dt);
        else s.scene.MenuCamera(s.aircraft.pos, s.menuTime);

        BeginDrawing();
        ClearBackground(s.env.horizon);
        s.scene.Draw(s.aircraft, s.terrain, s.flying ? s.cam : CamMode::Orbit, s.time + s.menuTime, WindAt(s, s.aircraft.pos), s.fx);
        if (s.flying && s.run.Setup().goal == GoalKind::Gates) {
            BeginMode3D(s.scene.camera);
            s.run.DrawGates();
            EndMode3D();
        }

        if (s.flying) {
            bool below = !s.env.overcast || s.scene.camera.position.y < s.env.cloudBase;
            if (s.cam == CamMode::Cockpit) s.fx.DrawWindshield(s.env, s.aircraft.ias, below);
            HudInfo info;
            info.cam = s.cam;
            info.mouseYoke = s.mouseYoke;
            info.gamepad = s.gamepad;
            info.paused = s.paused;
            info.showHelp = s.help;
            info.windName = WindName(s.wind);
            info.time = s.time;
            info.lastTouchdownFpm = s.touchdownFpm;
            info.touchdownMsgTimer = s.touchdownTimer;
            info.touchdownCenterline = s.touchdownCl;
            info.ap = &s.ap;
            info.callouts = &s.callouts;
            std::string title;
            if (s.run.Active()) {
                title = s.run.Title();
                info.missionTitle = title.c_str();
                info.missionLines = s.run.HudLines(s.aircraft);
            }
            DrawHud(s.aircraft, s.ctl, s.warnings, s.terrain, s.scene.camera, info);
            // Снаружи (вид сзади и т. п.) эффекты слабее: это ощущения пилота.
            float k = s.cam == CamMode::Cockpit ? 1.0f : 0.5f;
            float frost = (s.cam == CamMode::Cockpit && s.aircraft.Type().kind == AircraftKind::Prop) ? s.aircraft.ice : 0.0f;
            DrawVisionEffects(s.gGrey * k, s.gRed * k, frost);
            s.map.Draw(s.aircraft, s.ap, WindAt(s, s.aircraft.pos), s.trail);
        }

        MenuAction act = s.menu.Update(s.cfg, s.run, s.aircraft, s.menuTime);
        switch (act.kind) {
        case MenuAction::StartMission:
            s.menu.selectedMission = act.index;
            StartFlight(s, MissionList()[act.index].setup);
            break;
        case MenuAction::StartFree:
            s.cfg.Save();
            StartFlight(s, FreeSetup(s.cfg));
            break;
        case MenuAction::Resume:
            s.paused = false;
            s.menu.Open(MenuScreen::None);
            break;
        case MenuAction::Restart: StartFlight(s, s.setup); break;
        case MenuAction::ToggleHelp:
            s.help = !s.help;
            s.paused = false;
            s.menu.Open(MenuScreen::None);
            break;
        case MenuAction::ToMain: ShowMenu(s, act.index == 1 ? MenuScreen::Missions : MenuScreen::Main); break;
        case MenuAction::Quit: s.quit = true; break;
        case MenuAction::SettingsChanged: SetMasterVolume(s.cfg.volume / 100.0f); break;
        default: break;
        }
        if (s.cfg.showFps) DrawFPS(GetScreenWidth() - 90, 6);
        EndDrawing();
    }

    s.cfg.Save();
    s.terrain.Unload();
    s.map.Unload();
    s.fx.Unload();
    s.scene.Unload();
    s.audio.Shutdown();
    ui::UnloadFonts();
    CloseWindow();
    return 0;
}
