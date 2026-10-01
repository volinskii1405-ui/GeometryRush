#pragma once
#include "FlightMath.h"

#include <string>

class Terrain;

// Положение органов управления в кабине (то, что двигает пилот).
struct Controls {
    float pitch = 0;     // штурвал: +1 — на себя (нос вверх), -1 — от себя
    float roll = 0;      // +1 — крен вправо
    float yaw = 0;       // педали: +1 — правая
    float throttle = 0;  // РУД 0..1
    float trim = 0;      // триммер руля высоты -1..1 (+ — на кабрирование)
    int flapsLever = 0;  // 0..3: UP / 10 / 20 / 35
    bool gearDown = true;
    float brakes = 0;    // тормоза колёс 0..1
    bool parkingBrake = false;
    bool speedbrake = false;
};

// Лёгкий двухдвигательный реактивный самолёт (масштаб бизнес-джета).
namespace ac {
constexpr float MASS = 7600.0f;            // кг
constexpr float WING_AREA = 30.0f;         // м²
constexpr float SPAN = 15.9f;              // м
constexpr float CHORD = 2.0f;              // м, средняя аэродинамическая хорда
constexpr float THRUST_MAX = 26000.0f;     // Н, оба двигателя на уровне моря
constexpr float IXX = 38000.0f;            // крен
constexpr float IYY = 100000.0f;            // рыскание
constexpr float IZZ = 74000.0f;            // тангаж

constexpr float VMO_KT = 260.0f;           // макс. эксплуатационная приборная скорость
constexpr float VLE_KT = 200.0f;           // с выпущенным шасси
constexpr float FLAP_DEG[4] = {0.0f, 10.0f, 20.0f, 35.0f};
constexpr float VFE_KT[4] = {VMO_KT, 200.0f, 180.0f, 160.0f};

constexpr float MAX_ELEVATOR = 25.0f * DEG2RAD;
constexpr float MAX_AILERON = 20.0f * DEG2RAD;
constexpr float MAX_RUDDER = 25.0f * DEG2RAD;
constexpr float GEAR_TIME = 6.0f;          // с, полный цикл выпуска/уборки
} // namespace ac

class Aircraft {
public:
    // pitchDeg — тангаж, gammaDeg — наклон траектории (для старта в воздухе).
    void Reset(Vector3 position, float headingDeg, float speedMs, bool onGround, const Controls& c,
               float pitchDeg = 0.0f, float gammaDeg = 0.0f);
    void Step(float dt, const Controls& c, const Terrain& terrain, Vector3 wind);

    // ---- состояние
    Vector3 pos{};            // центр масс, мир
    Vector3 vel{};            // м/с, мир
    Quaternion rot{0, 0, 0, 1}; // связанная → мир
    Vector3 omega{};          // рад/с, в связанных осях (x — крен вправо, z — нос вверх, -y — нос вправо)

    float n1 = 22.0f;         // обороты вентилятора, %
    float elevator = 0, aileron = 0, rudder = 0; // фактические отклонения, рад
    float flaps = 0;          // фактический угол закрылков, град
    float gear = 1;           // 0 — убрано, 1 — выпущено и на замках
    float speedbrake = 0;     // 0..1
    float noseSteer = 0;      // рад

    // ---- производные величины (обновляются на каждом шаге)
    float alpha = 0, beta = 0;    // рад
    float ias = 0, tas = 0, mach = 0; // м/с, м/с, M
    float gLoad = 1;              // перегрузка по нормали
    float thrust = 0;             // Н
    float stallAlpha = 0;         // текущий критический угол атаки, рад
    bool onGround = false;        // обжаты стойки шасси
    int wheelsOnGround = 0;

    // ---- события
    bool crashed = false;
    std::string crashReason;
    bool touchdown = false;       // касание в этом кадре (сбрасывает вызывающий код)
    float touchdownFpm = 0;       // вертикальная скорость при касании
    bool tailStrike = false;

    Vector3 Forward() const { return fs::Rotate({1, 0, 0}, rot); }
    Vector3 Up() const { return fs::Rotate({0, 1, 0}, rot); }
    Vector3 Right() const { return fs::Rotate({0, 0, 1}, rot); }
    Vector3 ToWorld(Vector3 local) const { return Vector3Add(pos, fs::Rotate(local, rot)); }

    float PitchDeg() const;
    float BankDeg() const;
    float HeadingDeg() const;
    // Балансировочный режим для горизонтального/наклонного полёта (для старта в воздухе).
    struct Trim { float alphaDeg, trim, throttle; };
    static Trim ComputeTrim(float tasMs, float altM, float flapsDeg, bool gearDown, float gammaDeg);

    float StallSpeedKt() const;   // скорость сваливания (приборная) при текущих массе и закрылках
    float SpeedLimitKt() const;   // текущее ограничение: VMO / VFE / VLE

private:
    void Crash(const std::string& reason);
    bool wheelContact_[3] = {false, false, false};
    float stallDrop_ = 0;   // в какую сторону сваливается крыло при срыве
};
