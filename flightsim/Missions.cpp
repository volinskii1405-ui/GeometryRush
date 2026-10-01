#include "Missions.h"
#include "Terrain.h"
#include "UiText.h"
#include "Warnings.h"
#include "World.h"

#include "rlgl.h"

using namespace fs;
using ui::L;

namespace {

FlightSetup Make(AircraftKind ac, StartKind start, GoalKind goal)
{
    FlightSetup s;
    s.aircraft = ac;
    s.start = start;
    s.goal = goal;
    return s;
}

std::vector<MissionDef> BuildMissions()
{
    std::vector<MissionDef> m;
    auto add = [&](const char* id, const char* te, const char* tr, const char* de, const char* dr, int diff, FlightSetup s) {
        s.mission = (int)m.size();
        m.push_back({id, te, tr, de, dr, diff, s});
    };

    FlightSetup s = Make(AircraftKind::LightJet, StartKind::Runway, GoalKind::Takeoff);
    s.targetAltFt = 3000;
    s.timeLimit = 150;
    add("first_takeoff", "First takeoff", "Первый взлёт",
        "Take off from runway 09 and climb to 3000 ft. Release the parking brake (P), full power (X), rotate at 110 kt, gear up (G).",
        "Взлетите с полосы 09 и наберите 3000 ft. Снимите стояночный тормоз (P), полный газ (X), отрыв на 110 kt, уберите шасси (G).",
        1, s);

    s = Make(AircraftKind::Prop, StartKind::Runway, GoalKind::Land);
    s.timeLimit = 600;
    add("circuit_c172", "Circuit in a Cessna", "Круг на «Сессне»",
        "Take off, fly a circuit around the airport and land back on the same runway. Approach at 65 kt with full flaps.",
        "Взлетите, сделайте круг над аэродромом и сядьте на ту же полосу. Заход на 65 kt с полными закрылками.",
        1, s);

    s = Make(AircraftKind::LightJet, StartKind::Final, GoalKind::Land);
    s.finalNm = 6;
    add("ils_approach", "ILS approach", "Заход по ILS",
        "6 nm final to runway 09. Follow the ILS diamonds or the flight director, flaps 35, ~120 kt, flare at 30 ft.",
        "6 миль до полосы 09. Держитесь ромбов ILS или директора, закрылки 35, ~120 kt, выравнивание на 30 ft.",
        1, s);

    s = Make(AircraftKind::LightJet, StartKind::Final, GoalKind::Land);
    s.finalNm = 6;
    s.wind = 2;
    add("crosswind", "Crosswind landing", "Посадка с боковым ветром",
        "Wind from the north, 15 knots gusting 25. Crab into the wind on final and straighten with rudder just before touchdown.",
        "Ветер с севера 15 узлов, порывы до 25. На заходе доверните на ветер, перед касанием выровняйте нос педалями.",
        2, s);

    s = Make(AircraftKind::LightJet, StartKind::Final, GoalKind::Land);
    s.finalNm = 8;
    s.time = TimeOfDay::Night;
    add("night_landing", "Night landing", "Ночная посадка",
        "Night approach. Use the approach lights, PAPI and the ILS. Landing lights come on with the gear.",
        "Ночной заход. Помогут огни приближения, PAPI и ILS. Посадочные фары включаются вместе с шасси.",
        2, s);

    s = Make(AircraftKind::LightJet, StartKind::Final, GoalKind::Land);
    s.finalNm = 8;
    s.weather = WeatherKind::Fog;
    add("fog", "Fog: instrument approach", "Туман: заход по приборам",
        "Visibility 800 m, cloud base 250 ft. Fly the ILS on instruments, the runway appears just before minimums.",
        "Видимость 800 м, нижний край 250 ft. Заход по приборам по ILS, полоса появится перед минимумом.",
        3, s);

    s = Make(AircraftKind::LightJet, StartKind::Runway, GoalKind::Land);
    s.fail = FailPlan::EngineAtV1;
    add("engine_v1", "Engine failure at V1", "Отказ двигателя на взлёте",
        "An engine fails during the takeoff run. Keep straight with rudder, continue the takeoff, climb, come back and land.",
        "Двигатель откажет на разбеге. Держите направление педалями, продолжайте взлёт, наберите высоту, вернитесь и сядьте.",
        3, s);

    s = Make(AircraftKind::Airliner, StartKind::Runway, GoalKind::Land);
    s.fail = FailPlan::FireAfterTakeoff;
    add("engine_fire", "Engine fire", "Пожар двигателя",
        "Airliner. After takeoff an engine catches fire: pull the fire handle (J), return and land with one engine.",
        "Лайнер. После взлёта загорится двигатель: пожарный кран (J), возвращайтесь и садитесь на одном двигателе.",
        3, s);

    s = Make(AircraftKind::LightJet, StartKind::Final, GoalKind::Land);
    s.finalNm = 9;
    s.fail = FailPlan::GearHydraulics;
    add("gear_failure", "Gear won't extend", "Шасси не выпускается",
        "Hydraulic failure: the gear lever does nothing. Use the alternate extension (H) - it takes 15 seconds - then land.",
        "Отказ гидросистемы: рычаг шасси не работает. Аварийный выпуск (H) занимает 15 секунд, потом садитесь.",
        2, s);

    s = Make(AircraftKind::LightJet, StartKind::Air, GoalKind::Gates);
    s.timeLimit = 140;
    s.airSpeedKt = 220;
    add("mountain_gates", "Mountain course", "Горная трасса",
        "Fly through 10 gates low over the hills between the town and the mountain strip. The next gate is green.",
        "Пролетите 10 ворот низко над холмами между городом и горным аэродромом. Следующие ворота — зелёные.",
        2, s);

    s = Make(AircraftKind::Prop, StartKind::Final, GoalKind::Land);
    s.airport = 1;
    s.finalNm = 3;
    s.targetAirport = 1;
    add("mountain_strip", "Mountain strip", "Горный аэродром",
        "Land the Cessna on the short strip on a cliff (1200 m, no ILS). Steeper 3.5 deg approach, PAPI on the left.",
        "Посадите «Сессну» на короткую полосу на краю обрыва (1200 м, без ILS). Глиссада круче — 3.5°, PAPI слева.",
        2, s);

    s = Make(AircraftKind::LightJet, StartKind::Air, GoalKind::Land);
    s.airPos = {-15000, 6000, 0};
    s.airHdg = 90;
    s.airSpeedKt = 210;
    s.fuelFrac = 0.006f;
    s.fail = FailPlan::LowFuel;
    add("deadstick", "Out of fuel", "Без топлива",
        "Fuel is almost gone, 15 km from the airport at 6000 ft. The engines will quit - glide to runway 09.",
        "Топливо на исходе, 15 км до аэропорта, высота 6000 ft. Двигатели остановятся — планируйте к полосе 09.",
        3, s);

    s = Make(AircraftKind::Airliner, StartKind::Final, GoalKind::Land);
    s.finalNm = 9;
    s.time = TimeOfDay::Sunset;
    s.weather = WeatherKind::Rain;
    add("airliner_rain", "Airliner in the rain", "Лайнер под дождём",
        "Heavy airliner, sunset, rain and low clouds. ILS to runway 09, full flaps ~140 kt, reverse after touchdown (Ctrl).",
        "Тяжёлый лайнер, закат, дождь и низкие облака. ILS на полосу 09, полные закрылки ~140 kt, после касания реверс (Ctrl).",
        2, s);

    s = Make(AircraftKind::Airliner, StartKind::Final, GoalKind::Land);
    s.finalNm = 10;
    s.fail = FailPlan::FlapsJam;
    add("flapless", "Flapless landing", "Посадка без закрылков",
        "The flaps are jammed up. Approach much faster (~175 kt), touch down early and stop with reverse and brakes.",
        "Закрылки заклинило в убранном положении. Заход на большой скорости (~175 kt), садитесь в начале полосы, реверс и тормоза.",
        3, s);
    return m;
}

} // namespace

