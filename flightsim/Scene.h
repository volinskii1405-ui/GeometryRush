#pragma once
#include "Environment.h"
#include "Scenery.h"

class Aircraft;
class Terrain;
class Effects;

enum class CamMode { Cockpit, Chase, Orbit, Tower, Count };
const char* CamModeName(CamMode m);

// 3D-сцена: небо, рельеф, аэродромы, окружение, самолёт и камеры.
class Scene {
public:
    void Init();
    void BuildWorld(const Terrain& t);   // окружение (леса, города) — после генерации рельефа
    void Unload();
    Shader LitShader() const { return lit_; }
    void SetEnvironment(const Environment& env);

    void ResetCamera(const Aircraft& a);
    void UpdateCamera(const Aircraft& a, CamMode mode, float dt);
    // Медленный облёт для фона меню.
    void MenuCamera(Vector3 target, float time);
    // Толчок камеры (касание полосы): strength 0..1.
    void Kick(float strength) { kick_ = fmaxf(kick_, strength); }
    void Draw(const Aircraft& a, const Terrain& t, CamMode mode, float time, Vector3 wind, const Effects& fx,
              bool showAircraft = true);

    Camera3D camera{};
    float crashAge = -1;
    bool mouseLocked = false;   // мышь занята (открыта карта): камера её не слушает   // секунд после катастрофы (для огня и дыма)

    // Другой самолёт (трафик): внутри BeginMode3D.
    void DrawTraffic(const Aircraft& a, float time) { DrawAircraftModel(a, time); }

private:
    void DrawSky();
    void DrawAirport(int index, float time, Vector3 wind);
    void DrawAircraftModel(const Aircraft& a, float time, bool self = true);   // self=false — только обломки
    void DrawLightJet(const Aircraft& a, const Matrix& world, int mask);
    void DrawProp(const Aircraft& a, const Matrix& world, float time, int mask);
    void DrawAirliner(const Aircraft& a, const Matrix& world, int mask);
    void DrawShadow(const Aircraft& a, const Terrain& t);
    void DrawCrashFx(const Aircraft& a);
    void Part(const Mesh& mesh, Matrix local, Matrix world, Color c, float spec = 0.35f);
    void PartBetween(Vector3 a, Vector3 b, float r, const Matrix& world, Color c);
    void Box(Vector3 center, Vector3 size, Color c, float spec = 0.0f);
    void SetSpecular(float s);
    // Светящаяся точка, видная издалека; farScale — насколько ореол растёт с расстоянием.
    void Glow(Vector3 pos, Color c, float size, float farScale = 0.004f);
    void DrawGlows();

    Shader lit_{};
    int locSun_ = -1, locSunLight_ = -1, locAmbient_ = -1, locView_ = -1, locFog_ = -1, locFogDensity_ = -1, locSpec_ = -1;
    int locLlPos_ = -1, locLlDir_ = -1, locLlOn_ = -1;
    Material mat_{};
    Material unlit_{};
    Mesh sphere_{}, cube_{}, cylinder_{}, cone_{}, sky_{};
    Texture2D soft_{};
    float spec_ = -1;
    Environment env_;
    Scenery scenery_;

    struct GlowPt { Vector3 p; Color c; float size, farScale; };
    std::vector<GlowPt> glows_;

    Vector3 chaseOffset_{};   // сглаженное положение камеры преследования относительно самолёта
    float orbitYaw_ = 200.0f, orbitPitch_ = 12.0f, orbitDist_ = 35.0f;
    float lookYaw_ = 0, lookPitch_ = 0;   // обзор из кабины (ПКМ)
    float kick_ = 0, shakeTime_ = 0, shakeSmooth_ = 0;
    float ShakeAmount(const Aircraft& a) const;   // 0..1: тряска от сваливания, скорости, полосы, повреждений
    void ApplyShake(const Aircraft& a, CamMode mode, float dt);
    void UpdateCameraBase(const Aircraft& a, CamMode mode, float dt);
};
