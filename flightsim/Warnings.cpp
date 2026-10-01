#include "Warnings.h"
#include "Aircraft.h"
#include "Terrain.h"

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
    default: return "";
    }
}

bool WarningSystem::IsWarning(Alert a)
{
    return a == Alert::PullUp || a == Alert::Overspeed || a == Alert::Stall;
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

    const float gearHeight = 1.75f;   // высота ЦМ над грунтом на стоянке
    float ra = (a.pos.y - gearHeight - t.SurfaceHeight(a.pos.x, a.pos.z)) * M_TO_FT;
    radioAltFt = fmaxf(ra, 0.0f);
    const float descentFpm = -a.vel.y * MS_TO_FPM;
    const float iasKt = a.ias * MS_TO_KT;
    const bool airborne = !a.onGround && radioAltFt > 30.0f;
    const bool landingConfig = a.gear > 0.99f && a.flaps >= 19.0f;

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
                bool runwayAhead = a.gear > 0.99f && Terrain::DistToRunway(p.x, p.z) < 1800.0f &&
                                   ground < Terrain::kAirportElev + 15.0f;
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
        cond[(int)Alert::TooLowGear] = airborne && radioAltFt < 500.0f && a.gear < 0.99f && iasKt < 190.0f && descentFpm > 0.0f;
        cond[(int)Alert::BankAngle] = airborne && fabsf(a.BankDeg()) > 35.0f;
        cond[(int)Alert::Overspeed] = iasKt > a.SpeedLimitKt() + 1.0f;
        cond[(int)Alert::Stall] = airborne && a.alpha > a.stallAlpha - 2.0f * DEG2RAD && a.ias > 15.0f;
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
