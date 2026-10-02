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
// iceLoss — насколько лёд на крыле уменьшает критический угол атаки, рад.
float LiftCoef(const AircraftType& t, float a, float ff, float* stallFactor, float iceLoss = 0.0f)
{
    const float as = StallAlpha(t, ff) - iceLoss, cl0 = Cl0(t, ff);
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
    ice = 0;
    for (float& b : brakeTemp) b = oat;
    for (bool& f : tireFlat) f = false;
    plugHeat_[0] = plugHeat_[1] = 0;
    crashed = false;
    crashFire = true;
    crashReason.clear();
    for (bool& l : lost) l = false;
    for (Debris& d : debris) d = Debris{};
    overstressed = false;
    peakG = minG = 1;
    damage.clear();
    belly_ = false;
    ultimateK_ = 1.5f * (1.0f + 0.1f * (float)GetRandomValue(0, 100) / 100.0f);
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
    float clmax = (Cl0(t, ff) + CL_ALPHA * (StallAlpha(t, ff) - IceStallLossDeg() * DEG2RAD)) * (1.0f - 0.1f * ice);
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
                                     bool gearDown, float gammaDeg, float rhoScale)
{
    const float rho = AirDensity(altM) * rhoScale, qd = 0.5f * rho * tas * tas;
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

float Aircraft::Density(float altM) const
{
    float tIsa = 288.15f - 0.0065f * Clampf(altM, -500.0f, 11000.0f);
    return AirDensity(altM) * (qnh / 1013.25f) * tIsa / (tIsa + isaDev);
}

float Aircraft::DensityAltFt() const
{
    // Обратная формула стандартной атмосферы: rho = 1.225·(1 − 2.25577e-5·h)^4.2559.
    float rho = Density(pos.y);
    return (1.0f - powf(rho / 1.225f, 1.0f / 4.2559f)) / 2.25577e-5f * M_TO_FT;
}

void Aircraft::Crash(const std::string& reason, bool fire)
{
    if (crashed) return;
    crashed = true;
    crashFire = fire;
    crashReason = damage.empty() ? reason : damage + ". " + reason;
}

float Aircraft::LimitG() const
{
    switch (type_->kind) {
    case AircraftKind::Airliner: return 2.5f;   // транспортная категория
    case AircraftKind::Prop: return 3.8f;       // нормальная категория
    default: return 3.3f;
    }
}

float Aircraft::LimitNegG() const
{
    switch (type_->kind) {
    case AircraftKind::Airliner: return -1.0f;
    case AircraftKind::Prop: return -1.52f;
    default: return -1.3f;
    }
}

// Где крепится часть (центр отрывающегося куска), связанные оси.
static Vector3 PartCenter(AircraftKind k, Part p)
{
    float s = p == Part::WingL ? -1.0f : 1.0f;
    switch (k) {
    case AircraftKind::Prop:
        return p == Part::Tail ? Vector3{-4.85f, 0.3f, 0} : Vector3{-0.3f, 0.95f, s * 2.75f};
    case AircraftKind::Airliner:
        return p == Part::Tail ? Vector3{-16.5f, 0.6f, 0} : Vector3{-3.0f, -1.6f, s * 8.5f};
    default:
        return p == Part::Tail ? Vector3{-7.5f, 3.42f, 0} : Vector3{-0.4f, -0.6f, s * 3.95f};
    }
}

// Отрыв части конструкции. Самолёт не взрывается: он продолжает лететь (точнее, падать)
// с тем, что осталось, а оторванная часть кувыркается отдельно.
void Aircraft::Separate(Part p, const std::string& why)
{
    if (crashed || lost[(int)p]) return;
    lost[(int)p] = true;
    if (damage.empty()) damage = why;

    const AircraftType& t = *type_;
    Debris& d = debris[(int)p];
    d = Debris{};
    d.active = true;
    d.part = p;
    d.center = PartCenter(t.kind, p);
    Vector3 r = Rotate(d.center, rot);
    Vector3 omegaW = Rotate(omega, rot);
    d.pos = Vector3Add(pos, r);
    d.vel = Vector3Add(vel, Vector3CrossProduct(omegaW, r));
    d.rot = rot;
    float side = p == Part::WingL ? -1.0f : (p == Part::WingR ? 1.0f : 0.0f);
    // Крыло складывается по направлению нагрузки и уходит назад потоком.
    float load = Clampf(gLoad, -3.0f, 4.0f);
    d.vel = Vector3Add(d.vel, Vector3Add(Vector3Scale(Up(), 2.0f * load), Vector3Scale(Right(), side * 2.5f)));
    d.vel = Vector3Add(d.vel, Vector3Scale(Forward(), -4.0f));
    Vector3 tumble{(float)GetRandomValue(-100, 100), (float)GetRandomValue(-100, 100), (float)GetRandomValue(-100, 100)};
    d.omega = Vector3Add(omegaW, Vector3Scale(tumble, 0.03f));
    d.omega = Vector3Add(d.omega, Vector3Scale(Forward(), -side * 2.5f * Sign(load)));
    d.burn = p == Part::Tail ? 0.0f : 25.0f;

    if (p != Part::Tail) {
        // В крыле — топливные баки: половина топлива потеряна.
        fuel *= 0.5f;
        // Двигатели на крыле уходят вместе с ним.
        for (int i = 0; i < t.engineCount; ++i)
            if (t.enginePos[i].z * side > 1.0f && fabsf(t.enginePos[i].y) < 4.0f && t.kind == AircraftKind::Airliner) {
                engines[i].failed = true;
                engines[i].thrust = 0;
                engines[i].n1 = 0;
            }
    }
}

void Aircraft::UpdateDebris(float dt, const Terrain& terrain)
{
    for (Debris& d : debris) {
        if (!d.active) continue;
        d.burn = fmaxf(d.burn - dt, 0.0f);
        if (d.landed) continue;
        // Плоский кусок крыла быстро тормозится воздухом (установившаяся скорость ~40 м/с).
        float v = Vector3Length(d.vel);
        Vector3 drag = Vector3Scale(d.vel, -0.006f * v);
        d.vel = Vector3Add(d.vel, Vector3Scale(Vector3Add(drag, {0, -G, 0}), dt));
        d.pos = Vector3Add(d.pos, Vector3Scale(d.vel, dt));
        float w = Vector3Length(d.omega);
        if (w > 1e-4f) {
            Quaternion dq = QuaternionFromAxisAngle(Vector3Scale(d.omega, 1.0f / w), w * dt);
            d.rot = QuaternionNormalize(QuaternionMultiply(dq, d.rot));
        }
        float ground = terrain.SurfaceHeight(d.pos.x, d.pos.z);
        if (terrain.IsWater(d.pos.x, d.pos.z)) ground -= 1.5f;   // тонет
        if (d.pos.y < ground + 0.3f) {
            d.pos.y = ground + 0.3f;
            // Ложится плашмя.
            Vector3 up = Rotate({0, 1, 0}, d.rot);
            Quaternion flat = QuaternionFromVector3ToVector3(up, up.y >= 0 ? Vector3{0, 1, 0} : Vector3{0, -1, 0});
            d.rot = QuaternionNormalize(QuaternionMultiply(flat, d.rot));
            d.vel = {};
            d.omega = {};
            d.landed = true;
        }
    }
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
    if (t.jet && c.antiIce) raw *= 0.96f;              // отбор воздуха на обогрев
    if (!t.jet) raw *= 1.0f - 0.18f * ice;             // лёд на винте и в воздухозаборнике
    if (running && e.n1 < t.idleN1 - 1.0f) raw *= Clampf(e.n1 / t.idleN1, 0, 1);
    // Реверс: створки разворачивают часть струи вперёд.
    e.thrust = raw * (1.0f - reverser) - raw * reverser * 0.45f;

    if (running) {
        float burn = t.jet ? t.fuelFlowCoef * fmaxf(raw, t.maxThrust / t.engineCount * 0.04f)
                           : t.fuelFlowCoef * t.propPower * (0.05f + 0.95f * frac);
        fuelFlow += burn * (t.jet && c.antiIce ? 1.05f : 1.0f);
    }
    return e.thrust;
}

void Aircraft::Step(float dt, const Controls& c, const Terrain& terrain, Vector3 wind)
{
    const AircraftType& t = *type_;
    UpdateDebris(dt, terrain);
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
    const float rho = Density(pos.y);
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
            // Огонь прожигает конструкцию рядом с двигателем.
            if (t.kind == AircraftKind::Prop) {
                Crash(L("Engine fire spread to the cabin", "Пожар двигателя перекинулся в кабину"));
                return;
            }
            Part burnt = t.kind == AircraftKind::Airliner ? (t.enginePos[i].z < 0 ? Part::WingL : Part::WingR) : Part::Tail;
            Separate(burnt, t.kind == AircraftKind::Airliner
                                ? L("Engine fire burned through the wing", "Пожар двигателя прожёг крыло — крыло отломилось")
                                : L("Engine fire burned through the tail", "Пожар двигателя прожёг хвост — оперение отломилось"));
        }
    }
    if (failures_[(int)Failure::FuelLeak]) fuelFlow += t.maxFuel * 0.004f;
    fuel = fmaxf(fuel - fuelFlow * dt, 0.0f);

    // ------------------------------------------------ обледенение
    // В облаке при температуре от +2 до −25 °C на передней кромке нарастает лёд (сильнее всего около −6 °C,
    // в переохлаждённом дожде — вдвое быстрее). Обогрев крыла его срывает; у «Сессны» обогрева крыла нет.
    {
        lastAntiIce = c.antiIce;
        const bool deice = c.antiIce && t.kind != AircraftKind::Prop;
        if (Icing() && !deice) {
            float k = Clampf(1.0f - fabsf(oat + 6.0f) / 20.0f, 0.25f, 1.0f) * (inRain ? 2.0f : 1.0f);
            ice += k * Clampf(V / 80.0f, 0.3f, 1.5f) * dt / 300.0f;
        }
        if (deice) ice -= dt / 20.0f;
        if (oat > 1.0f) ice -= (oat - 1.0f) * dt / 150.0f;   // тает в тёплом воздухе
        else if (!inCloud) ice -= dt / 900.0f;               // медленно испаряется вне облаков
        ice = Clampf(ice, 0.0f, 1.0f);
    }

    // ------------------------------------------------ аэродинамика
    alpha = V > 1.0f ? atan2f(-vb.y, vb.x) : 0.0f;
    beta = V > 1.0f ? asinf(Clampf(vb.z / V, -1, 1)) : 0.0f;

    const float ff = FlapFrac(t, flaps);
    const float iceLoss = IceStallLossDeg() * DEG2RAD;
    stallAlpha = StallAlpha(t, ff) - iceLoss;
    const float Vd = fmaxf(V, 10.0f);
    const float p = omega.x, q = omega.z, r = -omega.y;
    const float ph = p * t.span / (2 * Vd), qh = q * t.chord / (2 * Vd), rh = r * t.span / (2 * Vd);
    const float elevEff = elevator + Clampf(c.trim, -1, 1) * trimRange;

    // Влияние земли: меньше индуктивное сопротивление на высоте меньше размаха.
    float wingAgl = fmaxf(pos.y - t.restHeight * 0.7f - terrain.SurfaceHeight(pos.x, pos.z), 0.0f);
    float ge = 16.0f * wingAgl / t.span;
    float groundEffect = (ge * ge) / (1.0f + ge * ge);

    float stall = 0;
    float cl = LiftCoef(t, alpha, ff, &stall, iceLoss);
    cl *= 1.0f - 0.1f * ice;
    cl += 4.0f * qh + 0.35f * elevEff;
    cl *= 1.0f - 0.15f * speedbrake * (onGround ? 3.0f : 1.0f);   // на земле интерцепторы «гасят» подъёмную силу
    cl *= 1.0f + 0.08f * (1.0f - groundEffect);

    const float aspect = t.span * t.span / t.wingArea;
    const float kInduced = 1.0f / (PI * 0.8f * aspect);
    float sa = sinf(alpha);
    float cd = t.cd0 + kInduced * cl * cl * (0.3f + 0.7f * groundEffect)
             + t.cdFlapsLin * ff + t.cdFlapsSq * ff * ff + t.cdGear * gear + 0.035f * speedbrake + 0.025f * ice
             + 0.6f * beta * beta
             + 1.2f * sa * sa * SmoothStep(stallAlpha - 3.0f * DEG2RAD, stallAlpha + 8.0f * DEG2RAD, fabsf(alpha));
    if (mach > 0.72f) cd += 25.0f * Sq(mach - 0.72f);   // волновой кризис

    float cy = -0.75f * beta - 0.12f * rudder;

    // Сваливание: демпфирование крена пропадает (сваливание на крыло, вход в штопор).
    if (stall > 0.3f && stallDrop_ == 0.0f) stallDrop_ = (GetRandomValue(0, 1) ? 1.0f : -1.0f);
    if (stall < 0.05f) stallDrop_ = 0.0f;
    // Повреждения: оторванное крыло не создаёт подъёмной силы, оставшееся переворачивает самолёт.
    const float wl = lost[(int)Part::WingL] ? 0.0f : 1.0f, wr = lost[(int)Part::WingR] ? 0.0f : 1.0f;
    const float wings = 0.5f * (wl + wr);
    const bool tailGone = lost[(int)Part::Tail];

    float clRoll = -0.09f * beta + wings * (t.clAileron * aileron - t.clp * (1.0f - 1.4f * stall) * ph) + 0.12f * rh
                 + 0.035f * stall * stallDrop_ * wings;
    float cm = tailGone
        // Без стабилизатора самолёт статически неустойчив и резко опускает нос.
        ? t.cm0 - 0.12f + 0.4f * t.cmAlpha * sinf(alpha) - 0.1f * t.cmq * qh - t.cmFlaps * ff
        : t.cm0 - t.cmAlpha * sinf(alpha) + t.cmElev * elevEff - t.cmq * qh - t.cmFlaps * ff - 0.12f * stall;
    float cn = t.cnBeta * beta + t.cnRudder * rudder - t.cnr * rh - 0.03f * ph - 0.012f * aileron;

    if (V > 0.1f) {
        Vector3 vxy{vb.x, vb.y, 0};
        if (Vector3Length(vxy) > 0.01f) {
            Vector3 liftDir = Vector3Normalize(Vector3CrossProduct({0, 0, 1}, vxy));
            Fb = Vector3Add(Fb, Vector3Scale(liftDir, qd * t.wingArea * cl * (0.06f + 0.94f * wings)));
        }
        Fb = Vector3Add(Fb, Vector3Scale(vb, -qd * t.wingArea * cd / V));
        if (wl != wr) {
            // Подъёмная сила и сопротивление одного крыла приложены сбоку от фюзеляжа.
            float arm = t.span * 0.22f;
            float wingLift = qd * t.wingArea * cl * 0.47f;
            float wingDrag = qd * t.wingArea * cd * 0.4f;
            Tb.x += arm * wingLift * (wl - wr);
            Tb.y += -arm * wingDrag * (wr - wl);
        }
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
        if (k == WING_L && lost[(int)Part::WingL]) continue;
        if (k == WING_R && lost[(int)Part::WingR]) continue;
        if (t.kind == AircraftKind::Airliner && ((k == ENGINE_L && lost[(int)Part::WingL]) || (k == ENGINE_R && lost[(int)Part::WingR])))
            continue;
        if (isWheel && gear < 0.98f) {
            wheelContact_[k] = false;
            continue;
        }
        Vector3 local = t.contacts[k];
        // Спущенная шина: колесо стоит на ободе — ниже и с сильным трением.
        if ((k == LEFT_MAIN || k == RIGHT_MAIN) && tireFlat[k - LEFT_MAIN]) local.y += t.restHeight * 0.07f;
        Vector3 rw = Rotate(local, rot);
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
        bool skid = false;   // контакт без разрушения: скольжение с трением
        const float impact = Vector3Length(vel);
        if (k == WING_L || k == WING_R) {
            if (groundSpeed > 12.0f || vn < -3.0f) {
                // Крыло цепляет землю и отламывается; самолёт разворачивает в сторону удара.
                float side = k == WING_L ? -1.0f : 1.0f;
                Separate(k == WING_L ? Part::WingL : Part::WingR,
                         k == WING_L ? L("Left wing hit the ground and broke off", "Левое крыло задело землю и отломилось")
                                     : L("Right wing hit the ground and broke off", "Правое крыло задело землю и отломилось"));
                vel = Vector3Scale(vel, 0.88f);
                omega.y -= side * 0.5f;
                omega.x *= 0.5f;
                continue;
            }
            skid = true;
        } else if (k == ENGINE_L || k == ENGINE_R) {
            // Удар винтом/гондолой: двигатель разрушен, но это ещё не катастрофа.
            if (vn < -6.0f && t.kind == AircraftKind::Airliner) {
                Separate(k == ENGINE_L ? Part::WingL : Part::WingR,
                         L("Engine pod hit the ground, wing torn off", "Двигатель ударился о землю и оторвал крыло"));
                continue;
            }
            int e = t.engineCount > 1 ? k - ENGINE_L : 0;
            if (!engines[e].failed) {
                Fail(e == 0 ? Failure::Engine1 : Failure::Engine2);
                if (damage.empty())
                    damage = t.jet ? L("Engine pod strike", "Удар гондолой двигателя") : L("Propeller strike — engine stopped", "Удар винтом — двигатель остановился");
            }
            skid = true;
        } else if (k == BELLY || k == NOSE_CONE) {
            // Мягкое касание фюзеляжем — посадка на брюхо, самолёт скользит; жёсткое — катастрофа.
            if (vn > -3.0f && impact < 75.0f) {
                skid = true;
                belly_ = true;
                if (damage.empty())
                    damage = gear < 0.98f ? L("Belly landing — gear not down", "Посадка на брюхо — шасси не выпущено")
                                          : L("Fuselage scraped the ground", "Фюзеляж задел землю");
            }
        }
        if (!isWheel && k != TAIL && !skid) {
            char buf[160];
            const char* what = k == BELLY ? (gear < 0.98f ? L("Gear-up belly impact", "Посадка на брюхо — шасси не выпущено")
                                                          : L("Fuselage impact", "Удар фюзеляжем"))
                             : k == NOSE_CONE ? L("Nose impact", "Удар носом")
                             : (k == WING_L || k == WING_R) ? L("Wingtip strike", "Удар законцовкой крыла")
                             : (t.jet ? L("Engine pod strike", "Удар гондолой двигателя") : L("Propeller strike", "Удар винтом о землю"));
            snprintf(buf, sizeof buf, "%s: %.0f kt, %.0f fpm", what, impact * MS_TO_KT, vn * MS_TO_FPM);
            Crash(buf, impact > 20.0f || vn < -6.0f);
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

        bool hard = k == TAIL || skid;   // жёсткий контакт: хвостовая пята, фюзеляж, законцовка
        float kSpring = k == NOSE_WHEEL ? t.springNose : (hard ? t.springMain * 2.0f : t.springMain);
        float cDamp = k == NOSE_WHEEL ? t.dampNose : (hard ? t.dampMain * 1.6f : t.dampMain);
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
        if (k == LEFT_MAIN || k == RIGHT_MAIN) {
            const int side = k - LEFT_MAIN;
            // Перегретые тормоза слабеют (fade).
            float fade = 1.0f - 0.5f * SmoothStep(450.0f, 900.0f, brakeTemp[side]);
            float brake = 0.55f * fade * (c.parkingBrake ? 1.0f : Clampf(c.brakes, 0, 1));
            muRoll += brake;
            if (tireFlat[side]) muRoll += 0.25f;
            // Вся работа торможения уходит в тепло тормозных дисков.
            float heatCap = 2.8f * (t.emptyMass + t.payload + t.defaultFuel);   // Дж/К на одну сторону
            brakeTemp[side] += brake * fn * fabsf(tanhf(vl / 0.3f) * vl) * dt / heatCap;
        }
        float muSide = hard ? 0.5f : 0.8f;
        if (hard) muRoll = 0.5f;
        float fl = -muRoll * fn * tanhf(vl / 0.3f);
        float fs = -muSide * fn * tanhf(vs / 0.4f);

        Vector3 F = Vector3Add(Vector3Scale(n, fn), Vector3Add(Vector3Scale(rollDir, fl), Vector3Scale(sideDir, fs)));
        Fw = Vector3Add(Fw, F);
        Tw = Vector3Add(Tw, Vector3CrossProduct(rw, F));
    }
    wheelsOnGround = wheels;
    onGround = wheels > 0;
    // Остывание тормозов (быстрее на ходу, медленно в убранной нише шасси) и плавкие пробки:
    // при перегреве они выпускают воздух из шины, чтобы она не взорвалась.
    for (int side = 0; side < 2; ++side) {
        float cool = (gear > 0.5f ? 1.0f / 600.0f : 1.0f / 1500.0f) + groundSpeed * 0.00006f;
        brakeTemp[side] -= (brakeTemp[side] - oat) * cool * dt;
        // Пробки плавятся не сразу: тепло от дисков доходит до колеса за полминуты.
        plugHeat_[side] = brakeTemp[side] > FUSE_PLUG ? plugHeat_[side] + dt : fmaxf(plugHeat_[side] - dt, 0.0f);
        if (plugHeat_[side] > 25.0f && t.retractableGear) tireFlat[side] = true;
    }
    // Повреждённый самолёт остановился на земле — полёт окончен, но без взрыва.
    if ((belly_ || Broken() || damage.size()) && Vector3Length(vel) < 0.8f && (onGround || belly_)
        && pos.y - terrain.SurfaceHeight(pos.x, pos.z) < t.restHeight + 1.0f) {
        Crash(L("The aircraft came to a stop. It is damaged, but everyone survived",
                "Самолёт остановился. Он повреждён, но все живы"), false);
        return;
    }

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
    // Выше эксплуатационной перегрузки — остаточная деформация (самолёт летит, но его надо осматривать).
    // В 1.5 раза выше (разрушающая) — крыло отламывается. Самолёт при этом не взрывается, а падает.
    if (wheelsOnGround == 0 && !belly_) {
        peakG = fmaxf(peakG, gLoad);
        minG = fminf(minG, gLoad);
        if (gLoad > LimitG() || gLoad < LimitNegG()) overstressed = true;
        if (gLoad > LimitG() * ultimateK_ || gLoad < LimitNegG() * ultimateK_) {
            // Ломается более нагруженное крыло: при крене с перегрузкой — поднимающееся.
            Part w = omega.x > 0.05f ? Part::WingL : omega.x < -0.05f ? Part::WingR : (GetRandomValue(0, 1) ? Part::WingL : Part::WingR);
            if (lost[(int)w]) w = w == Part::WingL ? Part::WingR : Part::WingL;
            char buf[160];
            snprintf(buf, sizeof buf,
                     w == Part::WingL ? L("Left wing failed under %.1f G (limit %.1f G)", "Левое крыло сломалось от перегрузки %.1f G (предел %.1f G)")
                                      : L("Right wing failed under %.1f G (limit %.1f G)", "Правое крыло сломалось от перегрузки %.1f G (предел %.1f G)"),
                     gLoad, gLoad > 0 ? LimitG() : LimitNegG());
            Separate(w, buf);
        }
    }
    if (ias * MS_TO_KT > t.vmo + 90.0f && !lost[(int)Part::Tail])
        Separate(Part::Tail, L("Flutter: the tailplane broke off (overspeed)", "Флаттер: оторвало стабилизатор (превышение скорости)"));
}
