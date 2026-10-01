#pragma once
#include "Aircraft.h"
#include "World.h"

// Курсо-глиссадная система (ILS). Берётся ближайший торец ВПП с ILS, к которому
// самолёт летит (курс в пределах ±90° от посадочного) и в зоне действия которого он находится.
struct Ils {
    bool valid = false;        // в зоне действия курсового маяка
    bool gsValid = false;      // глиссадный сигнал есть (до точки приземления)
    char name[16] = "";
    int airport = -1, end = 0;
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
    Ils best;
    float bestDist = 1e30f;
    const float hdg = a.HeadingDeg();
    for (int i = 0; i < world::AirportCount(); ++i) {
        const Runway& rw = world::GetAirport(i).rwy;
        if (!rw.ils) continue;
        for (int end = 0; end < 2; ++end) {
            float course = rw.Course(end);
            if (fabsf(WrapDeg180(hdg - course)) > 90.0f) continue;
            Vector3 d = rw.Dir(end);
            Vector3 right{-d.z, 0, d.x};
            // Курсовой маяк — за дальним торцом, глиссадный — у зоны приземления.
            Vector3 loc = Vector3Add(rw.center, Vector3Scale(d, rw.halfLen + 300.0f));
            Vector3 gs = Vector3Add(rw.center, Vector3Scale(d, -(rw.halfLen - 300.0f)));
            float rx = a.pos.x - loc.x, rz = a.pos.z - loc.z;
            float along = -(rx * d.x + rz * d.z);
            if (along < 0 || along > 35000.0f) continue;
            float lateral = rx * right.x + rz * right.z;
            float locAngle = atan2f(lateral, along) * RAD2DEG;
            if (fabsf(locAngle) > 35.0f || along > bestDist) continue;
            bestDist = along;
            Ils ils;
            ils.valid = true;
            ils.airport = i;
            ils.end = end;
            snprintf(ils.name, sizeof ils.name, "ILS %s", rw.ident[end]);
            ils.courseDeg = course;
            ils.lateralM = lateral;
            ils.locAngleDeg = locAngle;
            ils.locDots = Clampf(locAngle / 1.25f, -2.5f, 2.5f);
            float gx = gs.x - a.pos.x, gz = gs.z - a.pos.z;
            float dg = gx * d.x + gz * d.z;
            float h = a.pos.y - a.Type().restHeight - rw.center.y;
            ils.distTdM = dg;
            ils.dmeNm = sqrtf(gx * gx + gz * gz) / 1852.0f;
            if (dg > 150.0f) {
                ils.gsValid = true;
                ils.gsDevDeg = atan2f(h, dg) * RAD2DEG - 3.0f;
                ils.gsDots = Clampf(ils.gsDevDeg / 0.35f, -2.5f, 2.5f);
            }
            best = ils;
        }
    }
    return best;
}