const std::vector<MissionDef>& MissionList()
{
    static const std::vector<MissionDef> list = BuildMissions();
    return list;
}

std::vector<Vector3> MissionRun::GatePath()
{
    return {{1900, 0, -1900}, {3400, 0, -2900}, {5400, 0, -4300}, {7000, 0, -5600}, {8300, 0, -7000}};
}

void MissionRun::Start(const FlightSetup& s, const Terrain& t)
{
    setup_ = s;
    criteria.clear();
    resultText.clear();
    time = 0;
    finished_ = success_ = false;
    airborne_ = s.start != StartKind::Runway;
    failTriggered_ = false;
    stoppedTime_ = 0;
    warnSeen_ = false;
    touched = false;
    tdFpm = tdCenterline = tdDistance = 0;
    randomFailAt_ = (float)GetRandomValue(90, 480);
    gates_.clear();
    nextGate_ = gatesPassed_ = 0;
    prevSide_ = 0;
    if (s.goal == GoalKind::Gates) {
        // 10 ворот вдоль трассы, низко над рельефом.
        std::vector<Vector3> path = GatePath();
        float total = 0;
        for (size_t i = 0; i + 1 < path.size(); ++i) total += Vector3Distance(path[i], path[i + 1]);
        const int n = 10;
        for (int g = 0; g < n; ++g) {
            float d = total * (g + 0.5f) / n;
            size_t i = 0;
            while (i + 2 < path.size() && d > Vector3Distance(path[i], path[i + 1])) {
                d -= Vector3Distance(path[i], path[i + 1]);
                ++i;
            }
            Vector3 dir = Vector3Normalize(Vector3Subtract(path[i + 1], path[i]));
            Vector3 p = Vector3Add(path[i], Vector3Scale(dir, d));
            float ground = 0;
            for (float ox = -120; ox <= 120; ox += 60)
                for (float oz = -120; oz <= 120; oz += 60) ground = fmaxf(ground, t.SurfaceHeight(p.x + ox, p.z + oz));
            p.y = ground + 85.0f;
            gates_.push_back({p, dir, 75.0f, 45.0f, 0});
        }
    }
}

