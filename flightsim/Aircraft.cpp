#include "Aircraft.h"
#include "Terrain.h"
#include "UiText.h"

#include <cstdio>

using namespace fs;
using ui::L;

namespace {

constexpr float CL_ALPHA = 5.0f;   // 1/рад
constexpr float NEG_STALL = -11.0f * DEG2RAD;

float FlapFrac(const AircraftType& t, float flapsDeg) { return flapsDeg / t.flapDeg[3]; }
float StallAlpha(const AircraftType& t, float ff) { return (t.stallDeg - t.stallFlapsLossDeg * ff) * DEG2RAD; }
float Cl0(const AircraftType& t, float ff) { return t.cl0 + t.clFlaps * ff; }

// Коэффициент подъёмной силы по углу атаки: линейный участок, срыв, «плоская пластина».
float LiftCoef(const AircraftType& t, float a, float ff, float* stallFactor)
{
    const float as = StallAlpha(t, ff), cl0 = Cl0(t, ff);
    const float plate = 1.05f * sinf(2.0f * a);
    *stallFactor = fmaxf(SmoothStep(as - 1.0f * DEG2RAD, as + 4.0f * DEG2RAD, a),
                         SmoothStep(-NEG_STALL + 1.0f * DEG2RAD, -NEG_STALL + 5.0f * DEG2RAD, -a));
    if (a >= NEG_STALL && a <= as) return cl0 + CL_ALPHA * a;
    if (a > as) {
        float clmax = cl0 + CL_ALPHA * as;
        return Lerp(clmax, plate, SmoothStep(as, as + 9.0f * DEG2RAD, a));
    }
    float clmin = cl0 + CL_ALPHA * NEG_STALL;
    return Lerp(clmin, plate, SmoothStep(-NEG_STALL, -NEG_STALL + 9.0f * DEG2RAD, -a));
}

// Тяга по режиму 0..1 (без учёта реверса).
float RawThrust(const AircraftType& t, float frac, float V, float rho, float mach)
{
    float sigma = rho / 1.225f;
    if (t.jet) {
        return t.maxThrust / t.engineCount * (0.05f + 0.95f * powf(frac, 1.5f)) * powf(sigma, 0.75f) * (1.0f - 0.3f * mach);
    }
    // Винт: на месте — статическая тяга, на скорости — мощность / скорость.
    float power = t.propPower * (0.05f + 0.95f * frac) * sigma;
    float tStatic = t.maxThrust * (0.05f + 0.95f * frac) * sigma;
    float v0 = power * 0.8f / fmaxf(tStatic, 1.0f);
    return power * 0.8f / sqrtf(V * V + v0 * v0);
}

} // namespace

void Aircraft::Reset(Vector3 position, float headingDeg, float speedMs, bool ground, const Controls& c,
                     float pitchDeg, float gammaDeg, float fuelKg)
{
    const AircraftType& t = *type_;
    pos = position;
    // Курс 0 — север (-Z), 90 — восток (+X). Нос самолёта — ось +X связанной системы.
    Quaternion yaw = QuaternionFromAxisAngle({0, 1, 0}, (90.0f - headingDeg) * DEG2RAD);
    rot = QuaternionMultiply(yaw, QuaternionFromAxisAngle({0, 0, 1}, pitchDeg * DEG2RAD));
    Vector3 flat = Rotate({1, 0, 0}, yaw);
    float g = gammaDeg * DEG2RAD;
    vel = Vector3Scale(Vector3Add(Vector3Scale(flat, cosf(g)), {0, sinf(g), 0}), speedMs);
    omega = {};
    for (Engine& e : engines) {
        e = Engine{};
        e.n1 = t.idleN1 + (100.0f - t.idleN1) * c.throttle;
    }
    fuel = fuelKg >= 0 ? fminf(fuelKg, t.maxFuel) : t.defaultFuel;
    fuelFlow = 0;
    reverser = 0;
    elevator = aileron = rudder = noseSteer = 0;
    flaps = t.flapDeg[c.flapsLever];
    gear = (c.gearDown || !t.retractableGear) ? 1.0f : 0.0f;
    speedbrake = c.speedbrake ? 1.0f : 0.0f;
    alpha = beta = 0;
    gLoad = 1;
    onGround = ground;
    wheelsOnGround = ground ? 3 : 0;
    for (bool& w : wheelContact_) w = ground;
    for (bool& f : failures_) f = false;
    crashed = false;
    crashReason.clear();
    touchdown = false;
    touchdownFpm = 0;
    tailStrike = false;
    stallDrop_ = 0;
}

