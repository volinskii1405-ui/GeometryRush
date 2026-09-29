// IconRenderer.h — процедурные иконки игрока (куб / шар / корабль) и палитра.
#pragma once
#include "raylib.h"
#include "config.h"

namespace icons {

constexpr int PALETTE_SIZE = 12;
constexpr int CUBE_COUNT   = 8;
constexpr int BALL_COUNT   = 8;
constexpr int SHIP_COUNT   = 8;

Color       Palette(int index);
int         Count(PlayerMode mode);
const char* Name(PlayerMode mode, int id);

// size — сторона/диаметр иконки в пикселях, rotation — градусы по часовой.
void DrawCube(int id, Vector2 center, float size, float rotation, Color primary, Color secondary);
void DrawBall(int id, Vector2 center, float size, float rotation, Color primary, Color secondary);
// Корабль рисуется носом вправо; сверху сидит мини-куб cubeId (-1 = без него).
void DrawShip(int id, Vector2 center, float size, float rotation, bool flipY,
              Color primary, Color secondary, int cubeId);

// Универсальная отрисовка для сеток выбора.
void DrawIcon(PlayerMode mode, int id, Vector2 center, float size, float rotation,
              Color primary, Color secondary, int cubeIdForShip = -1);

} // namespace icons
