// Player.cpp — физика куба, шара и корабля.
#include "Player.h"
#include "Collision.h"
#include <algorithm>
#include <cmath>

void Player::Reset(const Level& level) {
    pos = {cfg::PLAYER_START_X, level.FloorY() - cfg::PLAYER_SIZE};
    prevPos = pos;
    vy = 0;
    mode = PlayerMode::Cube;
    gravityFlipped = false;
    onGround = true;
    dead = false;
    finished = false;
    wantFlip = false;
    holding = false;
    rotation = prevRotation = 0;
    portalUsed.assign(level.Portals().size(), 0);
}

void Player::ApplyPortal(const Portal& p) {
    switch (p.type) {
        case PortalType::Cube:
            mode = PlayerMode::Cube;
            break;
        case PortalType::Ball:
            mode = PlayerMode::Ball;
            vy *= 0.5f;
            break;
        case PortalType::Ship:
            if (mode != PlayerMode::Ship) rotation = 0, prevRotation = 0;
            mode = PlayerMode::Ship;
            vy *= 0.5f;
            break;
        case PortalType::GravityFlip:
            gravityFlipped = !gravityFlipped;
            vy *= 0.5f;
            onGround = false;
            break;
        case PortalType::GravityNormal:
            if (gravityFlipped) {
                gravityFlipped = false;
                vy *= 0.5f;
                onGround = false;
            }
            break;
    }
    wantFlip = false;
}

void Player::Step(const Level& level, bool held, bool pressed, StepEvents* ev) {
    StepEvents dummy;
    if (!ev) ev = &dummy;
    prevPos = pos;
    prevRotation = rotation;
    if (dead || finished) return;

    const float dt = cfg::PHYSICS_DT;
    const float S = cfg::PLAYER_SIZE;
    holding = held;
    if (pressed) wantFlip = true;
    if (!held) wantFlip = false;

    // ------------------------------------------------ ввод и ускорения
    float g = GravityDir();
    switch (mode) {
        case PlayerMode::Cube:
            if (held && onGround) {            // нажатие или автопрыжок при удержании
                vy = -cfg::CUBE_JUMP_VELOCITY * g;
                onGround = false;
                ev->jumped = true;
            }
            vy += cfg::CUBE_GRAVITY * g * dt;
            if (vy * g > cfg::MAX_FALL_SPEED) vy = cfg::MAX_FALL_SPEED * g;
            break;
        case PlayerMode::Ball:
            if (wantFlip && onGround) {        // смена гравитации — только с поверхности
                gravityFlipped = !gravityFlipped;
                g = -g;
                vy = cfg::BALL_FLIP_KICK * g;
                onGround = false;
                wantFlip = false;
                ev->jumped = true;
            }
            vy += cfg::BALL_GRAVITY * g * dt;
            vy = std::clamp(vy, -cfg::BALL_MAX_SPEED, cfg::BALL_MAX_SPEED);
            break;
        case PlayerMode::Ship: {
            float acc = held ? -cfg::SHIP_LIFT * g : cfg::SHIP_GRAVITY * g;
            vy += acc * dt;
            vy = std::clamp(vy, -cfg::SHIP_MAX_VY, cfg::SHIP_MAX_VY);
            break;
        }
    }

    const bool wasOnGround = onGround;
    std::vector<Rectangle> cells;

    // ------------------------------------------------ движение по X
    pos.x += cfg::PLAYER_SPEED * dt;
    {
        // Внутренний хитбокс: удар боковой гранью блока = смерть.
        Rectangle inner = collision::Shrink(Hitbox(), 0, cfg::INNER_HITBOX_INSET);
        collision::SolidCellsOverlapping(level, inner, &cells);
        if (!cells.empty()) {
            Die();
            ev->died = true;
            return;
        }
    }

    // ------------------------------------------------ движение по Y
    pos.y += vy * dt;
    onGround = false;
    collision::SolidCellsOverlapping(level, Hitbox(), &cells);
    const float tol = cfg::LAND_TOLERANCE + std::fabs(vy) * dt;
    for (const Rectangle& b : cells) {
        float top = pos.y, bottom = pos.y + S;
        float bTop = b.y, bBottom = b.y + b.height;
        if (!collision::Overlaps(Hitbox(), b)) continue;   // уже вытолкнули другим блоком
        bool towardGravity = vy * g >= 0;
        if (g > 0) {
            if (towardGravity && bottom - bTop <= tol) {        // приземление сверху
                pos.y = bTop - S; vy = 0; onGround = true;
            } else if (!towardGravity && bBottom - top <= tol) { // удар "головой"
                if (mode == PlayerMode::Ship) { pos.y = bBottom; vy = 0; }
                else { Die(); ev->died = true; return; }
            } else { Die(); ev->died = true; return; }
        } else {
            if (towardGravity && bBottom - top <= tol) {         // приземление на низ блока
                pos.y = bBottom; vy = 0; onGround = true;
            } else if (!towardGravity && bottom - bTop <= tol) {
                if (mode == PlayerMode::Ship) { pos.y = bTop - S; vy = 0; }
                else { Die(); ev->died = true; return; }
            } else { Die(); ev->died = true; return; }
        }
    }

    // пол всегда твёрдый
    const float floorY = level.FloorY();
    if (pos.y + S >= floorY) {
        pos.y = floorY - S;
        if (vy > 0) vy = 0;
        if (g > 0) onGround = true;
    }
    // потолок коридора (корабль, шар, перевёрнутый куб)
    if (CeilingActive()) {
        const float ceilY = level.CeilingY();
        if (pos.y <= ceilY) {
            pos.y = ceilY;
            if (vy < 0) vy = 0;
            if (g < 0) onGround = true;
        }
    }
    if (pos.y < cfg::WORLD_TOP_LIMIT) { Die(); ev->died = true; return; }

    if (onGround && !wasOnGround) ev->landed = true;

    // ------------------------------------------------ шипы
    Rectangle spikeBox = collision::Shrink(Hitbox(), cfg::SPIKE_HITBOX_INSET, cfg::SPIKE_HITBOX_INSET);
    if (collision::TouchesSpike(level, spikeBox)) {
        Die();
        ev->died = true;
        return;
    }

    // ------------------------------------------------ порталы
    const auto& portals = level.Portals();
    for (size_t i = 0; i < portals.size(); ++i) {
        if (portalUsed[i]) continue;
        if (portals[i].rect.x > pos.x + S) break;          // отсортированы по X
        if (collision::Overlaps(Hitbox(), portals[i].rect)) {
            portalUsed[i] = 1;
            ApplyPortal(portals[i]);
            ev->portal = true;
            ev->portalType = portals[i].type;
        }
    }

    // ------------------------------------------------ финиш
    if (pos.x >= level.FinishX()) {
        finished = true;
        ev->finished = true;
    }

    UpdateRotation(dt);
}

