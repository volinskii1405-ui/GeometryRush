#include "Autopilot.h"
#include "Aircraft.h"
#include "UiText.h"

#include <cstdio>

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
    activeWp = 0;
    navOn = false;
}

void Autopilot::ToggleNav(const Aircraft& a)
{
    if (navOn) {
        navOn = false;
        hdgBug = WrapDeg360(roundf(a.HeadingDeg()));
        return;
    }
    if (!RouteActive()) {
        apOffTimer = 2.0f;
        apOffReason = ui::L("NAV: NO ROUTE (N - map)", "NAV: НЕТ МАРШРУТА (N — карта)");
        return;
    }
    navOn = true;
    if (activeWp == 0) resetLeg_ = true;   // первый участок — от текущего положения
}

void Autopilot::AddWaypoint(Vector3 pos, const char* name, float altFt)
{
    if (!RouteActive()) resetLeg_ = true;   // маршрут был пройден — к новой точке прямо от самолёта
    Waypoint w;
    w.pos = pos;
    snprintf(w.name, sizeof w.name, "%s", name);
    w.altFt = altFt;
    route.push_back(w);
}

void Autopilot::RemoveLastWaypoint()
{
    if (route.empty()) return;
    route.pop_back();
    if (activeWp > (int)route.size()) activeWp = (int)route.size();
    if (!RouteActive()) navOn = false;
}

void Autopilot::RemoveWaypoint(int i)
{
    if (i < 0 || i >= (int)route.size()) return;
    if (i == activeWp) resetLeg_ = true;   // удалили активную — прямо на следующую
    route.erase(route.begin() + i);
    if (i < activeWp) --activeWp;
    if (activeWp > (int)route.size()) activeWp = (int)route.size();
    if (!RouteActive()) navOn = false;
}