void MissionRun::Finish(bool ok, const std::string& text)
{
    if (finished_) return;
    finished_ = true;
    success_ = ok;
    resultText = text;
}

int MissionRun::Stars() const
{
    if (!success_) return 0;
    int n = 0;
    for (const Criterion& c : criteria)
        if (c.met) ++n;
    return n;
}

void MissionRun::OnTouchdown(const Aircraft& a)
{
    if (!airborne_ || touched) return;
    touched = true;
    tdFpm = a.touchdownFpm;
    const Runway& rw = world::GetAirport(setup_.targetAirport).rwy;
    int end = (a.vel.x * rw.Dir(0).x + a.vel.z * rw.Dir(0).z) >= 0 ? 0 : 1;
    Vector3 th = rw.Threshold(end), d = rw.Dir(end);
    tdDistance = (a.pos.x - th.x) * d.x + (a.pos.z - th.z) * d.z;
    tdCenterline = fabsf(rw.Local(a.pos.x, a.pos.z).y);
}

void MissionRun::EvaluateLanding(const Aircraft& a)
{
    const Runway& rw = world::GetAirport(setup_.targetAirport).rwy;
    bool onRunway = rw.Contains(a.pos.x, a.pos.z, 5.0f);
    criteria.clear();
    criteria.push_back({L("Landed and stopped on the runway", "Посадка и остановка на полосе"), onRunway});
    criteria.push_back({TextFormat(L("Touchdown softer than 300 fpm (%.0f)", "Касание мягче 300 fpm (%.0f)"), fabsf(tdFpm)),
                        fabsf(tdFpm) <= 300.0f});
    float tdzFar = rw.halfLen > 1000 ? 750.0f : 400.0f, tdzNear = rw.halfLen > 1000 ? 150.0f : 50.0f;
    bool tdz = tdDistance >= tdzNear && tdDistance <= tdzFar && tdCenterline <= 5.0f;
    criteria.push_back({TextFormat(L("Touchdown zone, on centerline (%.0f m, %.1f m off)", "В зоне приземления по оси (%.0f м, %.1f м от оси)"),
                                   tdDistance, tdCenterline),
                        tdz});
    if (onRunway) Finish(true, L("Landed!", "Посадка выполнена!"));
    else Finish(false, L("Stopped outside the runway", "Самолёт остановился за пределами полосы"));
}