void Player::UpdateRotation(float dt) {
    float g = GravityDir();
    switch (mode) {
        case PlayerMode::Cube:
            if (onGround) {
                float target = std::round(rotation / 90.0f) * 90.0f;
                rotation += (target - rotation) * std::min(1.0f, cfg::CUBE_SNAP_RATE * dt);
                if (std::fabs(target - rotation) < 0.05f) rotation = target;
            } else {
                rotation += cfg::CUBE_ROTATION_SPEED * g * dt;
            }
            break;
        case PlayerMode::Ball: {
            float radius = cfg::PLAYER_SIZE * 0.5f;
            float degPerSec = cfg::PLAYER_SPEED / radius * RAD2DEG * cfg::BALL_ROLL_FACTOR;
            rotation += degPerSec * g * dt;
            break;
        }
        case PlayerMode::Ship: {
            float target = std::atan2(vy, cfg::PLAYER_SPEED) * RAD2DEG;
            target = std::clamp(target, -cfg::SHIP_TILT_MAX, cfg::SHIP_TILT_MAX);
            rotation += (target - rotation) * std::min(1.0f, cfg::SHIP_TILT_RATE * dt);
            break;
        }
    }
    // держим угол в разумных пределах, не ломая интерполяцию
    if (rotation > 3600.0f || rotation < -3600.0f) {
        float wrap = std::round(rotation / 360.0f) * 360.0f;
        rotation -= wrap;
        prevRotation -= wrap;
    }
}

Vector2 Player::RenderCenter(float alpha) const {
    float x = prevPos.x + (pos.x - prevPos.x) * alpha;
    float y = prevPos.y + (pos.y - prevPos.y) * alpha;
    return {x + cfg::PLAYER_SIZE * 0.5f, y + cfg::PLAYER_SIZE * 0.5f};
}

float Player::RenderRotation(float alpha) const {
    return prevRotation + (rotation - prevRotation) * alpha;
}
