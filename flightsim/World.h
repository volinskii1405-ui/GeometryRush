#pragma once
#include "FlightMath.h"

// Аэродромы острова. У каждого одна ВПП с двумя посадочными курсами.
struct Runway {
    const char* ident[2];  // номера торцов: [0] — посадка курсом heading, [1] — обратным
    Vector3 center;        // середина ВПП (y — превышение)
    float heading;         // курс торца [0], град
    float halfLen, halfWidth;
    bool ils;              // есть ли курсо-глиссадная система

    // Направление посадки (единичный вектор) для торца end.
    Vector3 Dir(int end) const
    {
        float h = (heading + (end ? 180.0f : 0.0f)) * DEG2RAD;
        return {sinf(h), 0, -cosf(h)};
    }
    float Course(int end) const { return fs::WrapDeg360(heading + (end ? 180.0f : 0.0f)); }
    // Порог (начало) полосы для посадки на торец end.
    Vector3 Threshold(int end) const { return Vector3Add(center, Vector3Scale(Dir(end), -halfLen)); }
    // Координаты точки в системе полосы: x — вдоль курса heading от центра, y — вправо.
    Vector2 Local(float x, float z) const
    {
        Vector3 d = Dir(0);
        float dx = x - center.x, dz = z - center.z;
        return {dx * d.x + dz * d.z, dx * -d.z + dz * d.x};
    }
    float Dist(float x, float z, float marginLen = 0, float marginWid = 0) const
    {
        Vector2 l = Local(x, z);
        float a = fmaxf(fabsf(l.x) - halfLen - marginLen, 0.0f), b = fmaxf(fabsf(l.y) - halfWidth - marginWid, 0.0f);
        return sqrtf(a * a + b * b);
    }
    bool Contains(float x, float z, float margin = 0) const { return Dist(x, z, margin, margin) <= 0.0f; }
};

struct Airport {
    const char* nameEn;
    const char* nameRu;
    Runway rwy;
    bool main;             // главный аэродром: вышка, ангары, город рядом
};

namespace world {
int AirportCount();
const Airport& GetAirport(int i);
// Ближайшая ВПП к точке; dist — расстояние до её прямоугольника.
int NearestAirport(float x, float z, float* dist = nullptr);
} // namespace world
