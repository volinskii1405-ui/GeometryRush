#pragma once
#include "FlightMath.h"

class Aircraft;
class Terrain;

enum class CamMode { Cockpit, Chase, Orbit, Tower, Count };
const char* CamModeName(CamMode m);

// 3D-сцена: небо, рельеф, аэродром, самолёт и камеры.
class Scene {
public:
    void Init();
    void Unload();
    Shader LitShader() const { return lit_; }

    void ResetCamera(const Aircraft& a);
    void UpdateCamera(const Aircraft& a, CamMode mode, float dt);
    void Draw(const Aircraft& a, const Terrain& t, CamMode mode, float time, Vector3 wind);

    Camera3D camera{};
    float crashAge = -1;   // секунд после катастрофы (для огня и дыма)

private:
    void DrawSky();
    void DrawAirport(float time, Vector3 wind);
    void DrawAircraftModel(const Aircraft& a, float time);
    void DrawShadow(const Aircraft& a, const Terrain& t);
    void DrawCrashFx(const Aircraft& a);
    void Part(const Mesh& mesh, Matrix local, Matrix world, Color c, float spec = 0.35f);
    void Box(Vector3 center, Vector3 size, Color c, float spec = 0.0f);
    void SetSpecular(float s);

    Shader lit_{};
    int locSun_ = -1, locView_ = -1, locFog_ = -1, locFogDensity_ = -1, locSpec_ = -1;
    Material mat_{};
    Material unlit_{};
    Mesh sphere_{}, cube_{}, cylinder_{}, cone_{}, sky_{};
    float spec_ = -1;

    Vector3 chaseOffset_{};   // сглаженное положение камеры преследования относительно самолёта
    float orbitYaw_ = 200.0f, orbitPitch_ = 12.0f, orbitDist_ = 35.0f;
    float lookYaw_ = 0, lookPitch_ = 0;   // обзор из кабины (ПКМ)
};