void MissionRun::Update(Aircraft& a, const WarningSystem& w, const Terrain& t, float dt)
{
    if (finished_) return;
    time += dt;
    const AircraftType& ty = a.Type();
    float iasKt = a.ias * MS_TO_KT;
    if (!a.onGround && w.radioAltFt > 50.0f) airborne_ = true;

    // ---- плановые отказы
    if (!failTriggered_) {
        switch (setup_.fail) {
        case FailPlan::EngineAtV1:
            if (a.onGround && iasKt > ty.rotateKt - 12.0f) { a.Fail(Failure::Engine2); failTriggered_ = true; }
            break;
        case FailPlan::FireAfterTakeoff:
            if (airborne_ && w.radioAltFt > 1200.0f) { a.Fail(Failure::Fire1); failTriggered_ = true; }
            break;
        case FailPlan::GearHydraulics: a.Fail(Failure::GearHydraulics); failTriggered_ = true; break;
        case FailPlan::FlapsJam: a.Fail(Failure::FlapsJam); failTriggered_ = true; break;
        case FailPlan::Random:
            if (time > randomFailAt_ && airborne_) {
                int k = GetRandomValue(0, 3);
                Failure f = k == 0 ? Failure::Engine2 : k == 1 ? Failure::Fire1 : k == 2 ? Failure::FlapsJam : Failure::FuelLeak;
                if (a.gear < 0.01f && ty.retractableGear && GetRandomValue(0, 2) == 0) f = Failure::GearHydraulics;
                a.Fail(f);
                failTriggered_ = true;
            }
            break;
        default: break;
        }
    }
    if (setup_.goal == GoalKind::None) return;

    if (a.crashed) {
        criteria.clear();
        Finish(false, a.crashReason);
        return;
    }
    if (w.Active(Alert::Stall) || w.Active(Alert::Overspeed) || w.Active(Alert::BankAngle) || a.tailStrike) warnSeen_ = true;

    switch (setup_.goal) {
    case GoalKind::Takeoff:
        if (a.pos.y * M_TO_FT >= setup_.targetAltFt) {
            bool clean = a.gear < 0.01f && a.flaps < 0.5f;
            criteria = {{TextFormat(L("Climbed to %.0f ft", "Набрана высота %.0f ft"), setup_.targetAltFt), true},
                        {L("No stall, overspeed, bank warnings or tail strike", "Без сваливания, превышения скорости, крена и удара хвостом"), !warnSeen_},
                        {TextFormat(L("Gear and flaps up, under %.0f s (%.0f s)", "Шасси и закрылки убраны, быстрее %.0f с (%.0f с)"),
                                    setup_.timeLimit, time),
                         clean && time <= setup_.timeLimit}};
            Finish(true, L("Takeoff complete!", "Взлёт выполнен!"));
        }
        break;
    case GoalKind::Land: {
        // Ушли на второй круг после касания — касание не засчитываем.
        if (touched && !a.onGround && w.radioAltFt > 100.0f) touched = false;
        float gs = sqrtf(a.vel.x * a.vel.x + a.vel.z * a.vel.z);
        if (touched && a.onGround && gs < 1.5f) stoppedTime_ += dt;
        else stoppedTime_ = 0;
        if (stoppedTime_ > 1.5f) EvaluateLanding(a);
        break;
    }
    case GoalKind::Gates: {
        if (nextGate_ < (int)gates_.size()) {
            Gate& g = gates_[nextGate_];
            Vector3 rel = Vector3Subtract(a.pos, g.pos);
            float side = Vector3DotProduct(rel, g.dir);
            if (prevSide_ < 0 && side >= 0) {
                Vector3 right{-g.dir.z, 0, g.dir.x};
                bool inside = fabsf(Vector3DotProduct(rel, right)) <= g.halfW && fabsf(rel.y) <= g.halfH;
                g.state = inside ? 1 : -1;
                if (inside) ++gatesPassed_;
                ++nextGate_;
                prevSide_ = nextGate_ < (int)gates_.size() ? Vector3DotProduct(Vector3Subtract(a.pos, gates_[nextGate_].pos), gates_[nextGate_].dir) : 0;
            } else {
                prevSide_ = side;
            }
        }
        if (nextGate_ >= (int)gates_.size()) {
            int n = (int)gates_.size();
            criteria = {{TextFormat(L("Passed at least %d of %d gates (%d)", "Пройдено не меньше %d из %d ворот (%d)"), n - 2, n, gatesPassed_),
                         gatesPassed_ >= n - 2},
                        {L("All gates", "Все ворота"), gatesPassed_ == n},
                        {TextFormat(L("Faster than %.0f s (%.0f s)", "Быстрее %.0f с (%.0f с)"), setup_.timeLimit, time), time <= setup_.timeLimit}};
            if (gatesPassed_ >= n - 2) Finish(true, L("Course complete!", "Трасса пройдена!"));
            else Finish(false, L("Too many gates missed", "Слишком много пропущенных ворот"));
        }
        break;
    }
    default: break;
    }
    (void)t;
}

