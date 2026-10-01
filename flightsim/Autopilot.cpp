#include "Autopilot.h"
#include "Aircraft.h"
#include "UiText.h"

using namespace fs;

void Autopilot::Reset(float hdgDeg, float altFt, float spdKt, bool takeoff, bool armApp)
{
    hdgBug = WrapDeg360(hdgDeg);
    altTarget = altFt;
    spdTarget = spdKt;
    apOn = athrOn = false;
    appArmed = armApp;
    locCaptured = gsCaptured = false;
    retard = false;
    takeoff_ = takeoff;
    goAround_ = vsMode_ = false;
    vsTarget = 0;
    lat = Lat::Hdg;
    vert = takeoff ? Vert::TakeOff : Vert::Alt;
    apOffTimer = 0;
    apOffSound = false;
    prevIas_ = 0;
    accelKt_ = 0;
}

void Autopilot::DisconnectAp(const char* reason)
{
    if (!apOn) return;
    apOn = false;
    apOffTimer = 3.0f;
    apOffSound = true;
    apOffReason = reason;
}

void Autopilot::DisconnectAthr()
{
    athrOn = false;
    retard = false;
}

void Autopilot::ToggleAp(const Aircraft& a, float raFt)
{
    if (apOn) {
        DisconnectAp("");
        return;
    }
    // Как в настоящем самолёте: автопилот включается только в воздухе.
    if (a.onGround || raFt < 100.0f || a.crashed) {
        apOffTimer = 2.0f;
        apOffReason = ui::L("AP: NOT BELOW 100 FT", "АВТОПИЛОТ: ТОЛЬКО ВЫШЕ 100 FT");
        return;
    }
    apOn = true;
    fdOn = true;
    apOffTimer = 0;
}

void Autopilot::ToggleAthr(const Aircraft& a)
{
    if (athrOn) {
        DisconnectAthr();
        return;
    }
    if (a.crashed) return;
    athrOn = true;
    retard = false;
}

void Autopilot::ToggleApp()
{
    if (appArmed || locCaptured) {
        appArmed = locCaptured = gsCaptured = false;
        return;
    }
    appArmed = true;
}

void Autopilot::Toga(const Aircraft& a)
{
    if (a.crashed || a.onGround) return;
    goAround_ = true;
    takeoff_ = false;
    appArmed = locCaptured = gsCaptured = false;
    hdgBug = a.HeadingDeg();
    float altFt = a.pos.y * M_TO_FT;
    altTarget = fmaxf(ceilf((altFt + 1500.0f) / 500.0f) * 500.0f, 2000.0f);
    spdTarget = a.Type().approachKt + 30.0f;
    athrOn = true;
    retard = false;
}

void Autopilot::ToggleVs(const Aircraft& a)
{
    if (vsMode_) {
        vsMode_ = false;
        return;
    }
    vsMode_ = true;
    goAround_ = false;
    vsTarget = roundf(a.vel.y * MS_TO_FPM / 100.0f) * 100.0f;
}

