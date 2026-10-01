#pragma once
#include "FlightMath.h"

// Параметры типа самолёта. Аэродинамические коэффициенты безразмерные, поэтому
// одна и та же модель полёта работает и для «Сессны», и для лайнера.
enum class AircraftKind { LightJet, Prop, Airliner, Count };

// Точки касания (связанные оси). Первые три — колёса шасси.
enum ContactKind { NOSE_WHEEL, LEFT_MAIN, RIGHT_MAIN, TAIL, BELLY, NOSE_CONE, WING_L, WING_R, ENGINE_L, ENGINE_R, CONTACT_COUNT };

struct AircraftType {
    AircraftKind kind;
    const char* nameEn;
    const char* nameRu;

    // ---- масса, кг
    float emptyMass, payload, maxFuel, defaultFuel;

    // ---- геометрия, м
    float wingArea, span, chord;
    Vector3 contacts[CONTACT_COUNT];
    Vector3 eye;               // глаза пилота
    float restHeight;          // высота ЦМ над землёй на стоянке
    float inertia[3];          // крен, рыскание, тангаж, кг·м²

    // ---- силовая установка
    bool jet;
    int engineCount;
    Vector3 enginePos[2];
    float maxThrust;           // Н, все двигатели, у земли на месте
    float propPower;           // Вт (только винт)
    float thrustLineY;         // плечо тяги по высоте относительно ЦМ, м
    float idleN1;              // %
    float fuelFlowCoef;        // реактивный: кг/(Н·с); винт: кг/Дж
    bool hasReverser;

    // ---- шасси и механизация
    bool retractableGear;
    float gearTime;
    float flapDeg[4];
    const char* flapNames[4];
    float springMain, springNose, dampMain, dampNose;
    float gearCollapseMs;      // вертикальная скорость поломки шасси
    float maxSteerDeg;

    // ---- ограничения скорости, kt
    float vmo, vle;
    float vfe[4];

    // ---- аэродинамика
    float cl0, clFlaps, stallDeg, stallFlapsLossDeg;
    float cd0, cdFlapsLin, cdFlapsSq, cdGear;
    float cm0, cmAlpha, cmElev, cmq, cmFlaps;
    float clAileron, clp, cnBeta, cnRudder, cnr;
    float maxElevDeg, maxAilDeg, maxRudDeg;
    float trimRangeDeg;

    // ---- для подсказок, автопилота и сценариев, kt
    float rotateKt, approachKt, climbKt, cruiseKt;
};

const AircraftType& GetAircraftType(AircraftKind k);