std::string MissionRun::Title() const
{
    if (setup_.mission < 0) return "";
    const MissionDef& m = MissionList()[setup_.mission];
    return L(m.titleEn, m.titleRu);
}

std::vector<std::string> MissionRun::HudLines(const Aircraft& a) const
{
    std::vector<std::string> v;
    switch (setup_.goal) {
    case GoalKind::Takeoff:
        v.push_back(TextFormat(L("Climb to %.0f ft   (now %.0f)", "Наберите %.0f ft   (сейчас %.0f)"), setup_.targetAltFt, a.pos.y * M_TO_FT));
        v.push_back(TextFormat(L("Time %.0f s", "Время %.0f с"), time));
        break;
    case GoalKind::Land: {
        const Airport& ap = world::GetAirport(setup_.targetAirport);
        Vector3 d{ap.rwy.center.x - a.pos.x, 0, ap.rwy.center.z - a.pos.z};
        float dist = sqrtf(d.x * d.x + d.z * d.z) / 1000.0f;
        float brg = WrapDeg360(atan2f(d.x, -d.z) * RAD2DEG);
        v.push_back(TextFormat(L("Land at %s", "Посадка: %s"), L(ap.nameEn, ap.nameRu)));
        v.push_back(TextFormat(L("Runway %s/%s: bearing %03.0f, %.1f km", "ВПП %s/%s: пеленг %03.0f, %.1f км"), ap.rwy.ident[0],
                               ap.rwy.ident[1], brg, dist));
        break;
    }
    case GoalKind::Gates:
        if (nextGate_ < (int)gates_.size()) {
            Vector3 d = Vector3Subtract(gates_[nextGate_].pos, a.pos);
            float brg = WrapDeg360(atan2f(d.x, -d.z) * RAD2DEG);
            v.push_back(TextFormat(L("Gate %d of %d: bearing %03.0f, %.1f km, %.0f ft", "Ворота %d из %d: пеленг %03.0f, %.1f км, %.0f ft"),
                                   nextGate_ + 1, (int)gates_.size(), brg, sqrtf(d.x * d.x + d.z * d.z) / 1000.0f,
                                   gates_[nextGate_].pos.y * M_TO_FT));
        }
        v.push_back(TextFormat(L("Passed %d   time %.0f s", "Пройдено %d   время %.0f с"), gatesPassed_, time));
        break;
    default: break;
    }
    return v;
}

void MissionRun::DrawGates() const
{
    for (int i = 0; i < (int)gates_.size(); ++i) {
        const Gate& g = gates_[i];
        Color c = g.state == 1 ? Color{120, 120, 120, 255} : g.state == -1 ? Color{200, 40, 40, 255}
                : i == nextGate_ ? Color{60, 255, 90, 255} : Color{255, 140, 30, 255};
        Vector3 right{-g.dir.z, 0, g.dir.x};
        float th = 4.0f;
        Matrix rot = MatrixRotateY(atan2f(-g.dir.z, g.dir.x));   // локальная X — по направлению пролёта
        auto bar = [&](Vector3 center, Vector3 size) {
            rlPushMatrix();
            rlTranslatef(center.x, center.y, center.z);
            rlMultMatrixf(MatrixToFloat(rot));
            DrawCube({0, 0, 0}, size.x, size.y, size.z, c);
            rlPopMatrix();
        };
        bar(Vector3Add(g.pos, {0, g.halfH, 0}), {th, th, g.halfW * 2 + th});
        bar(Vector3Add(g.pos, {0, -g.halfH, 0}), {th, th, g.halfW * 2 + th});
        bar(Vector3Add(g.pos, Vector3Scale(right, g.halfW)), {th, g.halfH * 2, th});
        bar(Vector3Add(g.pos, Vector3Scale(right, -g.halfW)), {th, g.halfH * 2, th});
    }
}
