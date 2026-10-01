#pragma once
#include "Environment.h"

#include <vector>

class Aircraft;

// Полупрозрачные эффекты: облака, частицы (дым шин, выхлоп, пожар), дождь, звёзды, капли на стекле.
class Effects {
public:
    void Init();
    void Unload();
    void SetEnvironment(const Environment& env);

    // Обновление частиц и порождение новых по состоянию самолёта.
    void Update(const Aircraft& a, const Environment& env, Vector3 wind, float dt, bool touchdown);
    void Clear();

    // Внутри BeginMode3D, после непрозрачной геометрии.
    void DrawSkyObjects(const Camera3D& cam, const Environment& env) const;       // звёзды
    void DrawClouds(const Camera3D& cam, const Environment& env) const;           // облака
    void DrawParticles(const Camera3D& cam) const;
    void DrawRain3D(const Camera3D& cam, const Environment& env) const;
    // 2D: капли на лобовом стекле (вид из кабины).
    void DrawWindshield(const Environment& env, float airspeed, bool belowClouds) const;

    Texture2D Soft() const { return soft_; }

private:
    struct Particle {
        Vector3 pos, vel;
        float life, maxLife, size, grow;
        Color color;
    };
    struct Puff { Vector3 pos; float size; float shade; };
    struct Drop { float x, y, r, speed; };

    void Spawn(Vector3 p, Vector3 v, float life, float size, float grow, Color c);

    Texture2D soft_{}, cloudTex_{};
    Mesh layer_{};
    Material layerMat_{};
    std::vector<Particle> particles_;
    std::vector<Puff> puffs_;
    std::vector<Vector3> stars_;
    std::vector<Vector3> rain_;
    std::vector<Drop> drops_;
    float exhaustTimer_ = 0, fireTimer_ = 0, rainCamY_ = 0;
    Vector3 lastCam_{};
};
