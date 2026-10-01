#pragma once
#include "Aircraft.h"
#include "Environment.h"

#include <string>
#include <vector>

class Terrain;
class WarningSystem;

enum class StartKind { Runway, Final, Air };
enum class GoalKind { None, Takeoff, Land, Gates };
enum class FailPlan { None, Random, EngineAtV1, FireAfterTakeoff, GearHydraulics, FlapsJam, LowFuel };

// Всё, что нужно, чтобы начать полёт (и задание, и свободный полёт).
struct FlightSetup {
    AircraftKind aircraft = AircraftKind::LightJet;
    TimeOfDay time = TimeOfDay::Day;
    WeatherKind weather = WeatherKind::Clear;
    int wind = 0;
    StartKind start = StartKind::Runway;
    int airport = 0, end = 0;          // аэродром и торец ВПП старта/захода
    float finalNm = 6;                 // удаление при старте на заходе
    Vector3 airPos{};                  // старт в воздухе (y — высота, ft)
    float airHdg = 90, airSpeedKt = 200;
    float fuelFrac = -1;               // доля от полной заправки (-1 — обычная)
    FailPlan fail = FailPlan::None;
    GoalKind goal = GoalKind::None;
    int targetAirport = 0;
    float targetAltFt = 3000;
    float timeLimit = 0;               // для третьей звезды, с
    int mission = -1;                  // номер задания (-1 — свободный полёт)
};

struct MissionDef {
    const char* id;
    const char* titleEn;
    const char* titleRu;
    const char* descEn;
    const char* descRu;
    int difficulty;   // 1..3
    FlightSetup setup;
};

const std::vector<MissionDef>& MissionList();

// Ход выполнения задания: плановые отказы, проверка цели, звёзды.
class MissionRun {
public:
    struct Gate { Vector3 pos; Vector3 dir; float halfW, halfH; int state; };   // state: 0 впереди, 1 пройдены, -1 пропущены

    void Start(const FlightSetup& s, const Terrain& t);
    void Update(Aircraft& a, const WarningSystem& w, const Terrain& t, float dt);
    void OnTouchdown(const Aircraft& a);
    void DrawGates() const;   // внутри BeginMode3D

    bool Active() const { return setup_.goal != GoalKind::None; }
    bool Finished() const { return finished_; }
    bool Success() const { return success_; }
    int Stars() const;
    const FlightSetup& Setup() const { return setup_; }
    const std::vector<Gate>& Gates() const { return gates_; }

    std::string Title() const;
    std::vector<std::string> HudLines(const Aircraft& a) const;

    struct Criterion { std::string text; bool met; };
    std::vector<Criterion> criteria;
    std::string resultText;
    float time = 0;
    // для итогов
    float tdFpm = 0, tdCenterline = 0, tdDistance = 0;
    bool touched = false;

    // Отрезок, по которому строится трасса ворот (для старта задания «Горная трасса»).
    static std::vector<Vector3> GatePath();

private:
    void Finish(bool ok, const std::string& text);
    void EvaluateLanding(const Aircraft& a);

    FlightSetup setup_;
    bool finished_ = false, success_ = false;
    bool airborne_ = false;
    bool failTriggered_ = false;
    float randomFailAt_ = 0;
    float stoppedTime_ = 0;
    bool warnSeen_ = false;
    std::vector<Gate> gates_;
    int nextGate_ = 0, gatesPassed_ = 0;
    float prevSide_ = 0;
};
