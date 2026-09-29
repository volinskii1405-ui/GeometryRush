// Collision.h — геометрия хитбоксов и запросы к сетке уровня.
#pragma once
#include "raylib.h"
#include "Level.h"
#include <vector>

namespace collision {

bool Overlaps(const Rectangle& a, const Rectangle& b);

// Прямоугольник клетки сетки.
Rectangle CellRect(int col, int row);

// Уменьшенный хитбокс шипа (прощает касание краёв треугольника).
Rectangle SpikeHitbox(int col, int row, Tile spike);

// Диапазон клеток, которые пересекает прямоугольник.
struct CellRange { int c0, c1, r0, r1; };
CellRange CellsCovering(const Rectangle& r);

// Список твёрдых блоков, пересекающих прямоугольник.
void SolidCellsOverlapping(const Level& level, const Rectangle& r, std::vector<Rectangle>* out);

// Касается ли прямоугольник хотя бы одного шипа.
bool TouchesSpike(const Level& level, const Rectangle& r);

Rectangle Shrink(const Rectangle& r, float dx, float dy);

} // namespace collision
