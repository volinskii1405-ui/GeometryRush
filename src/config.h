// config.h — все настраиваемые константы игры в одном месте.
// Единицы: пиксели мира, секунды, градусы. Ось Y направлена вниз.
#pragma once

// Режим игрока (меняется порталами).
enum class PlayerMode { Cube = 0, Ball = 1, Ship = 2 };
constexpr int PLAYER_MODE_COUNT = 3;

namespace cfg {

// ---------------------------------------------------------------- окно
constexpr int         SCREEN_W     = 1280;
constexpr int         SCREEN_H     = 720;
constexpr int         TARGET_FPS   = 144;   // лимит рендера (физика от него не зависит)
constexpr const char* WINDOW_TITLE = "Geometry Rush";

// ---------------------------------------------------------------- мир
constexpr float TILE                = 40.0f;           // размер клетки уровня
constexpr int   PHYSICS_HZ          = 240;             // фиксированный шаг физики
constexpr float PHYSICS_DT          = 1.0f / PHYSICS_HZ;
constexpr float MAX_FRAME_TIME      = 0.1f;            // защита от "спирали смерти"
constexpr int   CORRIDOR_TILES      = 10;              // высота коридора (корабль/шар)
constexpr float PORTAL_WIDTH        = TILE * 0.9f;
constexpr float PORTAL_HEIGHT       = TILE * 3.0f;     // портал = 3 клетки по высоте
constexpr float WORLD_TOP_LIMIT     = -TILE * 30.0f;   // улетел выше — смерть

// ---------------------------------------------------------------- игрок (общее)
constexpr float PLAYER_SIZE         = 36.0f;   // сторона хитбокса
constexpr float PLAYER_SPEED        = 416.0f;  // горизонтальная скорость, px/s (10.4 клетки/с)
constexpr float PLAYER_START_X      = TILE * 2.0f;
constexpr float INNER_HITBOX_INSET  = 7.0f;    // внутренний хитбокс для боковых ударов
constexpr float LAND_TOLERANCE      = 6.0f;    // допуск "запрыгивания" на грань блока
constexpr float SPIKE_HITBOX_INSET  = 4.0f;    // хитбокс игрока для шипов меньше на столько
constexpr float MAX_FALL_SPEED      = 1500.0f;

// ---------------------------------------------------------------- куб
constexpr float CUBE_GRAVITY        = 4950.0f;
constexpr float CUBE_JUMP_VELOCITY  = 960.0f;  // высота прыжка ~2.3 клетки
constexpr float CUBE_ROTATION_SPEED = 470.0f;  // град/с в воздухе (~180° за прыжок)
constexpr float CUBE_SNAP_RATE      = 22.0f;   // скорость доводки угла до 90° на земле

// ---------------------------------------------------------------- шар
constexpr float BALL_GRAVITY        = 4200.0f;
constexpr float BALL_FLIP_KICK      = 320.0f;  // стартовая скорость после смены гравитации
constexpr float BALL_MAX_SPEED      = 1200.0f;
constexpr float BALL_ROLL_FACTOR    = 0.85f;   // множитель скорости вращения при качении

// ---------------------------------------------------------------- корабль
constexpr float SHIP_LIFT           = 2300.0f; // ускорение вверх при удержании
constexpr float SHIP_GRAVITY        = 2100.0f; // ускорение вниз без удержания
constexpr float SHIP_MAX_VY         = 560.0f;  // ограничение вертикальной скорости
constexpr float SHIP_TILT_MAX       = 42.0f;   // макс. наклон корпуса, градусы
constexpr float SHIP_TILT_RATE      = 14.0f;   // сглаживание наклона

// ---------------------------------------------------------------- камера
constexpr float CAMERA_PLAYER_SCREEN_X = 380.0f; // где по X на экране держим игрока
constexpr float CAMERA_FLOOR_SCREEN_Y  = 560.0f; // пол не поднимается выше этой строки
constexpr float CAMERA_BAND_TOP        = 250.0f; // "мёртвая зона" по вертикали
constexpr float CAMERA_BAND_BOTTOM     = 540.0f;
constexpr float CAMERA_SMOOTH          = 5.0f;   // скорость сглаживания по Y (1/с)

// ---------------------------------------------------------------- игровой процесс
constexpr float DEATH_RESTART_DELAY = 0.35f;   // 0 = рестарт мгновенно без взрыва
constexpr float VICTORY_DELAY       = 1.2f;
constexpr int   TRAIL_LENGTH        = 28;      // точек в следе

} // namespace cfg
