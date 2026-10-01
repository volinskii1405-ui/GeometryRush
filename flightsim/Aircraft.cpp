#include "Aircraft.h"
#include "Terrain.h"

#include <cstdio>

using namespace fs;

namespace {

// Точки касания (связанные оси). Первые три — колёса шасси.
enum ContactKind { NOSE_WHEEL, LEFT_MAIN, RIGHT_MAIN, TAIL, BELLY, NOSE_CONE, WING_L, WING_R, ENGINE_L, ENGINE_R, CONTACT_COUNT };
const Vector3 kContacts[CONTACT_COUNT] = {
    {4.9f, -1.9f, 0.0f},  {-0.6f, -1.9f, -1.8f}, {-0.6f, -1.9f, 1.8f},
    {-7.4f, -0.4f, 0.0f}, {0.0f, -1.05f, 0.0f},  {7.4f, -0.2f, 0.0f},
    {-1.8f, -0.15f, -7.9f}, {-1.8f, -0.15f, 7.9f},
    {-3.6f, 0.1f, -2.0f}, {-3.6f, 0.1f, 2.0f},
};

float FlapFrac(float flapsDeg) { return flapsDeg / ac::FLAP_DEG[3]; }

float StallAlpha(float flapFrac) { return (15.0f - 2.5f * flapFrac) * DEG2RAD; }
float Cl0(float flapFrac) { return 0.10f + 0.60f * flapFrac; }
constexpr float CL_ALPHA = 5.0f;   // 1/рад
constexpr float NEG_STALL = -11.0f * DEG2RAD;

// Коэффициент подъёмной силы по углу атаки: линейный участок, срыв, «плоская пластина».
float LiftCoef(float a, float ff, float* stallFactor)
{
    const float as = StallAlpha(ff), cl0 = Cl0(ff);
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

} // namespace

void Aircraft::Reset(Vector3 position, float headingDeg, float speedMs, bool ground, const Controls& c,
                     float pitchDeg, float gammaDeg)
{
    pos = position;
    // Курс 0 — север (-Z), 90 — восток (+X). Нос самолёта — ось +X связанной системы.
    Quaternion yaw = QuaternionFromAxisAngle({0, 1, 0}, (90.0f - headingDeg) * DEG2RAD);
    rot = QuaternionMultiply(yaw, QuaternionFromAxisAngle({0, 0, 1}, pitchDeg * DEG2RAD));
    Vector3 flat = Rotate({1, 0, 0}, yaw);
    float g = gammaDeg * DEG2RAD;
    vel = Vector3Scale(Vector3Add(Vector3Scale(flat, cosf(g)), {0, sinf(g), 0}), speedMs);
    omega = {};
    n1 = 22.0f + 78.0f * c.throttle;
    elevator = aileron = rudder = noseSteer = 0;
    flaps = ac::FLAP_DEG[c.flapsLever];
    gear = c.gearDown ? 1.0f : 0.0f;
    speedbrake = c.speedbrake ? 1.0f : 0.0f;
    alpha = beta = 0;
    gLoad = 1;
    onGround = ground;
    wheelsOnGround = ground ? 3 : 0;
    for (bool& w : wheelContact_) w = ground;
    crashed = false;
    crashReason.clear();
    touchdown = false;
    touchdownFpm = 0;
    tailStrike = false;
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

float Aircraft::StallSpeedKt() const
{
    float ff = FlapFrac(flaps);
    float clmax = Cl0(ff) + CL_ALPHA * StallAlpha(ff);
    return sqrtf(2.0f * ac::MASS * G / (1.225f * ac::WING_AREA * clmax)) * MS_TO_KT;
}

float Aircraft::SpeedLimitKt() const
{
    float lim = ac::VMO_KT;
    for (int i = 1; i < 4; ++i)
        if (flaps > ac::FLAP_DEG[i - 1] + 0.5f) lim = fminf(lim, ac::VFE_KT[i]);
    if (gear > 0.01f) lim = fminf(lim, ac::VLE_KT);
    return lim;
}

Aircraft::Trim Aircraft::ComputeTrim(float tas, float altM, float flapsDeg, bool gearDown, float gammaDeg)
{
    const float rho = AirDensity(altM), qd = 0.5f * rho * tas * tas;
    const float ff = FlapFrac(flapsDeg), W = ac::MASS * G, gam = gammaDeg * DEG2RAD;
    const float mach = tas / SpeedOfSound(altM);
    const float kInduced = 1.0f / (PI * 0.8f * ac::SPAN * ac::SPAN / ac::WING_AREA);
    float elevEff = 0, alpha = 0, thrust = 0;
    for (int it = 0; it < 8; ++it) {
        float clReq = W * cosf(gam) / (qd * ac::WING_AREA);
        alpha = (clReq - Cl0(ff) - 0.35f * elevEff) / CL_ALPHA;
        float cd = 0.021f + kInduced * clReq * clReq + 0.006f * ff + 0.045f * ff * ff + (gearDown ? 0.018f : 0.0f);
        thrust = qd * ac::WING_AREA * cd + W * sinf(gam);
        elevEff = (sinf(alpha) - 0.06f + 0.06f * ff + 0.4f * thrust / (qd * ac::WING_AREA * ac::CHORD)) / 1.3f;
    }
    float avail = ac::THRUST_MAX * powf(rho / 1.225f, 0.75f) * (1.0f - 0.3f * mach);
    float f = Clampf((thrust / avail - 0.05f) / 0.95f, 0.0f, 1.0f);
    return Trim{alpha * RAD2DEG, Clampf(elevEff / (10.0f * DEG2RAD), -1, 1), powf(f, 2.0f / 3.0f)};
}

void Aircraft::Crash(const std::string& reason)
{
    if (crashed) return;
    crashed = true;
    crashReason = reason;
}

void Aircraft::Step(float dt, const Controls& c, const Terrain& terrain, Vector3 wind)
{
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

    // ------------------------------------------------ приводы органов управления
    elevator = MoveTowards(elevator, Clampf(c.pitch, -1, 1) * ac::MAX_ELEVATOR, 60.0f * DEG2RAD * dt);
    aileron = MoveTowards(aileron, Clampf(c.roll, -1, 1) * ac::MAX_AILERON, 70.0f * DEG2RAD * dt);
    rudder = MoveTowards(rudder, Clampf(c.yaw, -1, 1) * ac::MAX_RUDDER, 50.0f * DEG2RAD * dt);
    flaps = MoveTowards(flaps, ac::FLAP_DEG[c.flapsLever], 2.5f * dt);
    speedbrake = MoveTowards(speedbrake, c.speedbrake ? 1.0f : 0.0f, dt / 1.5f);
    // Блокировка уборки шасси на земле (концевик обжатия стоек).
    float gearTarget = (c.gearDown || onGround) ? 1.0f : 0.0f;
    gear = MoveTowards(gear, gearTarget, dt / ac::GEAR_TIME);

    // ------------------------------------------------ двигатели
    float n1Target = 22.0f + 78.0f * Clampf(c.throttle, 0, 1);
    float spool = n1 < 50.0f ? 0.35f : 0.9f;   // на малом газе турбина раскручивается медленно
    n1 = MoveTowards(n1, n1Target, fminf(fabsf(n1Target - n1) * spool, 14.0f) * dt);

    const Quaternion inv = QuaternionInvert(rot);
    const Vector3 air = Vector3Subtract(vel, wind);
    const Vector3 vb = Rotate(air, inv);
    const float V = Vector3Length(vb);
    const float rho = AirDensity(pos.y);
    const float qd = 0.5f * rho * V * V;
    tas = V;
    ias = V * sqrtf(rho / 1.225f);
    mach = V / SpeedOfSound(pos.y);

    float n1Frac = Clampf((n1 - 22.0f) / 78.0f, 0, 1);
    thrust = ac::THRUST_MAX * (0.05f + 0.95f * powf(n1Frac, 1.5f)) * powf(rho / 1.225f, 0.75f) * (1.0f - 0.3f * mach);

    // ------------------------------------------------ аэродинамика
    alpha = V > 1.0f ? atan2f(-vb.y, vb.x) : 0.0f;
    beta = V > 1.0f ? asinf(Clampf(vb.z / V, -1, 1)) : 0.0f;

    const float ff = FlapFrac(flaps);
    stallAlpha = StallAlpha(ff);
    const float Vd = fmaxf(V, 10.0f);
    const float p = omega.x, q = omega.z, r = -omega.y;
    const float ph = p * ac::SPAN / (2 * Vd), qh = q * ac::CHORD / (2 * Vd), rh = r * ac::SPAN / (2 * Vd);
    const float elevEff = elevator + Clampf(c.trim, -1, 1) * 10.0f * DEG2RAD;

    // Влияние земли: меньше индуктивное сопротивление на высоте меньше размаха.
    float wingAgl = fmaxf(pos.y - 1.2f - terrain.SurfaceHeight(pos.x, pos.z), 0.0f);
    float ge = 16.0f * wingAgl / ac::SPAN;
    float groundEffect = (ge * ge) / (1.0f + ge * ge);

    float stall = 0;
    float cl = LiftCoef(alpha, ff, &stall);
    cl += 4.0f * qh + 0.35f * elevEff;
    cl *= 1.0f - 0.15f * speedbrake;
    cl *= 1.0f + 0.08f * (1.0f - groundEffect);

    const float aspect = ac::SPAN * ac::SPAN / ac::WING_AREA;
    const float kInduced = 1.0f / (PI * 0.8f * aspect);
    float sa = sinf(alpha);
    float cd = 0.021f + kInduced * cl * cl * (0.3f + 0.7f * groundEffect)
             + 0.006f * ff + 0.045f * ff * ff + 0.018f * gear + 0.035f * speedbrake
             + 0.6f * beta * beta
             + 1.2f * sa * sa * SmoothStep(stallAlpha - 3.0f * DEG2RAD, stallAlpha + 8.0f * DEG2RAD, fabsf(alpha));
    if (mach > 0.72f) cd += 25.0f * Sq(mach - 0.72f);   // волновой кризис

    float cy = -0.75f * beta - 0.12f * rudder;

    // Сваливание: демпфирование крена пропадает (сваливание на крыло, вход в штопор).
    if (stall > 0.3f && stallDrop_ == 0.0f) stallDrop_ = (GetRandomValue(0, 1) ? 1.0f : -1.0f);
    if (stall < 0.05f) stallDrop_ = 0.0f;
    float clRoll = -0.09f * beta + 0.11f * aileron - 0.48f * (1.0f - 1.4f * stall) * ph + 0.12f * rh
                 + 0.035f * stall * stallDrop_;
    float cm = 0.06f - 1.0f * sinf(alpha) + 1.3f * elevEff - 22.0f * qh - 0.06f * ff - 0.12f * stall;
    float cn = 0.11f * beta + 0.06f * rudder - 0.16f * rh - 0.03f * ph - 0.012f * aileron;

    Vector3 Fb{thrust, 0, 0};
    Vector3 Tb{0, 0, -0.4f * thrust};   // двигатели на хвосте выше ЦМ: газ опускает нос
    if (V > 0.1f) {
        Vector3 vxy{vb.x, vb.y, 0};
        if (Vector3Length(vxy) > 0.01f) {
            Vector3 liftDir = Vector3Normalize(Vector3CrossProduct({0, 0, 1}, vxy));
            Fb = Vector3Add(Fb, Vector3Scale(liftDir, qd * ac::WING_AREA * cl));
        }
        Fb = Vector3Add(Fb, Vector3Scale(vb, -qd * ac::WING_AREA * cd / V));
        Fb.z += qd * ac::WING_AREA * cy;
        Tb.x += qd * ac::WING_AREA * ac::SPAN * clRoll;
        Tb.y += -qd * ac::WING_AREA * ac::SPAN * cn;
        Tb.z += qd * ac::WING_AREA * ac::CHORD * cm;
    }

    Vector3 Fw = Rotate(Fb, rot);           // без гравитации — для перегрузки
    Vector3 Tw{};                            // моменты от земли, мир

    // ------------------------------------------------ шасси и касание земли
    const Vector3 omegaW = Rotate(omega, rot);
    const Vector3 fwd = Forward();
    const Vector3 up = Up();
    float groundSpeed = sqrtf(vel.x * vel.x + vel.z * vel.z);
    float maxSteer = Lerp(45.0f, 6.0f, SmoothStep(5.0f, 30.0f, groundSpeed)) * DEG2RAD;
    noseSteer = MoveTowards(noseSteer, Clampf(c.yaw, -1, 1) * maxSteer, 60.0f * DEG2RAD * dt);

    int wheels = 0;
    bool anyContact = false;
    for (int k = 0; k < CONTACT_COUNT; ++k) {
        bool isWheel = k <= RIGHT_MAIN;
        if (isWheel && gear < 0.98f) {
            wheelContact_[k] = false;
            continue;
        }
        Vector3 rw = Rotate(kContacts[k], rot);
        Vector3 pw = Vector3Add(pos, rw);
        float ground = terrain.SurfaceHeight(pw.x, pw.z);
        float depth = ground - pw.y;
        if (depth <= 0.0f) {
            if (isWheel) wheelContact_[k] = false;
            continue;
        }
        anyContact = true;
        Vector3 vp = Vector3Add(vel, Vector3CrossProduct(omegaW, rw));
        Vector3 n = terrain.Normal(pw.x, pw.z);
        float vn = Vector3DotProduct(vp, n);
        bool water = terrain.IsWater(pw.x, pw.z);

        if (water) {
            Crash(V > 40.0f ? "Impact with the sea" : "Ditched in the sea");
            return;
        }
        if (!isWheel && k != TAIL) {
            char buf[96];
            const char* what = k == BELLY ? (gear < 0.98f ? "Gear-up belly impact" : "Fuselage impact")
                             : k == NOSE_CONE ? "Nose impact"
                             : (k == WING_L || k == WING_R) ? "Wingtip strike"
                             : "Engine pod strike";
            snprintf(buf, sizeof buf, "%s at %.0f kt, %.0f fpm", what, Vector3Length(vel) * MS_TO_KT, vn * MS_TO_FPM);
            Crash(buf);
            return;
        }
        if (k == TAIL) {
            if (vn < -3.0f || groundSpeed > 90.0f) {
                Crash("Tail impact");
                return;
            }
            tailStrike = true;
        }
        if (isWheel && !wheelContact_[k]) {
            if (wheelsOnGround == 0 && !touchdown) {
                touchdown = true;
                touchdownFpm = vn * MS_TO_FPM;
            }
            if (vn < -4.6f) {
                char buf[96];
                snprintf(buf, sizeof buf, "Gear collapsed: hard landing (%.0f fpm)", vn * MS_TO_FPM);
                Crash(buf);
                return;
            }
        }
        if (isWheel) {
            wheelContact_[k] = true;
            ++wheels;
        }

        float kSpring = k == NOSE_WHEEL ? 120000.0f : (k == TAIL ? 400000.0f : 200000.0f);
        float cDamp = k == NOSE_WHEEL ? 15000.0f : (k == TAIL ? 40000.0f : 25000.0f);
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
    (void)anyContact;

    // ------------------------------------------------ интегрирование
    Vector3 acc = Vector3Scale(Fw, 1.0f / ac::MASS);
    float nzRaw = Vector3DotProduct(acc, up) / G;
    gLoad = Lerp(gLoad, nzRaw, fminf(dt * 15.0f, 1.0f));
    acc.y -= G;
    vel = Vector3Add(vel, Vector3Scale(acc, dt));
    pos = Vector3Add(pos, Vector3Scale(vel, dt));

    Vector3 T = Vector3Add(Tb, Rotate(Tw, inv));
    const Vector3 I{ac::IXX, ac::IYY, ac::IZZ};
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
    if (gLoad > 6.0f || gLoad < -3.0f) {
        char buf[64];
        snprintf(buf, sizeof buf, "Structural failure: overstress %.1f G", gLoad);
        Crash(buf);
    } else if (ias * MS_TO_KT > ac::VMO_KT + 90.0f) {
        Crash("Structural failure: flutter (overspeed)");
    }
}
