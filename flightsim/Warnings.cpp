#include "Warnings.h"
#include "Aircraft.h"
#include "Terrain.h"
#include "World.h"

using namespace fs;

void WarningSystem::Reset()
{
    for (int i = 0; i < N; ++i) {
        active_[i] = false;
        hold_[i] = 0;
    }
    radioAltFt = 0;
    timeToImpact = -1;
}

const char* WarningSystem::Text(Alert a)
{
    switch (a) {
    case Alert::PullUp: return "PULL UP! TERRAIN";
    case Alert::Terrain: return "TERRAIN";
    case Alert::SinkRate: return "SINK RATE";
    case Alert::TooLowGear: return "TOO LOW - GEAR";
    case Alert::BankAngle: return "BANK ANGLE";
    case Alert::Overspeed: return "OVERSPEED";
    case Alert::Stall: return "STALL";
    case Alert::EngFire: return "ENGINE FIRE";
    case Alert::EngFail: return "ENGINE FAIL";
    case Alert::GearUnsafe: return "GEAR UNSAFE";
    case Alert::FlapsJam: return "FLAPS JAMMED";
    case Alert::FuelLow: return "FUEL LOW";
    case Alert::Structure: return "STRUCTURAL FAILURE";
    case Alert::Overstress: return "OVERSTRESS";
    case Alert::Ice: return "ICE DETECTED";
    case Alert::BrakesHot: return "BRAKES HOT";
    default: return "";
    }
}

bool WarningSystem::IsWarning(Alert a)
{
    return a == Alert::PullUp || a == Alert::Overspeed || a == Alert::Stall || a == Alert::EngFire || a == Alert::Structure;
}

bool WarningSystem::AnyWarning() const
{
    for (int i = 0; i < N; ++i)
        if (active_[i] && IsWarning((Alert)i)) return true;
    return false;
}

bool WarningSystem::AnyCaution() const
{
    for (int i = 0; i < N; ++i)
        if (active_[i] && !IsWarning((Alert)i)) return true;
    return false;
}

Alert WarningSystem::TopVoice() const
{
    const Alert order[] = {Alert::PullUp, Alert::Terrain, Alert::SinkRate, Alert::TooLowGear, Alert::BankAngle};
    for (Alert a : order)
        if (active_[(int)a]) return a;
    return Alert::Count;
}