void Autopilot::Update(const Aircraft& a, float raFt, Controls& c, float dt)
{
    ils = ComputeIls(a);
    if (a.crashed) {
        apOn = athrOn = false;
        return;
    }
    if (a.Broken()) {
        DisconnectAp(ui::L("AP OFF: STRUCTURAL DAMAGE", "АВТОПИЛОТ ОТКЛ: РАЗРУШЕНИЕ КОНСТРУКЦИИ"));
        athrOn = false;
    }
    apOffTimer = fmaxf(apOffTimer - dt, 0.0f);

    const float hdg = a.HeadingDeg(), pitch = a.PitchDeg(), bank = a.BankDeg();
    const float iasKt = a.ias * MS_TO_KT;
    const float altFt = a.pos.y * M_TO_FT;
    const float alphaDeg = Clampf(a.alpha * RAD2DEG, -5.0f, 15.0f);
    const float V = fmaxf(a.tas, 30.0f);

    if (dt > 0) {
        float acc = (iasKt - prevIas_) / dt;
        accelKt_ = Lerp(accelKt_, acc, fminf(dt * 3.0f, 1.0f));
        prevIas_ = iasKt;
    }

    // ------------------------------------------------ режимы
    if (takeoff_ && !a.onGround && raFt > 400.0f) takeoff_ = false;
    if (goAround_ && altFt > altTarget - 300.0f) goAround_ = false;
    if (vsMode_ && ((vsTarget >= 0 && altFt >= altTarget - 50.0f) || (vsTarget < 0 && altFt <= altTarget + 50.0f))) vsMode_ = false;
    if ((appArmed || locCaptured) && ils.valid) {
        if (!locCaptured && fabsf(ils.locAngleDeg) < 2.5f && fabsf(WrapDeg180(hdg - ils.courseDeg)) < 70.0f)
            locCaptured = true;
        if (locCaptured && !gsCaptured && ils.gsValid && fabsf(ils.gsDevDeg) < 0.15f) gsCaptured = true;
    }
    if (locCaptured && !ils.valid) locCaptured = gsCaptured = false;
    if (locCaptured) appArmed = false;
    lat = locCaptured ? Lat::Loc : Lat::Hdg;
    if (gsCaptured) vsMode_ = goAround_ = false;
    vert = gsCaptured ? Vert::Gs : goAround_ ? Vert::GoAround : takeoff_ ? Vert::TakeOff : vsMode_ ? Vert::Vs : Vert::Alt;

    // ------------------------------------------------ команда крена
    float bankCmd = 0;
    if (lat == Lat::Loc) {
        // Курс на ось ВПП с углом подхода, уменьшающимся по мере приближения к оси.
        float desiredTrk = ils.courseDeg - Clampf(ils.lateralM * 0.04f, -25.0f, 25.0f);
        float trk = hdg;
        if (sqrtf(a.vel.x * a.vel.x + a.vel.z * a.vel.z) > 20.0f) trk = WrapDeg360(atan2f(a.vel.x, -a.vel.z) * RAD2DEG);
        bankCmd = Clampf(WrapDeg180(desiredTrk - trk) * 2.0f, -20.0f, 20.0f);
    } else {
        bankCmd = Clampf(WrapDeg180(hdgBug - hdg) * 1.5f, -25.0f, 25.0f);
    }
    if (a.onGround || (vert == Vert::TakeOff && raFt < 50.0f)) bankCmd = 0;

    // ------------------------------------------------ команда тангажа
    float pitchCmd = 0;
    const AircraftType& ty = a.Type();
    const float toPitch = ty.jet ? 10.0f : 8.0f;
    if (vert == Vert::TakeOff) {
        pitchCmd = (a.onGround && iasKt < ty.rotateKt - 5.0f) ? 0.0f : toPitch;
    } else if (vert == Vert::GoAround) {
        // Нос 12.5°, но не ценой скорости: ближе к сваливанию — опускаем.
        float vMin = a.StallSpeedKt() * 1.2f;
        pitchCmd = 12.5f * Clampf((iasKt - vMin) / 15.0f, 0.2f, 1.0f);
    } else {
        float gammaCmd;
        if (vert == Vert::Gs) {
            gammaCmd = Clampf(-3.0f - ils.gsDevDeg * 2.5f, -6.0f, 0.0f);
        } else {
            float maxClimb = ty.jet ? 2500.0f : 700.0f;
            float vsCmd = vert == Vert::Vs ? vsTarget : Clampf((altTarget - altFt) * 4.0f, -2000.0f, maxClimb);
            // Защита скорости: не набирать высоту ценой сваливания.
            float vMin = a.StallSpeedKt() * 1.25f;
            if (vsCmd > 0 && iasKt < vMin + 15.0f) vsCmd *= Clampf((iasKt - vMin) / 15.0f, 0.0f, 1.0f);
            gammaCmd = asinf(Clampf(vsCmd / MS_TO_FPM / V, -0.5f, 0.5f)) * RAD2DEG;
        }
        // В вираже часть подъёмной силы уходит вбок — нос нужно держать чуть выше.
        float turnComp = alphaDeg * (1.0f / fmaxf(cosf(bank * DEG2RAD), 0.5f) - 1.0f);
        pitchCmd = gammaCmd + alphaDeg + turnComp;
    }
    fdPitch = Clampf(pitchCmd, -10.0f, 15.0f);
    fdBank = bankCmd;

    // ------------------------------------------------ автопилот
    if (apOn) {
        if (fabsf(bank) > 45.0f || pitch > 25.0f || pitch < -20.0f) {
            DisconnectAp(ui::L("AP OFF: ATTITUDE", "АВТОПИЛОТ ОТКЛЮЧЁН: ПОЛОЖЕНИЕ"));
        } else if (vert == Vert::Gs && raFt < 150.0f) {
            DisconnectAp(ui::L("AP OFF: LAND MANUALLY", "АВТОПИЛОТ ОТКЛЮЧЁН: САДИТЕСЬ ВРУЧНУЮ"));   // дальше пилот выравнивает сам
        }
    }
    if (apOn) {
        float q = a.omega.z * RAD2DEG, p = a.omega.x * RAD2DEG;
        c.pitch = Clampf((fdPitch - pitch) * 0.09f - q * 0.06f, -0.6f, 0.6f);
        c.roll = Clampf((fdBank - bank) * 0.04f - p * 0.03f, -0.5f, 0.5f);
        c.yaw = 0;
        // Автотриммер: переносит усилие со штурвала на триммер.
        c.trim = Clampf(c.trim + c.pitch * 0.8f * dt, -1.0f, 1.0f);
    }

    // ------------------------------------------------ автомат тяги
    if (athrOn) {
        if (a.onGround && a.wheelsOnGround > 0 && !takeoff_) {
            DisconnectAthr();
        } else if (!a.onGround && a.gear > 0.99f && raFt < 30.0f && a.vel.y < 0.0f && !takeoff_) {
            retard = true;   // «RETARD»: малый газ перед касанием
        }
        if (retard) {
            c.throttle = MoveTowards(c.throttle, 0.0f, 0.5f * dt);
        } else if (takeoff_ || goAround_) {
            c.throttle = MoveTowards(c.throttle, 1.0f, 0.5f * dt);   // взлётный режим (TOGA)
        } else {
            float err = spdTarget - iasKt;
            float rate = Clampf(err * 0.015f - accelKt_ * 0.06f, -0.25f, 0.25f);
            c.throttle = Clampf(c.throttle + rate * dt, 0.0f, 1.0f);
        }
    }
}
