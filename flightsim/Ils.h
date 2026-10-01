#pragma once
#include "Aircraft.h"
#include "Terrain.h"

// Курсо-глиссадная система (ILS) обеих посадочных курсов ВПП 09/27.
// Полоса выбирается по курсу самолёта: на восток — ILS 09, на запад — ILS 27.
struct Ils {
    bool valid = false;        // в зоне действия курсового маяка
    bool gsValid = false;      // глиссадный сигнал есть (до точки приземления)
    const char* name = "";
    float courseDeg = 90;      // посадочный курс
    float locAngleDeg = 0;     // угол от оси (+ — самолёт правее оси)
    float lateralM = 0;        // боковое отклонение от оси, м (+ — правее)
    float gsDevDeg = 0;        // отклонение от глиссады 3°, град (+ — выше)
    float locDots = 0, gsDots = 0;  // то же в точках шкалы PFD (±2.5)
    float distTdM = 0;         // расстояние до точки приземления вдоль оси, м
    float dmeNm = 0;
};

inline Ils ComputeIls(const Aircraft& a)
{
    using namespace fs;
    Ils ils;
    const float L = Terrain::kRunwayHalfLen;
    bool rwy09 = fabsf(WrapDeg180(a.HeadingDeg() - 90.0f)) < 90.0f;
    float dir = rwy09 ? 1.0f : -1.0f;              // направление посадки вдоль X
    ils.name = rwy09 ? "ILS 09" : "ILS 27";
    ils.courseDeg = rwy09 ? 90.0f : 270.0f;
    float xLoc = dir * (L + 300.0f);               // курсовой маяк — за дальним торцом
    float xGs = -dir * (L - 300.0f);               // глиссадный — у зоны приземления
    float along = (xLoc - a.pos.x) * dir;
    ils.lateralM = a.pos.z * dir;
    if (along < 0 || along > 35000.0f) return ils;
    ils.locAngleDeg = atan2f(ils.lateralM, along) * RAD2DEG;
    if (fabsf(ils.locAngleDeg) > 35.0f) return ils;
    ils.valid = true;
    ils.locDots = Clampf(ils.locAngleDeg / 1.25f, -2.5f, 2.5f);
    float dg = (xGs - a.pos.x) * dir;
    float h = a.pos.y - 1.75f - Terrain::kAirportElev;
    ils.distTdM = dg;
    ils.dmeNm = sqrtf(dg * dg + a.pos.z * a.pos.z) / 1852.0f;
    if (dg > 150.0f) {
        ils.gsValid = true;
        ils.gsDevDeg = atan2f(h, dg) * RAD2DEG - 3.0f;
        ils.gsDots = Clampf(ils.gsDevDeg / 0.35f, -2.5f, 2.5f);
    }
    return ils;
}