void Aircraft::Fail(Failure f)
{
    failures_[(int)f] = true;
    switch (f) {
    case Failure::Engine1: engines[0].failed = true; break;
    case Failure::Engine2: if (type_->engineCount > 1) engines[1].failed = true; else engines[0].failed = true; break;
    case Failure::Fire1: engines[0].fire = true; break;
    case Failure::Fire2: engines[type_->engineCount > 1 ? 1 : 0].fire = true; break;
    default: break;
    }
}

float Aircraft::PitchDeg() const { return asinf(Clampf(Forward().y, -1, 1)) * RAD2DEG; }

float Aircraft::BankDeg() const
{
    Vector3 r = Right(), u = Up();
    return atan2f(-r.y, u.y) * RAD2DEG;
}

float Aircraft::HeadingDeg() const
{
    Vector3 f = Forward();
    if (fabsf(f.y) > 0.999f) f = Vector3Scale(Up(), -Sign(f.y)); // почти вертикально
    return WrapDeg360(atan2f(f.x, -f.z) * RAD2DEG);
}

float Aircraft::N1() const
{
    float sum = 0;
    for (int i = 0; i < type_->engineCount; ++i) sum += engines[i].n1;
    return sum / type_->engineCount;
}

float Aircraft::StallSpeedKt() const
{
    const AircraftType& t = *type_;
    float ff = FlapFrac(t, flaps);
    float clmax = Cl0(t, ff) + CL_ALPHA * StallAlpha(t, ff);
    return sqrtf(2.0f * Mass() * G / (1.225f * t.wingArea * clmax)) * MS_TO_KT;
}

float Aircraft::SpeedLimitKt() const
{
    const AircraftType& t = *type_;
    float lim = t.vmo;
    for (int i = 1; i < 4; ++i)
        if (flaps > t.flapDeg[i - 1] + 0.5f) lim = fminf(lim, t.vfe[i]);
    if (t.retractableGear && gear > 0.01f) lim = fminf(lim, t.vle);
    return lim;
}

Aircraft::Trim Aircraft::ComputeTrim(const AircraftType& t, float mass, float tas, float altM, float flapsDeg,
                                     bool gearDown, float gammaDeg)
{
    const float rho = AirDensity(altM), qd = 0.5f * rho * tas * tas;
    const float ff = FlapFrac(t, flapsDeg), W = mass * G, gam = gammaDeg * DEG2RAD;
    const float mach = tas / SpeedOfSound(altM);
    const float kInduced = 1.0f / (PI * 0.8f * t.span * t.span / t.wingArea);
    const float trimRange = t.trimRangeDeg * DEG2RAD;
    float elevEff = 0, alpha = 0, thrust = 0;
    for (int it = 0; it < 8; ++it) {
        float clReq = W * cosf(gam) / (qd * t.wingArea);
        alpha = (clReq - Cl0(t, ff) - 0.35f * elevEff) / CL_ALPHA;
        float cd = t.cd0 + kInduced * clReq * clReq + t.cdFlapsLin * ff + t.cdFlapsSq * ff * ff + (gearDown ? t.cdGear : 0.0f);
        thrust = qd * t.wingArea * cd + W * sinf(gam);
        elevEff = (t.cmAlpha * sinf(alpha) - t.cm0 + t.cmFlaps * ff + t.thrustLineY * thrust / (qd * t.wingArea * t.chord)) / t.cmElev;
    }
    // Подбираем режим двигателя под нужную тягу (двоичным поиском — годится и для винта).
    float lo = 0, hi = 1;
    for (int it = 0; it < 30; ++it) {
        float mid = 0.5f * (lo + hi);
        if (RawThrust(t, mid, tas, rho, mach) * t.engineCount < thrust) lo = mid;
        else hi = mid;
    }
    return Trim{alpha * RAD2DEG, Clampf(elevEff / trimRange, -1, 1), 0.5f * (lo + hi)};
}

void Aircraft::Crash(const std::string& reason)
{
    if (crashed) return;
    crashed = true;
    crashReason = reason;
}