void Autopilot::ClearRoute()
{
    route.clear();
    activeWp = 0;
    navOn = false;
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
    float altFt = a.AltimeterFt(baro_);
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
    baro_ = c.baro;
    const float altFt = a.AltimeterFt(baro_);
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

    // ------------------------------------------------ маршрут: наведение L1 и смена точек
    // Самолёт целится в точку на линии участка на расстоянии L1 впереди (≈10 с полёта) и держит
    // боковое ускорение 2·V²/L1·sin(η) — плавный выход на линию без раскачки, как в настоящих автопилотах.
    const float gsMs = sqrtf(a.vel.x * a.vel.x + a.vel.z * a.vel.z);
    const float trkNow = gsMs > 20.0f ? WrapDeg360(atan2f(a.vel.x, -a.vel.z) * RAD2DEG) : hdg;
    float navBank = 0.0f;
    if (RouteActive()) {
        if (resetLeg_ || (!navOn && activeWp == 0)) {
            legFrom_ = a.pos;
            direct_ = true;
            resetLeg_ = false;
        }
        const Waypoint& to = route[activeWp];
        const Vector3 from = (direct_ || activeWp == 0) ? legFrom_ : route[activeWp - 1].pos;
        float dx = to.pos.x - a.pos.x, dz = to.pos.z - a.pos.z;
        navDistM = sqrtf(dx * dx + dz * dz);
        float lx = to.pos.x - from.x, lz = to.pos.z - from.z;
        float len = sqrtf(lx * lx + lz * lz);
        float ux = len > 1.0f ? lx / len : dx / fmaxf(navDistM, 1.0f), uz = len > 1.0f ? lz / len : dz / fmaxf(navDistM, 1.0f);
        float relx = a.pos.x - from.x, relz = a.pos.z - from.z;
        float along = relx * ux + relz * uz;
        navXtkM = relx * -uz + relz * ux;   // вправо от линии: (−uz, ux)
        // Далеко от линии L1 растёт вместе с уклонением: выход на линию под углом не круче ~55°, без перелёта.
        const float L1 = fmaxf(Clampf(gsMs * 10.0f, 350.0f, 2500.0f), 1.2f * fabsf(navXtkM));
        float aimAlong = along + sqrtf(fmaxf(L1 * L1 - navXtkM * navXtkM, 0.0f));
        float ax = from.x + ux * aimAlong - a.pos.x, az = from.z + uz * aimAlong - a.pos.z;
        navTrack = WrapDeg360(atan2f(ax, -az) * RAD2DEG);
        float eta = WrapDeg180(navTrack - trkNow) * DEG2RAD;
        if (fabsf(eta) > PI * 0.5f) eta = Sign(eta) * PI * 0.5f;   // цель сзади — разворот с полным креном
        float aLat = 2.0f * gsMs * gsMs / L1 * sinf(eta);
        navBank = Clampf(atanf(aLat / G) * RAD2DEG, -25.0f, 25.0f);

        // Упреждение разворота по радиусу виража 25°, но не больше половины следующего участка.
        const float radius = gsMs * gsMs / (G * tanf(25.0f * DEG2RAD));
        float lead = 60.0f;
        if (activeWp + 1 < (int)route.size()) {
            const Waypoint& nx = route[activeWp + 1];
            float nlx = nx.pos.x - to.pos.x, nlz = nx.pos.z - to.pos.z, nlen = sqrtf(nlx * nlx + nlz * nlz);
            float turn = fabsf(WrapDeg180(atan2f(nlx, -nlz) * RAD2DEG - atan2f(ux, -uz) * RAD2DEG)) * DEG2RAD;
            lead = Clampf(radius * tanf(fminf(turn, 0.5f * PI) * 0.5f), 60.0f, fminf(0.5f * nlen, 3000.0f));
        }
        float remaining = len > 1.0f ? len - along : navDistM;
        // Точку прошли: до неё по линии меньше упреждения, или она уже на траверзе/позади.
        if (!a.onGround && (navDistM <= (navOn ? lead : 300.0f) || remaining < 0.0f)) {
            ++activeWp;
            // Следующий участок — прямо от того места, где самолёт начал разворот: после виража
            // он сразу на линии и не «догоняет» линию между точками.
            legFrom_ = a.pos;
            direct_ = true;
            if (!RouteActive() && navOn) {   // последняя точка: дальше прежним курсом
                navOn = false;
                hdgBug = WrapDeg360(roundf(hdg));
            }
        }
    } else {
        navOn = false;
        navXtkM = 0;
    }

    // ------------------------------------------------ VNAV: высоты точек маршрута
    // Ближайшая впереди точка с высотой: набор — сразу, снижение — по прямой так, чтобы прийти на неё на высоте.
    vnavActive = false;
    if (navOn && vnavOn && RouteActive() && !gsCaptured && !goAround_ && !takeoff_ && !vsMode_) {
        float dist = navDistM;
        for (int k = activeWp; k < (int)route.size(); ++k) {
            if (k > activeWp) dist += Vector2Distance({route[k - 1].pos.x, route[k - 1].pos.z}, {route[k].pos.x, route[k].pos.z});
            if (route[k].altFt <= 0.0f) continue;
            altTarget = route[k].altFt;
            float diff = altTarget - altFt;
            const AircraftType& t = a.Type();
            float maxDesc = t.kind == AircraftKind::Prop ? 1000.0f : 3000.0f;
            if (diff < -150.0f) {
                float minutes = dist / fmaxf(gsMs, 20.0f) / 60.0f;
                vnavVs_ = Clampf(diff / fmaxf(minutes, 0.1f) * 1.15f, -maxDesc, -200.0f);
            } else {
                vnavVs_ = Clampf(diff * 4.0f, -maxDesc, t.jet ? 2500.0f : 700.0f);
            }
            vnavActive = true;
            break;
        }
    }
    lat = locCaptured ? Lat::Loc : (navOn ? Lat::Nav : Lat::Hdg);
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
    } else if (lat == Lat::Nav) {
        bankCmd = navBank;
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
            float vsCmd = vert == Vert::Vs ? vsTarget
                        : vnavActive ? vnavVs_
                        : Clampf((altTarget - altFt) * 4.0f, -2000.0f, maxClimb);
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
