// Player.h — состояние и физика игрока во всех трёх режимах.
#pragma once
#include "raylib.h"
#include "config.h"
#include "Level.h"
#include <vector>

// События одного шага физики — используются для эффектов и статистики.
struct StepEvents {
    bool jumped   = false;
    bool landed   = false;
    bool died     = false;
    bool finished = false;
    bool portal   = false;
    PortalType portalType = PortalType::Cube;
};

class Player {
public:
    void Reset(const Level& level);

    // Один фиксированный шаг физики (cfg::PHYSICS_DT).
    // held — кнопка удерживается, pressed — была нажата с прошлого шага.
    void Step(const Level& level, bool held, bool pressed, StepEvents* events);

    Rectangle Hitbox() const { return {pos.x, pos.y, cfg::PLAYER_SIZE, cfg::PLAYER_SIZE}; }
    Vector2   Center() const { return {pos.x + cfg::PLAYER_SIZE * 0.5f, pos.y + cfg::PLAYER_SIZE * 0.5f}; }
    float     GravityDir() const { return gravityFlipped ? -1.0f : 1.0f; }
    bool      CeilingActive() const { return mode != PlayerMode::Cube || gravityFlipped; }

    // Интерполированные значения для рендера (alpha = доля шага 0..1).
    Vector2 RenderCenter(float alpha) const;
    float   RenderRotation(float alpha) const;

    // --- состояние (открыто: копируется целиком, удобно для проверки уровней)
    Vector2    pos{};            // левый верхний угол хитбокса
    Vector2    prevPos{};
    float      vy = 0;
    PlayerMode mode = PlayerMode::Cube;
    bool       gravityFlipped = false;
    bool       onGround = false;
    bool       dead = false;
    bool       finished = false;
    bool       wantFlip = false;  // буфер нажатия для шара
    bool       holding = false;   // для визуала (пламя корабля)
    float      rotation = 0;      // визуальный угол, градусы
    float      prevRotation = 0;
    std::vector<unsigned char> portalUsed;

private:
    void ApplyPortal(const Portal& p);
    void Die() { dead = true; }
    void UpdateRotation(float dt);
};