float Aircraft::EngineThrust(int i, float V, float rho, float dt, const Controls& c)
{
    const AircraftType& t = *type_;
    Engine& e = engines[i];

    // Пожарный кран: двигатель выключается, огнетушитель гасит пожар за 3 с.
    if (c.fireHandle[i] && !e.shutdown) e.shutdown = true;
    if (e.shutdown && e.fire) {
        e.extinguish += dt;
        if (e.extinguish > 3.0f) e.fire = false;
    }
    if (e.fire) e.fireTime += dt;

    bool running = e.Running() && fuel > 0.0f;
    float target;
    if (!running) {
        target = t.jet ? 6.0f : 0.0f;   // авторотация
    } else if (reverser > 0.5f) {
        target = t.idleN1 + (75.0f - t.idleN1) * (c.reverse ? 1.0f : 0.0f);
    } else {
        target = t.idleN1 + (100.0f - t.idleN1) * Clampf(c.throttle, 0, 1);
    }
    float spool = t.jet ? (e.n1 < 50.0f ? 0.35f : 0.9f) : 2.0f;   // турбина раскручивается медленно
    e.n1 = MoveTowards(e.n1, target, fminf(fabsf(target - e.n1) * spool, t.jet ? 14.0f : 60.0f) * dt);

    float frac = Clampf((e.n1 - t.idleN1) / (100.0f - t.idleN1), 0, 1);
    float raw = running ? RawThrust(t, frac, V, rho, mach) : 0.0f;
    if (running && e.n1 < t.idleN1 - 1.0f) raw *= Clampf(e.n1 / t.idleN1, 0, 1);
    // Реверс: створки разворачивают часть струи вперёд.
    e.thrust = raw * (1.0f - reverser) - raw * reverser * 0.45f;

    if (running) {
        float burn = t.jet ? t.fuelFlowCoef * fmaxf(raw, t.maxThrust / t.engineCount * 0.04f)
                           : t.fuelFlowCoef * t.propPower * (0.05f + 0.95f * frac);
        fuelFlow += burn;
    }
    return e.thrust;
}