void WarningSystem::Update(const Aircraft& a, const Terrain& t, float dt)
{
    bool cond[N] = {};

    const float gearHeight = a.Type().restHeight;   // высота ЦМ над грунтом на стоянке
    float ra = (a.pos.y - gearHeight - t.SurfaceHeight(a.pos.x, a.pos.z)) * M_TO_FT;
    radioAltFt = fmaxf(ra, 0.0f);
    const float descentFpm = -a.vel.y * MS_TO_FPM;
    const float iasKt = a.ias * MS_TO_KT;
    const bool airborne = !a.onGround && radioAltFt > 30.0f;
    const bool landingConfig = a.gear > 0.99f && a.flaps >= a.Type().flapDeg[2] - 1.0f;

    // ---- TAWS: прогноз траектории по текущему вектору скорости
    timeToImpact = -1;
    if (airborne) {
        // Порог — 30 м (~100 ft), но не больше половины текущего запаса высоты:
        // иначе на взлёте и бреющем полёте над ровной местностью сигнал звучал бы всегда.
        const float clearanceNow = a.pos.y - gearHeight - t.SurfaceHeight(a.pos.x, a.pos.z);
        const float margin = fminf(30.0f, clearanceNow * 0.5f);
        for (float s = 0.5f; s <= 40.0f; s += 0.5f) {
            Vector3 p = Vector3Add(a.pos, Vector3Scale(a.vel, s));
            float ground = t.SurfaceHeight(p.x, p.z);
            if (p.y - gearHeight - ground < margin) {
                // На заходе в посадочной конфигурации полоса впереди — это не угроза.
                float rwDist;
                int api = world::NearestAirport(p.x, p.z, &rwDist);
                bool runwayAhead = a.gear > 0.99f && rwDist < 1800.0f &&
                                   ground < world::GetAirport(api).rwy.center.y + 15.0f;
                if (!runwayAhead) timeToImpact = s;
                break;
            }
        }
    }

    if (!a.crashed) {
        // Режим 1 GPWS: чрезмерная вертикальная скорость снижения по радиовысоте.
        bool sinkWarn = airborne && radioAltFt < 2500.0f && descentFpm > 1700.0f + 1.6f * radioAltFt;
        bool sinkCaut = airborne && radioAltFt < 2500.0f && descentFpm > 1000.0f + 1.2f * radioAltFt;
        float pullUpTime = landingConfig ? 10.0f : 18.0f;

        cond[(int)Alert::PullUp] = sinkWarn || (timeToImpact > 0 && timeToImpact <= pullUpTime);
        cond[(int)Alert::Terrain] = timeToImpact > 0 && timeToImpact <= 35.0f;
        cond[(int)Alert::SinkRate] = sinkCaut;
        cond[(int)Alert::TooLowGear] = airborne && radioAltFt < 500.0f && a.gear < 0.99f &&
                                       iasKt < a.Type().approachKt + 60.0f && descentFpm > 0.0f;
        cond[(int)Alert::BankAngle] = airborne && fabsf(a.BankDeg()) > 35.0f;
        cond[(int)Alert::Overspeed] = iasKt > a.SpeedLimitKt() + 1.0f;
        cond[(int)Alert::Stall] = airborne && a.alpha > a.stallAlpha - 2.0f * DEG2RAD && a.ias > 15.0f;

        // Отказы систем (ECAM): пожар — красный, остальное — жёлтое.
        cond[(int)Alert::EngFire] = a.AnyEngineFire();
        bool engOut = false;
        for (int i = 0; i < a.Type().engineCount; ++i)
            if (!a.engines[i].Running() || a.fuel <= 0.0f) engOut = true;
        cond[(int)Alert::EngFail] = engOut;
        cond[(int)Alert::GearUnsafe] = a.Failed(Failure::GearHydraulics) && a.gear < 0.999f;
        cond[(int)Alert::FlapsJam] = a.Failed(Failure::FlapsJam);
        cond[(int)Alert::FuelLow] = a.fuel < a.Type().maxFuel * 0.08f;
        cond[(int)Alert::Structure] = a.Broken();
        cond[(int)Alert::Overstress] = a.overstressed && !a.Broken();
        cond[(int)Alert::Ice] = a.ice > 0.05f && (!a.lastAntiIce || a.Type().kind == AircraftKind::Prop);
        cond[(int)Alert::BrakesHot] = fmaxf(a.brakeTemp[0], a.brakeTemp[1]) > Aircraft::BRAKES_HOT;
    }

    for (int i = 0; i < N; ++i) {
        // Удержание сигнала после исчезновения условия — без «мигания» на границе.
        float holdTime = (i == (int)Alert::Overspeed || i == (int)Alert::Stall) ? 0.3f : 1.0f;
        if (cond[i]) hold_[i] = holdTime;
        else hold_[i] = fmaxf(hold_[i] - dt, 0.0f);
        active_[i] = cond[i] || hold_[i] > 0.0f;
        if (a.crashed) active_[i] = false;
    }
}

// ---------------------------------------------------------------- отсчёт высоты

namespace {
const float kCalloutFt[] = {1000, 500, 200, 100, 50, 40, 30, 20, 10};
}

void CalloutSystem::Reset()
{
    for (bool& b : armed_) b = false;
    queueLen_ = 0;
    shown = Callout::Count;
    showTimer = 0;
    retardTimer_ = 0;
    takeoff_ = v1_ = rotate_ = posClimb_ = gearUp_ = false;
    posClimbTime_ = 0;
    armed2500_ = armedHundred_ = false;
}

const char* CalloutSystem::Text(Callout c)
{
    switch (c) {
    case Callout::C1000: return "1000";
    case Callout::C500: return "500";
    case Callout::Minimums: return "MINIMUMS";
    case Callout::C100: return "100";
    case Callout::C50: return "50";
    case Callout::C40: return "40";
    case Callout::C30: return "30";
    case Callout::C20: return "20";
    case Callout::C10: return "10";
    case Callout::Retard: return "RETARD";
    case Callout::V1: return "V1";
    case Callout::Rotate: return "ROTATE";
    case Callout::PositiveClimb: return "POSITIVE CLIMB";
    case Callout::GearUp: return "GEAR UP";
    case Callout::C2500: return "2500";
    case Callout::HundredAbove: return "HUNDRED ABOVE";
    default: return "";
    }
}

