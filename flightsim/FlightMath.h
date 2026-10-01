#pragma once
// Общие константы и мелкие математические помощники симулятора.
//
// Система координат мира: Y — вверх, север — (-Z), восток — (+X).
// Связанная система самолёта: X — вперёд (нос), Y — вверх, Z — правое крыло.
// (X × Y = Z, правая тройка.)

#include "raylib.h"
#include "raymath.h"

#include <cmath>

namespace fs {

constexpr float G = 9.80665f;

constexpr float MS_TO_KT = 1.943844f;
constexpr float KT_TO_MS = 1.0f / MS_TO_KT;
constexpr float M_TO_FT = 3.28084f;
constexpr float FT_TO_M = 1.0f / M_TO_FT;
constexpr float MS_TO_FPM = 196.8504f;

inline float Sq(float x) { return x * x; }
inline float Sign(float x) { return x < 0.0f ? -1.0f : 1.0f; }
inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float SmoothStep(float e0, float e1, float x)
{
    float t = Clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Движение значения к цели с ограниченной скоростью (рулевые машинки, рычаги).
inline float MoveTowards(float v, float target, float maxDelta)
{
    if (v < target) return fminf(v + maxDelta, target);
    return fmaxf(v - maxDelta, target);
}

inline float WrapDeg360(float d)
{
    d = fmodf(d, 360.0f);
    return d < 0.0f ? d + 360.0f : d;
}

inline float WrapDeg180(float d)
{
    d = WrapDeg360(d);
    return d > 180.0f ? d - 360.0f : d;
}

inline Vector3 Rotate(Vector3 v, Quaternion q) { return Vector3RotateByQuaternion(v, q); }

// Стандартная атмосфера (ISA), высота в метрах.
inline float AirDensity(float altM)
{
    float h = Clampf(altM, -500.0f, 11000.0f);
    return 1.225f * powf(1.0f - 2.25577e-5f * h, 4.2559f);
}

inline float SpeedOfSound(float altM)
{
    float t = 288.15f - 0.0065f * Clampf(altM, -500.0f, 11000.0f);
    return 20.0468f * sqrtf(t);
}

} // namespace fs
