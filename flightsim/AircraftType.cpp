#include "AircraftType.h"

namespace {

// Лёгкий двухдвигательный бизнес-джет (двигатели на хвосте, Т-образное оперение).
AircraftType MakeLightJet()
{
    AircraftType t{};
    t.kind = AircraftKind::LightJet;
    t.nameEn = "Business jet";
    t.nameRu = "Бизнес-джет";
    t.emptyMass = 5100; t.payload = 600; t.maxFuel = 2600; t.defaultFuel = 1900;
    t.wingArea = 30; t.span = 15.9f; t.chord = 2.0f;
    const Vector3 c[CONTACT_COUNT] = {
        {4.9f, -1.9f, 0}, {-0.6f, -1.9f, -1.8f}, {-0.6f, -1.9f, 1.8f}, {-7.4f, -0.4f, 0}, {0, -1.05f, 0},
        {7.4f, -0.2f, 0}, {-1.8f, -0.15f, -7.9f}, {-1.8f, -0.15f, 7.9f}, {-3.6f, 0.1f, -2.0f}, {-3.6f, 0.1f, 2.0f}};
    for (int i = 0; i < CONTACT_COUNT; ++i) t.contacts[i] = c[i];
    t.eye = {5.55f, 0.72f, 0};
    t.restHeight = 1.74f;
    t.inertia[0] = 38000; t.inertia[1] = 100000; t.inertia[2] = 74000;
    t.jet = true; t.engineCount = 2;
    t.enginePos[0] = {-3.6f, 0.65f, -1.65f}; t.enginePos[1] = {-3.6f, 0.65f, 1.65f};
    t.maxThrust = 26000; t.propPower = 0; t.thrustLineY = 0.4f; t.idleN1 = 22;
    t.fuelFlowCoef = 1.9e-5f; t.hasReverser = true;
    t.retractableGear = true; t.gearTime = 6;
    t.flapDeg[0] = 0; t.flapDeg[1] = 10; t.flapDeg[2] = 20; t.flapDeg[3] = 35;
    t.flapNames[0] = "UP"; t.flapNames[1] = "10"; t.flapNames[2] = "20"; t.flapNames[3] = "35";
    t.springMain = 200000; t.springNose = 120000; t.dampMain = 25000; t.dampNose = 15000;
    t.gearCollapseMs = 4.6f; t.maxSteerDeg = 45;
    t.vmo = 260; t.vle = 200; t.vfe[0] = 260; t.vfe[1] = 200; t.vfe[2] = 180; t.vfe[3] = 160;
    t.cl0 = 0.10f; t.clFlaps = 0.60f; t.stallDeg = 15; t.stallFlapsLossDeg = 2.5f;
    t.cd0 = 0.021f; t.cdFlapsLin = 0.006f; t.cdFlapsSq = 0.045f; t.cdGear = 0.018f;
    t.cm0 = 0.06f; t.cmAlpha = 1.0f; t.cmElev = 1.3f; t.cmq = 22; t.cmFlaps = 0.06f;
    t.clAileron = 0.11f; t.clp = 0.48f; t.cnBeta = 0.11f; t.cnRudder = 0.06f; t.cnr = 0.16f;
    t.maxElevDeg = 25; t.maxAilDeg = 20; t.maxRudDeg = 25; t.trimRangeDeg = 10;
    t.rotateKt = 110; t.approachKt = 120; t.climbKt = 180; t.cruiseKt = 220;
    return t;
}

// Одномоторный винтовой высокоплан (как Cessna 172): медленный и прощающий ошибки.
AircraftType MakeProp()
{
    AircraftType t{};
    t.kind = AircraftKind::Prop;
    t.nameEn = "Light prop (C172)";
    t.nameRu = "Лёгкий винтовой (C172)";
    t.emptyMass = 760; t.payload = 170; t.maxFuel = 145; t.defaultFuel = 110;
    t.wingArea = 16.2f; t.span = 11.0f; t.chord = 1.5f;
    const Vector3 c[CONTACT_COUNT] = {
        {1.4f, -1.05f, 0}, {-0.3f, -1.05f, -1.15f}, {-0.3f, -1.05f, 1.15f}, {-5.0f, -0.2f, 0}, {0, -0.75f, 0},
        {2.55f, -0.05f, 0}, {-0.3f, 0.95f, -5.5f}, {-0.3f, 0.95f, 5.5f}, {2.45f, -0.84f, 0}, {2.45f, -0.84f, 0}};
    for (int i = 0; i < CONTACT_COUNT; ++i) t.contacts[i] = c[i];
    t.eye = {0.55f, 0.45f, -0.3f};
    t.restHeight = 0.98f;
    t.inertia[0] = 1300; t.inertia[1] = 2700; t.inertia[2] = 1800;
    t.jet = false; t.engineCount = 1;
    t.enginePos[0] = {2.0f, 0, 0}; t.enginePos[1] = {2.0f, 0, 0};
    t.maxThrust = 2800; t.propPower = 120000; t.thrustLineY = 0.0f; t.idleN1 = 25;
    t.fuelFlowCoef = 8.5e-8f; t.hasReverser = false;
    t.retractableGear = false; t.gearTime = 1;
    t.flapDeg[0] = 0; t.flapDeg[1] = 10; t.flapDeg[2] = 20; t.flapDeg[3] = 30;
    t.flapNames[0] = "UP"; t.flapNames[1] = "10"; t.flapNames[2] = "20"; t.flapNames[3] = "30";
    t.springMain = 57000; t.springNose = 65000; t.dampMain = 5300; t.dampNose = 4500;
    t.gearCollapseMs = 3.5f; t.maxSteerDeg = 35;
    t.vmo = 163; t.vle = 163; t.vfe[0] = 163; t.vfe[1] = 110; t.vfe[2] = 85; t.vfe[3] = 85;
    t.cl0 = 0.25f; t.clFlaps = 0.60f; t.stallDeg = 15.5f; t.stallFlapsLossDeg = 1.5f;
    t.cd0 = 0.030f; t.cdFlapsLin = 0.01f; t.cdFlapsSq = 0.03f; t.cdGear = 0.0f;
    t.cm0 = 0.07f; t.cmAlpha = 1.0f; t.cmElev = 1.3f; t.cmq = 22; t.cmFlaps = 0.04f;
    t.clAileron = 0.11f; t.clp = 0.48f; t.cnBeta = 0.10f; t.cnRudder = 0.07f; t.cnr = 0.16f;
    t.maxElevDeg = 25; t.maxAilDeg = 20; t.maxRudDeg = 22; t.trimRangeDeg = 10;
    t.rotateKt = 55; t.approachKt = 65; t.climbKt = 75; t.cruiseKt = 110;
    return t;
}

// Узкофюзеляжный лайнер (класса A320): тяжёлый, инертный, двигатели под крылом.
AircraftType MakeAirliner()
{
    AircraftType t{};
    t.kind = AircraftKind::Airliner;
    t.nameEn = "Airliner (A320 class)";
    t.nameRu = "Лайнер (класса A320)";
    t.emptyMass = 42000; t.payload = 12000; t.maxFuel = 19000; t.defaultFuel = 6000;
    t.wingArea = 122.6f; t.span = 34.1f; t.chord = 4.2f;
    const Vector3 c[CONTACT_COUNT] = {
        {11.2f, -4.0f, 0}, {-1.2f, -4.0f, -3.8f}, {-1.2f, -4.0f, 3.8f}, {-18.0f, -1.0f, 0}, {0, -2.1f, 0},
        {18.6f, -0.4f, 0}, {-4.0f, 0.3f, -17.0f}, {-4.0f, 0.3f, 17.0f}, {2.0f, -3.1f, -5.75f}, {2.0f, -3.1f, 5.75f}};
    for (int i = 0; i < CONTACT_COUNT; ++i) t.contacts[i] = c[i];
    t.eye = {16.3f, 1.0f, -0.55f};
    t.restHeight = 3.75f;
    t.inertia[0] = 2.2e6f; t.inertia[1] = 6.0e6f; t.inertia[2] = 3.0e6f;
    t.jet = true; t.engineCount = 2;
    t.enginePos[0] = {2.0f, -2.2f, -5.75f}; t.enginePos[1] = {2.0f, -2.2f, 5.75f};
    t.maxThrust = 200000; t.propPower = 0; t.thrustLineY = -2.2f; t.idleN1 = 20;
    t.fuelFlowCoef = 1.6e-5f; t.hasReverser = true;
    t.retractableGear = true; t.gearTime = 9;
    t.flapDeg[0] = 0; t.flapDeg[1] = 10; t.flapDeg[2] = 20; t.flapDeg[3] = 35;
    t.flapNames[0] = "UP"; t.flapNames[1] = "1"; t.flapNames[2] = "2"; t.flapNames[3] = "FULL";
    t.springMain = 1.08e6f; t.springNose = 4.0e5f; t.dampMain = 2.0e5f; t.dampNose = 6.0e4f;
    t.gearCollapseMs = 3.6f; t.maxSteerDeg = 70;
    t.vmo = 350; t.vle = 280; t.vfe[0] = 350; t.vfe[1] = 230; t.vfe[2] = 200; t.vfe[3] = 177;
    t.cl0 = 0.15f; t.clFlaps = 1.0f; t.stallDeg = 14; t.stallFlapsLossDeg = 0;
    t.cd0 = 0.020f; t.cdFlapsLin = 0.006f; t.cdFlapsSq = 0.05f; t.cdGear = 0.015f;
    t.cm0 = 0.06f; t.cmAlpha = 1.5f; t.cmElev = 1.5f; t.cmq = 30; t.cmFlaps = 0.08f;
    t.clAileron = 0.09f; t.clp = 0.50f; t.cnBeta = 0.12f; t.cnRudder = 0.12f; t.cnr = 0.45f;
    t.maxElevDeg = 25; t.maxAilDeg = 25; t.maxRudDeg = 30; t.trimRangeDeg = 12;
    t.rotateKt = 140; t.approachKt = 140; t.climbKt = 250; t.cruiseKt = 280;
    return t;
}

} // namespace

const AircraftType& GetAircraftType(AircraftKind k)
{
    static const AircraftType types[] = {MakeLightJet(), MakeProp(), MakeAirliner()};
    return types[(int)k];
}
