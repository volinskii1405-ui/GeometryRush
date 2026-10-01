#pragma once
#include "AircraftType.h"

#include <string>

class Terrain;

// Положение органов управления в кабине (то, что двигает пилот).
struct Controls {
    float pitch = 0;     // штурвал: +1 — на себя (нос вверх), -1 — от себя
    float roll = 0;      // +1 — крен вправо
    float yaw = 0;       // педали: +1 — правая
    float throttle = 0;  // РУД 0..1
    float trim = 0;      // триммер руля высоты -1..1 (+ — на кабрирование)
    int flapsLever = 0;  // 0..3
    bool gearDown = true;
    float brakes = 0;    // тормоза колёс 0..1
    bool parkingBrake = false;
    bool speedbrake = false;
    bool reverse = false;      // реверс тяги (только на земле)
    bool altGear = false;      // аварийный выпуск шасси (под собственным весом)
    bool fireHandle[2] = {false, false};   // пожарный кран: выключить двигатель и разрядить огнетушитель
    bool antiIce = false;      // противообледенительная система (крыло, двигатели, ПВД)
};

enum class Failure { Engine1, Engine2, Fire1, Fire2, GearHydraulics, FlapsJam, FuelLeak, Count };

// Части, которые могут оторваться: крылья и горизонтальное оперение.
enum class Part { WingL, WingR, Tail, Count };

// Оторвавшаяся часть летит сама по себе, кувыркаясь, пока не упадёт на землю.
struct Debris {
    bool active = false;
    bool landed = false;
    Part part = Part::WingL;
    Vector3 center{};          // где была закреплена, связанные оси самолёта
    Vector3 pos{}, vel{};      // мир
    Quaternion rot{0, 0, 0, 1};
    Vector3 omega{};           // рад/с, мир
    float burn = 0;            // сколько ещё дымит топливо из разорванного бака, с
};

struct Engine {
    float n1 = 0;          // обороты, % (у винта — мощность)
    float thrust = 0;      // Н (отрицательная — реверс)
    bool failed = false;   // отказ: двигатель остановился
    bool fire = false;     // пожар
    bool shutdown = false; // выключен пожарным краном
    float fireTime = 0;    // сколько горит, с
    float extinguish = 0;  // таймер тушения
    bool Running() const { return !failed && !shutdown; }
};

class Aircraft {
public:
    void SetType(AircraftKind k) { type_ = &GetAircraftType(k); }
    const AircraftType& Type() const { return *type_; }

    // pitchDeg — тангаж, gammaDeg — наклон траектории (для старта в воздухе).
    void Reset(Vector3 position, float headingDeg, float speedMs, bool onGround, const Controls& c,
               float pitchDeg = 0.0f, float gammaDeg = 0.0f, float fuelKg = -1.0f);
    void Step(float dt, const Controls& c, const Terrain& terrain, Vector3 wind);
    void Fail(Failure f);
    bool Failed(Failure f) const { return failures_[(int)f]; }

    // ---- состояние
    Vector3 pos{};            // центр масс, мир
    Vector3 vel{};            // м/с, мир
    Quaternion rot{0, 0, 0, 1}; // связанная → мир
    Vector3 omega{};          // рад/с, в связанных осях (x — крен вправо, z — нос вверх, -y — нос вправо)

    Engine engines[2];
    float fuel = 0;           // кг
    float fuelFlow = 0;       // кг/с
    float reverser = 0;       // 0 — убран, 1 — раскрыт
    float elevator = 0, aileron = 0, rudder = 0; // фактические отклонения, рад
    float flaps = 0;          // фактический угол закрылков, град
    float gear = 1;           // 0 — убрано, 1 — выпущено и на замках
    float speedbrake = 0;     // 0..1
    float noseSteer = 0;      // рад

