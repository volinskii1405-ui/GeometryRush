// Collision.cpp
#include "Collision.h"
#include "config.h"
#include <cmath>

namespace collision {

bool Overlaps(const Rectangle& a, const Rectangle& b) {
    // строгие неравенства: касание гранями пересечением не считается
    return a.x < b.x + b.width && a.x + a.width > b.x &&
           a.y < b.y + b.height && a.y + a.height > b.y;
}

Rectangle CellRect(int col, int row) {
    return {col * cfg::TILE, row * cfg::TILE, cfg::TILE, cfg::TILE};
}

Rectangle SpikeHitbox(int col, int row, Tile spike) {
    const float T = cfg::TILE;
    float x = col * T, y = row * T;
    // узкая вертикальная коробка по центру треугольника
    if (spike == Tile::SpikeUp) return {x + T * 0.34f, y + T * 0.40f, T * 0.32f, T * 0.56f};
    return {x + T * 0.34f, y + T * 0.04f, T * 0.32f, T * 0.56f};
}

CellRange CellsCovering(const Rectangle& r) {
    const float T = cfg::TILE;
    CellRange cr;
    cr.c0 = (int)std::floor(r.x / T);
    cr.c1 = (int)std::floor((r.x + r.width) / T);
    cr.r0 = (int)std::floor(r.y / T);
    cr.r1 = (int)std::floor((r.y + r.height) / T);
    return cr;
}

void SolidCellsOverlapping(const Level& level, const Rectangle& r, std::vector<Rectangle>* out) {
    out->clear();
    CellRange cr = CellsCovering(r);
    for (int row = cr.r0; row <= cr.r1; ++row)
        for (int col = cr.c0; col <= cr.c1; ++col)
            if (level.IsSolid(col, row)) {
                Rectangle cell = CellRect(col, row);
                if (Overlaps(r, cell)) out->push_back(cell);
            }
}

bool TouchesSpike(const Level& level, const Rectangle& r) {
    CellRange cr = CellsCovering(r);
    for (int row = cr.r0; row <= cr.r1; ++row)
        for (int col = cr.c0; col <= cr.c1; ++col) {
            Tile t = level.At(col, row);
            if ((t == Tile::SpikeUp || t == Tile::SpikeDown) && Overlaps(r, SpikeHitbox(col, row, t)))
                return true;
        }
    return false;
}

Rectangle Shrink(const Rectangle& r, float dx, float dy) {
    return {r.x + dx, r.y + dy, r.width - 2 * dx, r.height - 2 * dy};
}

} // namespace collision