void Aircraft::Step(float dt, const Controls& c, const Terrain& terrain, Vector3 wind)
{
    const AircraftType& t = *type_;
    if (crashed) {
        // Обломки падают баллистически до земли и там остаются.
        float ground = terrain.SurfaceHeight(pos.x, pos.z) + 1.0f;
        if (pos.y > ground) {
            vel.y -= G * dt;
            vel = Vector3Scale(vel, 1.0f - 0.25f * dt);
            pos = Vector3Add(pos, Vector3Scale(vel, dt));
            Quaternion spin = QuaternionFromAxisAngle({1, 0, 0}, 1.5f * dt);
            rot = QuaternionNormalize(QuaternionMultiply(rot, spin));
        }
        if (pos.y <= ground) {
            pos.y = ground;
            vel = {};
        }
        return;
    }

    const float mass = Mass();
    const float trimRange = t.trimRangeDeg * DEG2RAD;

    // ------------------------------------------------ приводы органов управления
    elevator = MoveTowards(elevator, Clampf(c.pitch, -1, 1) * t.maxElevDeg * DEG2RAD, 60.0f * DEG2RAD * dt);
    aileron = MoveTowards(aileron, Clampf(c.roll, -1, 1) * t.maxAilDeg * DEG2RAD, 70.0f * DEG2RAD * dt);
    rudder = MoveTowards(rudder, Clampf(c.yaw, -1, 1) * t.maxRudDeg * DEG2RAD, 50.0f * DEG2RAD * dt);
    if (!failures_[(int)Failure::FlapsJam]) flaps = MoveTowards(flaps, t.flapDeg[c.flapsLever], 2.5f * dt);
    speedbrake = MoveTowards(speedbrake, c.speedbrake ? 1.0f : 0.0f, dt / 1.5f);
    if (!t.retractableGear) {
        gear = 1.0f;
    } else if (failures_[(int)Failure::GearHydraulics]) {
        // Гидросистема отказала: шасси выпускается только аварийно, под собственным весом.
        if (c.altGear) gear = MoveTowards(gear, 1.0f, dt / 15.0f);
    } else {
        // Блокировка уборки шасси на земле (концевик обжатия стоек).
        float gearTarget = (c.gearDown || onGround) ? 1.0f : 0.0f;
        gear = MoveTowards(gear, gearTarget, dt / t.gearTime);
    }
    // Реверс раскрывается только на земле и только на малом газе.
    bool revAllowed = t.hasReverser && onGround && c.throttle < 0.05f;
    reverser = MoveTowards(reverser, (c.reverse && revAllowed) ? 1.0f : 0.0f, dt / 1.5f);

    const Quaternion inv = QuaternionInvert(rot);
    const Vector3 air = Vector3Subtract(vel, wind);
    const Vector3 vb = Rotate(air, inv);
    const float V = Vector3Length(vb);
    const float rho = AirDensity(pos.y);
    const float qd = 0.5f * rho * V * V;
    tas = V;
    ias = V * sqrtf(rho / 1.225f);
    mach = V / SpeedOfSound(pos.y);

    // ------------------------------------------------ двигатели и топливо
    fuelFlow = 0;
    thrust = 0;
    Vector3 Fb{0, 0, 0}, Tb{0, 0, 0};
    for (int i = 0; i < t.engineCount; ++i) {
        float th = EngineThrust(i, V, rho, dt, c);
        thrust += th;
        Fb.x += th;
        // Момент от тяги: несимметричная тяга разворачивает самолёт, плечо по высоте — тангаж.
        Tb.y += t.enginePos[i].z * th;
        Tb.z += -t.thrustLineY * th;
        if (engines[i].fireTime > 120.0f) {
            Crash(L("Engine fire spread to the wing", "Пожар двигателя перекинулся на крыло"));
            return;
        }
    }
    if (failures_[(int)Failure::FuelLeak]) fuelFlow += t.maxFuel * 0.004f;
    fuel = fmaxf(fuel - fuelFlow * dt, 0.0f);

    // ------------------------------------------------ аэродинамика
    alpha = V > 1.0f ? atan2f(-vb.y, vb.x) : 0.0f;
    beta = V > 1.0f ? asinf(Clampf(vb.z / V, -1, 1)) : 0.0f;

    const float ff = FlapFrac(t, flaps);
    stallAlpha = StallAlpha(t, ff);
    const float Vd = fmaxf(V, 10.0f);
    const float p = omega.x, q = omega.z, r = -omega.y;
    const float ph = p * t.span / (2 * Vd), qh = q * t.chord / (2 * Vd), rh = r * t.span / (2 * Vd);
    const float elevEff = elevator + Clampf(c.trim, -1, 1) * trimRange;

    // Влияние земли: меньше индуктивное сопротивление на высоте меньше размаха.
    float wingAgl = fmaxf(pos.y - t.restHeight * 0.7f - terrain.SurfaceHeight(pos.x, pos.z), 0.0f);
    float ge = 16.0f * wingAgl / t.span;
    float groundEffect = (ge * ge) / (1.0f + ge * ge);

    float stall = 0;
    float cl = LiftCoef(t, alpha, ff, &stall);
    cl += 4.0f * qh + 0.35f * elevEff;
    cl *= 1.0f - 0.15f * speedbrake * (onGround ? 3.0f : 1.0f);   // на земле интерцепторы «гасят» подъёмную силу
    cl *= 1.0f + 0.08f * (1.0f - groundEffect);

    const float aspect = t.span * t.span / t.wingArea;
    const float kInduced = 1.0f / (PI * 0.8f * aspect);
    float sa = sinf(alpha);
    float cd = t.cd0 + kInduced * cl * cl * (0.3f + 0.7f * groundEffect)
             + t.cdFlapsLin * ff + t.cdFlapsSq * ff * ff + t.cdGear * gear + 0.035f * speedbrake
             + 0.6f * beta * beta
             + 1.2f * sa * sa * SmoothStep(stallAlpha - 3.0f * DEG2RAD, stallAlpha + 8.0f * DEG2RAD, fabsf(alpha));
    if (mach > 0.72f) cd += 25.0f * Sq(mach - 0.72f);   // волновой кризис

    float cy = -0.75f * beta - 0.12f * rudder;

    // Сваливание: демпфирование крена пропадает (сваливание на крыло, вход в штопор).
    if (stall > 0.3f && stallDrop_ == 0.0f) stallDrop_ = (GetRandomValue(0, 1) ? 1.0f : -1.0f);
    if (stall < 0.05f) stallDrop_ = 0.0f;
    float clRoll = -0.09f * beta + t.clAileron * aileron - t.clp * (1.0f - 1.4f * stall) * ph + 0.12f * rh
                 + 0.035f * stall * stallDrop_;
    float cm = t.cm0 - t.cmAlpha * sinf(alpha) + t.cmElev * elevEff - t.cmq * qh - t.cmFlaps * ff - 0.12f * stall;
    float cn = t.cnBeta * beta + t.cnRudder * rudder - t.cnr * rh - 0.03f * ph - 0.012f * aileron;

    if (V > 0.1f) {
        Vector3 vxy{vb.x, vb.y, 0};
        if (Vector3Length(vxy) > 0.01f) {
            Vector3 liftDir = Vector3Normalize(Vector3CrossProduct({0, 0, 1}, vxy));
            Fb = Vector3Add(Fb, Vector3Scale(liftDir, qd * t.wingArea * cl));
        }
        Fb = Vector3Add(Fb, Vector3Scale(vb, -qd * t.wingArea * cd / V));
        Fb.z += qd * t.wingArea * cy;
        Tb.x += qd * t.wingArea * t.span * clRoll;
        Tb.y += -qd * t.wingArea * t.span * cn;
        Tb.z += qd * t.wingArea * t.chord * cm;
    }

    Vector3 Fw = Rotate(Fb, rot);           // без гравитации — для перегрузки
    Vector3 Tw{};                            // моменты от земли, мир

    // ------------------------------------------------ шасси и касание земли
    const Vector3 omegaW = Rotate(omega, rot);
    const Vector3 fwd = Forward();
    const Vector3 up = Up();
    float groundSpeed = sqrtf(vel.x * vel.x + vel.z * vel.z);
    float maxSteer = Lerp(t.maxSteerDeg, 6.0f, SmoothStep(5.0f, 30.0f, groundSpeed)) * DEG2RAD;
    noseSteer = MoveTowards(noseSteer, Clampf(c.yaw, -1, 1) * maxSteer, 60.0f * DEG2RAD * dt);

    int wheels = 0;
    for (int k = 0; k < CONTACT_COUNT; ++k) {
        bool isWheel = k <= RIGHT_MAIN;
        if (isWheel && gear < 0.98f) {
            wheelContact_[k] = false;
            continue;
        }
        Vector3 rw = Rotate(t.contacts[k], rot);
        Vector3 pw = Vector3Add(pos, rw);
        float ground = terrain.SurfaceHeight(pw.x, pw.z);
        float depth = ground - pw.y;
        if (depth <= 0.0f) {
            if (isWheel) wheelContact_[k] = false;
            continue;
        }
        Vector3 vp = Vector3Add(vel, Vector3CrossProduct(omegaW, rw));
        Vector3 n = terrain.Normal(pw.x, pw.z);
        float vn = Vector3DotProduct(vp, n);
        bool water = terrain.IsWater(pw.x, pw.z);

        if (water) {
            Crash(V > 40.0f ? L("Impact with the sea", "Удар о воду") : L("Ditched in the sea", "Приводнение в море"));
            return;
        }
        if (!isWheel && k != TAIL) {
            char buf[160];
            const char* what = k == BELLY ? (gear < 0.98f ? L("Gear-up belly impact", "Посадка на брюхо — шасси не выпущено")
                                                          : L("Fuselage impact", "Удар фюзеляжем"))
                             : k == NOSE_CONE ? L("Nose impact", "Удар носом")
                             : (k == WING_L || k == WING_R) ? L("Wingtip strike", "Удар законцовкой крыла")
                             : (t.jet ? L("Engine pod strike", "Удар гондолой двигателя") : L("Propeller strike", "Удар винтом о землю"));
            snprintf(buf, sizeof buf, "%s: %.0f kt, %.0f fpm", what, Vector3Length(vel) * MS_TO_KT, vn * MS_TO_FPM);
            Crash(buf);
            return;
        }
        if (k == TAIL) {
            if (vn < -3.0f || groundSpeed > 90.0f) {
                Crash(L("Tail impact", "Удар хвостом"));
                return;
            }
            tailStrike = true;
        }
        if (isWheel && !wheelContact_[k]) {
            if (wheelsOnGround == 0 && !touchdown) {
                touchdown = true;
                touchdownFpm = vn * MS_TO_FPM;
            }
            if (vn < -t.gearCollapseMs) {
                char buf[128];
                snprintf(buf, sizeof buf, L("Gear collapsed: hard landing (%.0f fpm)", "Сломано шасси: грубая посадка (%.0f fpm)"),
                         vn * MS_TO_FPM);
                Crash(buf);
                return;
            }
        }
        if (isWheel) {
            wheelContact_[k] = true;
            ++wheels;
        }

        float kSpring = k == NOSE_WHEEL ? t.springNose : (k == TAIL ? t.springMain * 2.0f : t.springMain);
        float cDamp = k == NOSE_WHEEL ? t.dampNose : (k == TAIL ? t.dampMain * 1.6f : t.dampMain);
        float fn = fmaxf(kSpring * depth - cDamp * vn, 0.0f);

        // Направление качения колеса в плоскости грунта.
        Vector3 dir = fwd;
        if (k == NOSE_WHEEL) dir = Rotate({cosf(noseSteer), 0, sinf(noseSteer)}, rot);
        Vector3 rollDir = Vector3Subtract(dir, Vector3Scale(n, Vector3DotProduct(dir, n)));
        if (Vector3Length(rollDir) < 1e-3f) rollDir = Vector3Subtract(up, Vector3Scale(n, Vector3DotProduct(up, n)));
        rollDir = Vector3Normalize(rollDir);
        Vector3 sideDir = Vector3CrossProduct(rollDir, n);
        float vl = Vector3DotProduct(vp, rollDir);
        float vs = Vector3DotProduct(vp, sideDir);

        float muRoll = 0.015f;
        if (k == LEFT_MAIN || k == RIGHT_MAIN) muRoll += 0.55f * (c.parkingBrake ? 1.0f : Clampf(c.brakes, 0, 1));
        float muSide = k == TAIL ? 0.5f : 0.8f;
        if (k == TAIL) muRoll = 0.5f;
        float fl = -muRoll * fn * tanhf(vl / 0.3f);
        float fs = -muSide * fn * tanhf(vs / 0.4f);

        Vector3 F = Vector3Add(Vector3Scale(n, fn), Vector3Add(Vector3Scale(rollDir, fl), Vector3Scale(sideDir, fs)));
        Fw = Vector3Add(Fw, F);
        Tw = Vector3Add(Tw, Vector3CrossProduct(rw, F));
    }
    wheelsOnGround = wheels;
    onGround = wheels > 0;

    // ------------------------------------------------ интегрирование
    Vector3 acc = Vector3Scale(Fw, 1.0f / mass);
    float nzRaw = Vector3DotProduct(acc, up) / G;
    gLoad = Lerp(gLoad, nzRaw, fminf(dt * 15.0f, 1.0f));
    acc.y -= G;
    vel = Vector3Add(vel, Vector3Scale(acc, dt));
    pos = Vector3Add(pos, Vector3Scale(vel, dt));

    // Инерция меняется с массой топлива (пропорционально).
    const float massScale = mass / (t.emptyMass + t.payload + t.defaultFuel);
    Vector3 T = Vector3Add(Tb, Rotate(Tw, inv));
    const Vector3 I{t.inertia[0] * massScale, t.inertia[1] * massScale, t.inertia[2] * massScale};
    Vector3 Iw{I.x * omega.x, I.y * omega.y, I.z * omega.z};
    Vector3 gyro = Vector3CrossProduct(omega, Iw);
    omega.x += (T.x - gyro.x) / I.x * dt;
    omega.y += (T.y - gyro.y) / I.y * dt;
    omega.z += (T.z - gyro.z) / I.z * dt;

    Quaternion w{omega.x, omega.y, omega.z, 0};
    Quaternion dq = QuaternionMultiply(rot, w);
    rot.x += 0.5f * dq.x * dt;
    rot.y += 0.5f * dq.y * dt;
    rot.z += 0.5f * dq.z * dt;
    rot.w += 0.5f * dq.w * dt;
    rot = QuaternionNormalize(rot);

    // ------------------------------------------------ прочность
    const float gMax = t.kind == AircraftKind::Airliner ? 4.0f : 6.0f;
    if (gLoad > gMax || gLoad < -2.0f - (gMax - 4.0f) * 0.5f) {
        char buf[96];
        snprintf(buf, sizeof buf, L("Structural failure: overstress %.1f G", "Разрушение конструкции: перегрузка %.1f G"), gLoad);
        Crash(buf);
    } else if (ias * MS_TO_KT > t.vmo + 90.0f) {
        Crash(L("Structural failure: flutter (overspeed)", "Разрушение конструкции: флаттер (превышение скорости)"));
    }
}