    // ---- производные величины (обновляются на каждом шаге)
    float alpha = 0, beta = 0;    // рад
    float ias = 0, tas = 0, mach = 0; // м/с, м/с, M
    float gLoad = 1;              // перегрузка по нормали
    float thrust = 0;             // Н, суммарная
    float stallAlpha = 0;         // текущий критический угол атаки, рад
    bool onGround = false;        // обжаты стойки шасси
    int wheelsOnGround = 0;

    // ---- окружающая среда (задаёт вызывающий код каждый кадр)
    float oat = 15;              // температура наружного воздуха, °C
    bool inCloud = false, inRain = false;
    bool lastAntiIce = false;    // положение выключателя на последнем шаге (для сигнализации)

    // ---- обледенение и тормоза
    float ice = 0;               // 0..1: толщина льда на крыле (1 — сильное обледенение)
    float brakeTemp[2] = {15, 15};   // °C, левые и правые колёса
    bool tireFlat[2] = {false, false};
    float IceStallLossDeg() const { return ice * 6.0f; }
    bool Icing() const { return inCloud && oat < 2.0f && oat > -25.0f; }
    static constexpr float BRAKES_HOT = 300.0f, FUSE_PLUG = 550.0f;

    // ---- повреждения конструкции
    bool lost[(int)Part::Count] = {};   // оторвалось
    bool overstressed = false;   // превышена эксплуатационная перегрузка: остаточная деформация
    float peakG = 1, minG = 1;   // максимальная и минимальная перегрузка за полёт
    std::string damage;          // что и почему разрушилось (первое событие)
    Debris debris[(int)Part::Count];
    bool Broken() const { return lost[0] || lost[1] || lost[2]; }
    float LimitG() const;        // эксплуатационная перегрузка (+); разрушающая — в 1.5 раза больше
    float LimitNegG() const;

    // ---- события
    bool crashed = false;
    bool crashFire = true;       // пожар после удара (при мягком ударе — просто остановился)
    std::string crashReason;
    bool touchdown = false;       // касание в этом кадре (сбрасывает вызывающий код)
    float touchdownFpm = 0;       // вертикальная скорость при касании
    bool tailStrike = false;

    Vector3 Forward() const { return fs::Rotate({1, 0, 0}, rot); }
    Vector3 Up() const { return fs::Rotate({0, 1, 0}, rot); }
    Vector3 Right() const { return fs::Rotate({0, 0, 1}, rot); }
    Vector3 ToWorld(Vector3 local) const { return Vector3Add(pos, fs::Rotate(local, rot)); }

    float Mass() const { return type_->emptyMass + type_->payload + fuel; }
    float PitchDeg() const;
    float BankDeg() const;
    float HeadingDeg() const;
    float N1() const;             // средние обороты работающих двигателей
    bool AnyEngineFire() const { return engines[0].fire || engines[1].fire; }

    // Балансировочный режим для горизонтального/наклонного полёта (для старта в воздухе).
    struct Trim { float alphaDeg, trim, throttle; };
    static Trim ComputeTrim(const AircraftType& t, float mass, float tasMs, float altM, float flapsDeg,
                            bool gearDown, float gammaDeg);

    float StallSpeedKt() const;   // скорость сваливания (приборная) при текущих массе и закрылках
    float SpeedLimitKt() const;   // текущее ограничение: VMO / VFE / VLE

private:
    void Crash(const std::string& reason, bool fire = true);
    void Separate(Part p, const std::string& why);
    void UpdateDebris(float dt, const Terrain& terrain);
    float EngineThrust(int i, float V, float rho, float dt, const Controls& c);
    const AircraftType* type_ = &GetAircraftType(AircraftKind::LightJet);
    bool wheelContact_[3] = {false, false, false};
    bool failures_[(int)Failure::Count] = {};
    float stallDrop_ = 0;
    float ultimateK_ = 1.5f;   // разрушающая / эксплуатационная (немного разная от полёта к полёту)
    bool belly_ = false;       // скольжение на брюхе/крыле
    float plugHeat_[2] = {0, 0};   // сколько секунд колесо выше температуры плавления пробок   // в какую сторону сваливается крыло при срыве
};