const char* CalloutSystem::FileName(Callout c)
{
    switch (c) {
    case Callout::C1000: return "callout_1000.ogg";
    case Callout::C500: return "callout_500.ogg";
    case Callout::Minimums: return "minimums.ogg";
    case Callout::C100: return "callout_100.ogg";
    case Callout::C50: return "callout_50.ogg";
    case Callout::C40: return "callout_40.ogg";
    case Callout::C30: return "callout_30.ogg";
    case Callout::C20: return "callout_20.ogg";
    case Callout::C10: return "callout_10.ogg";
    case Callout::Retard: return "retard.ogg";
    case Callout::V1: return "v1.ogg";
    case Callout::Rotate: return "rotate.ogg";
    case Callout::PositiveClimb: return "positive_climb.ogg";
    case Callout::GearUp: return "gear_up.ogg";
    case Callout::C2500: return "callout_2500.ogg";
    case Callout::HundredAbove: return "hundred_above.ogg";
    default: return "";
    }
}

void CalloutSystem::Push(Callout c)
{
    if (queueLen_ < 4) queue_[queueLen_++] = c;
    shown = c;
    showTimer = 1.2f;
}

Callout CalloutSystem::Pop()
{
    if (queueLen_ == 0) return Callout::Count;
    Callout c = queue_[0];
    for (int i = 1; i < queueLen_; ++i) queue_[i - 1] = queue_[i];
    --queueLen_;
    return c;
}

void CalloutSystem::Update(const Aircraft& a, float raFt, const Controls& c, float dt)
{
    const float throttle = c.throttle;
    showTimer = fmaxf(showTimer - dt, 0.0f);
    retardTimer_ = fmaxf(retardTimer_ - dt, 0.0f);
    if (a.crashed) {
        queueLen_ = 0;
        return;
    }
    // Отсчёт идёт только при снижении с выпущенным шасси.
    const bool active = !a.onGround && a.gear > 0.99f && a.vel.y < -0.5f;
    for (int i = 0; i < 9; ++i) {
        float th = kCalloutFt[i];
        if (raFt > th * 1.1f + 20.0f) armed_[i] = true;
        if (armed_[i] && raFt <= th) {
            armed_[i] = false;
            if (active) {
                // Если самолёт быстро проходит несколько отметок, говорим только последнюю.
                queueLen_ = 0;
                Push((Callout)i);
            }
        }
    }
    // Взлёт (реактивные): «V1» чуть раньше V1 — с учётом времени на реакцию, «Rotate» на VR.
    const float iasKt = a.ias * MS_TO_KT;
    const AircraftType& t = a.Type();
    if (a.onGround && iasKt < 30.0f) {
        takeoff_ = true;
        v1_ = rotate_ = posClimb_ = gearUp_ = false;
    }
    if (takeoff_ && a.onGround && throttle > 0.6f && t.jet) {
        float vr = t.rotateKt;
        if (!v1_ && iasKt > vr - 9.0f) { v1_ = true; Push(Callout::V1); }
        if (!rotate_ && iasKt > vr) { rotate_ = true; Push(Callout::Rotate); }
    }
    // «Positive climb» — когда высотомер уверенно растёт; пилот отвечает «Gear up» и убирает шасси.
    if (takeoff_ && !a.onGround && t.retractableGear) {
        if (!posClimb_ && raFt > 15.0f && a.vel.y * MS_TO_FPM > 250.0f) {
            posClimb_ = true;
            posClimbTime_ = 0;
            Push(Callout::PositiveClimb);
        }
        if (posClimb_) posClimbTime_ += dt;
        if (posClimb_ && !gearUp_ && !c.gearDown && posClimbTime_ > 0.8f) {
            gearUp_ = true;
            Push(Callout::GearUp);
        }
        if (raFt > 1500.0f || posClimbTime_ > 60.0f) takeoff_ = false;
    }
    // «Two thousand five hundred» — радиовысотомер начал показывать высоту на снижении.
    if (raFt > 2800.0f) armed2500_ = true;
    if (armed2500_ && raFt <= 2500.0f && a.vel.y < -1.0f && !a.onGround) {
        armed2500_ = false;
        Push(Callout::C2500);
    }
    // «Hundred above» — 100 ft до минимума (200 ft).
    if (raFt > 400.0f) armedHundred_ = true;
    if (armedHundred_ && raFt <= 300.0f && active) {
        armedHundred_ = false;
        Push(Callout::HundredAbove);
    }

    // «Retard» — пора убрать газ: ниже 20 ft, а РУД не на малом газе.
    if (active && raFt < 20.0f && throttle > 0.05f && retardTimer_ <= 0.0f) {
        Push(Callout::Retard);
        retardTimer_ = 1.5f;
    }
}
