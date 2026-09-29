// IconRenderer.cpp — каждая иконка — отдельная функция с собственным узором.
// Все функции рисуют в локальных координатах: центр иконки в (0,0),
// h — половина размера (для шара — радиус). Поворот задаётся матрицей rlgl.
#include "IconRenderer.h"
#include "rlgl.h"
#include <cmath>
#include <initializer_list>

namespace icons {
namespace {

const Color OUT = {14, 14, 24, 255};   // тёмный контур

// ------------------------------------------------------------ примитивы
void R(float x, float y, float w, float h, Color c) { DrawRectangleRec({x, y, w, h}, c); }
void RL(float x, float y, float w, float h, float t, Color c) { DrawRectangleLinesEx({x, y, w, h}, t, c); }
void RR(float x, float y, float w, float h, float round, Color c) { DrawRectangleRounded({x, y, w, h}, round, 10, c); }
void Circ(float x, float y, float r, Color c) { DrawCircleV({x, y}, r, c); }
void Ring(float x, float y, float r0, float r1, Color c) { DrawRing({x, y}, r0, r1, 0, 360, 36, c); }
void Line(float x0, float y0, float x1, float y1, float t, Color c) { DrawLineEx({x0, y0}, {x1, y1}, t, c); }
void Tri(Vector2 a, Vector2 b, Vector2 c, Color col) { DrawTriangle(a, b, c, col); }

// Треугольник с тёмным контуром.
void TriO(Vector2 a, Vector2 b, Vector2 c, Color fill, float t) {
    DrawTriangle(a, b, c, fill);
    DrawLineEx(a, b, t, OUT);
    DrawLineEx(b, c, t, OUT);
    DrawLineEx(c, a, t, OUT);
    Circ(a.x, a.y, t * 0.5f, OUT);
    Circ(b.x, b.y, t * 0.5f, OUT);
    Circ(c.x, c.y, t * 0.5f, OUT);
}

// Четырёхугольник (выпуклый, вершины по порядку) с контуром.
void QuadO(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color fill, float t) {
    DrawTriangle(a, b, c, fill);
    DrawTriangle(a, c, d, fill);
    DrawLineEx(a, b, t, OUT);
    DrawLineEx(b, c, t, OUT);
    DrawLineEx(c, d, t, OUT);
    DrawLineEx(d, a, t, OUT);
    for (Vector2 p : {a, b, c, d}) Circ(p.x, p.y, t * 0.5f, OUT);
}

// Эллипс через масштаб окружности (дробные координаты, без int-округления).
void Ellipse(float cx, float cy, float rx, float ry, Color c) {
    rlPushMatrix();
    rlTranslatef(cx, cy, 0);
    rlScalef(rx / ry, 1.0f, 1.0f);
    DrawCircleV({0, 0}, ry, c);
    rlPopMatrix();
}

void EllipseRing(float cx, float cy, float rx, float ry, float t, Color c) {
    rlPushMatrix();
    rlTranslatef(cx, cy, 0);
    rlScalef(rx / ry, 1.0f, 1.0f);
    DrawRing({0, 0}, ry - t, ry, 0, 360, 40, c);
    rlPopMatrix();
}

void Star(float cx, float cy, float rOut, float rIn, int points, float rotDeg, Color c) {
    for (int i = 0; i < points; ++i) {
        float a0 = (rotDeg + i * 360.0f / points) * DEG2RAD;
        float a1 = a0 + PI / points;
        float a2 = a0 - PI / points;
        Vector2 tip = {cx + cosf(a0) * rOut, cy + sinf(a0) * rOut};
        Vector2 l = {cx + cosf(a1) * rIn, cy + sinf(a1) * rIn};
        Vector2 r = {cx + cosf(a2) * rIn, cy + sinf(a2) * rIn};
        Tri(tip, l, r, c);
        Tri({cx, cy}, l, r, c);
    }
}

Color Mix(Color a, Color b, float t) {
    return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
            (unsigned char)(a.b + (b.b - a.b) * t), 255};
}

// ============================================================ КУБЫ
void CubeBase(float h, Color c1) {
    R(-h, -h, 2 * h, 2 * h, OUT);
    float t = h * 0.12f;
    R(-h + t, -h + t, 2 * h - 2 * t, 2 * h - 2 * t, c1);
}

// 0. «Страж» — квадратные глаза и прямой рот.
void Cube_Sentinel(float h, Color c1, Color c2) {
    CubeBase(h, c1);
    RL(-0.72f * h, -0.72f * h, 1.44f * h, 1.44f * h, 0.07f * h, Mix(c1, c2, 0.5f));
    R(-0.56f * h, -0.48f * h, 0.38f * h, 0.38f * h, c2);
    R(0.18f * h, -0.48f * h, 0.38f * h, 0.38f * h, c2);
    R(-0.42f * h, -0.36f * h, 0.16f * h, 0.16f * h, OUT);
    R(0.32f * h, -0.36f * h, 0.16f * h, 0.16f * h, OUT);
    R(-0.56f * h, 0.24f * h, 1.12f * h, 0.2f * h, OUT);
    R(-0.1f * h, 0.24f * h, 0.2f * h, 0.1f * h, c2);
}

// 1. «Циклоп» — один большой глаз с бровью.
void Cube_Cyclops(float h, Color c1, Color c2) {
    CubeBase(h, c1);
    Circ(0, -0.02f * h, 0.52f * h, OUT);
    Circ(0, -0.02f * h, 0.44f * h, c2);
    Circ(0.08f * h, 0.0f, 0.22f * h, OUT);
    Circ(0.16f * h, -0.1f * h, 0.07f * h, WHITE);
    R(-0.66f * h, -0.8f * h, 1.32f * h, 0.16f * h, OUT);
    R(-0.6f * h, 0.62f * h, 1.2f * h, 0.12f * h, c2);
}

// 2. «Тигр» — вертикальные полосы и сплошной визор.
void Cube_Tiger(float h, Color c1, Color c2) {
    CubeBase(h, c1);
    for (int i = 0; i < 3; ++i) R((-0.62f + i * 0.5f) * h, -0.88f * h, 0.22f * h, 1.76f * h, c2);
    R(-0.88f * h, -0.17f * h, 1.76f * h, 0.34f * h, OUT);
    Circ(-0.4f * h, 0, 0.1f * h, WHITE);
    Circ(0.4f * h, 0, 0.1f * h, WHITE);
}

// 3. «Руна» — рамка, ромб и вертикальная черта.
void Cube_Rune(float h, Color c1, Color c2) {
    CubeBase(h, c1);
    RL(-0.72f * h, -0.72f * h, 1.44f * h, 1.44f * h, 0.09f * h, c2);
    DrawPolyLinesEx({0, 0}, 4, 0.46f * h, 0, 0.09f * h, c2);
    Line(0, -0.46f * h, 0, 0.46f * h, 0.09f * h, c2);
    Line(-0.2f * h, 0.12f * h, 0.2f * h, -0.12f * h, 0.07f * h, OUT);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) Circ(sx * 0.52f * h, sy * 0.52f * h, 0.07f * h, c2);
}

// 4. «Схема» — дорожки печатной платы с узлами.
void Cube_Circuit(float h, Color c1, Color c2) {
    CubeBase(h, c1);
    float t = 0.09f * h;
    Line(-0.88f * h, -0.45f * h, -0.2f * h, -0.45f * h, t, c2);
    Line(-0.2f * h, -0.45f * h, 0.1f * h, -0.12f * h, t, c2);
    Line(0.1f * h, -0.12f * h, 0.88f * h, -0.12f * h, t, c2);
    Line(0.3f * h, -0.88f * h, 0.3f * h, -0.5f * h, t, c2);
    Line(-0.1f * h, 0.52f * h, 0.88f * h, 0.52f * h, t, c2);
    Line(0.45f * h, 0.52f * h, 0.45f * h, 0.2f * h, t, c2);
    Vector2 nodes[] = {{-0.2f, -0.45f}, {0.1f, -0.12f}, {0.3f, -0.5f}, {0.45f, 0.2f}, {-0.1f, 0.52f}};
    for (Vector2 n : nodes) {
        Circ(n.x * h, n.y * h, 0.13f * h, c2);
        Circ(n.x * h, n.y * h, 0.06f * h, OUT);
    }
    R(-0.72f * h, 0.02f * h, 0.42f * h, 0.42f * h, OUT);
    R(-0.6f * h, 0.14f * h, 0.18f * h, 0.18f * h, c2);
}

// 5. «Шахматы» — сетка 4x4 с тёмным ядром.
void Cube_Checker(float h, Color c1, Color c2) {
    CubeBase(h, c1);
    float in = 0.88f * h, cell = 2 * in / 4;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if ((i + j) % 2) R(-in + i * cell, -in + j * cell, cell, cell, c2);
    R(-0.34f * h, -0.34f * h, 0.68f * h, 0.68f * h, OUT);
    R(-0.2f * h, -0.2f * h, 0.4f * h, 0.4f * h, c1);
    R(-0.07f * h, -0.07f * h, 0.14f * h, 0.14f * h, WHITE);
}

// 6. «Робот» — скруглённый визор, датчики и решётка.
void Cube_Visor(float h, Color c1, Color c2) {
    CubeBase(h, c1);
    R(-0.55f * h, -0.88f * h, 0.2f * h, 0.3f * h, c2);
    R(0.35f * h, -0.88f * h, 0.2f * h, 0.3f * h, c2);
    RR(-0.76f * h, -0.42f * h, 1.52f * h, 0.56f * h, 0.6f, OUT);
    Circ(-0.36f * h, -0.14f * h, 0.14f * h, c2);
    Circ(0.36f * h, -0.14f * h, 0.14f * h, c2);
    Circ(-0.32f * h, -0.18f * h, 0.05f * h, WHITE);
    Circ(0.40f * h, -0.18f * h, 0.05f * h, WHITE);
    for (int i = 0; i < 4; ++i) R((-0.58f + i * 0.34f) * h, 0.34f * h, 0.14f * h, 0.4f * h, Fade(OUT, 0.8f));
}

// 7. «Мишень» — концентрические квадраты и прицел.
void Cube_Target(float h, Color c1, Color c2) {
    CubeBase(h, c1);
    RL(-0.7f * h, -0.7f * h, 1.4f * h, 1.4f * h, 0.12f * h, c2);
    R(-0.42f * h, -0.42f * h, 0.84f * h, 0.84f * h, OUT);
    R(-0.28f * h, -0.28f * h, 0.56f * h, 0.56f * h, c2);
    Circ(0, 0, 0.12f * h, OUT);
    float t = 0.1f * h;
    Line(0, -0.88f * h, 0, -0.56f * h, t, c2);
    Line(0, 0.56f * h, 0, 0.88f * h, t, c2);
    Line(-0.88f * h, 0, -0.56f * h, 0, t, c2);
    Line(0.56f * h, 0, 0.88f * h, 0, t, c2);
}

// ============================================================ ШАРЫ
void BallBase(float r, Color c1) {
    Circ(0, 0, r, OUT);
    Circ(0, 0, r * 0.86f, c1);
}

// 0. «Колесо» — обод и шесть спиц.
void Ball_Wheel(float r, Color c1, Color c2) {
    BallBase(r, c1);
    Ring(0, 0, 0.55f * r, 0.7f * r, c2);
    for (int i = 0; i < 6; ++i) {
        float a = i * 60.0f * DEG2RAD;
        Line(0, 0, cosf(a) * 0.6f * r, sinf(a) * 0.6f * r, 0.11f * r, c2);
    }
    Circ(0, 0, 0.22f * r, OUT);
    Circ(0, 0, 0.1f * r, c2);
}

// 1. «Око» — большой глаз, катящийся вместе с шаром.
void Ball_Eye(float r, Color c1, Color c2) {
    BallBase(r, c1);
    Circ(0, 0, 0.58f * r, OUT);
    Circ(0, 0, 0.5f * r, c2);
    Circ(0.16f * r, 0, 0.28f * r, OUT);
    Circ(0.26f * r, -0.1f * r, 0.08f * r, WHITE);
    for (int i = 0; i < 3; ++i) {
        float a = (200 + i * 35) * DEG2RAD;
        Line(cosf(a) * 0.62f * r, sinf(a) * 0.62f * r, cosf(a) * 0.82f * r, sinf(a) * 0.82f * r, 0.08f * r, OUT);
    }
}

// 2. «Раскол» — две половины разного цвета и тёмная сердцевина.
void Ball_Split(float r, Color c1, Color c2) {
    Circ(0, 0, r, OUT);
    DrawCircleSector({0, 0}, 0.86f * r, 0, 180, 24, c1);
    DrawCircleSector({0, 0}, 0.86f * r, 180, 360, 24, c2);
    R(-0.86f * r, -0.05f * r, 1.72f * r, 0.1f * r, OUT);
    Ring(0, 0, 0.26f * r, 0.4f * r, OUT);
    Circ(0, 0, 0.26f * r, WHITE);
    Circ(0.55f * r, 0.4f * r, 0.1f * r, c2);
    Circ(-0.55f * r, -0.4f * r, 0.1f * r, c1);
}

// 3. «Шестерня» — зубцы по краю и отверстия.
void Ball_Gear(float r, Color c1, Color c2) {
    for (int i = 0; i < 8; ++i) {
        float w = 0.38f * r;
        DrawRectanglePro({0, 0, w, 0.4f * r}, {w * 0.5f, r}, i * 45.0f, OUT);
        DrawRectanglePro({0, 0, w * 0.6f, 0.3f * r}, {w * 0.3f, r * 0.94f}, i * 45.0f, c2);
    }
    Circ(0, 0, 0.8f * r, OUT);
    Circ(0, 0, 0.7f * r, c1);
    for (int i = 0; i < 4; ++i) {
        float a = (45 + i * 90) * DEG2RAD;
        Circ(cosf(a) * 0.42f * r, sinf(a) * 0.42f * r, 0.13f * r, c2);
    }
    Circ(0, 0, 0.2f * r, OUT);
}

// 4. «Звёздное ядро» — пятиконечная звезда.
void Ball_Star(float r, Color c1, Color c2) {
    BallBase(r, c1);
    Star(0, 0, 0.8f * r, 0.36f * r, 5, -90, OUT);
    Star(0, 0, 0.66f * r, 0.28f * r, 5, -90, c2);
    Circ(0, 0, 0.14f * r, OUT);
}

// 5. «Атом» — три орбиты и ядро.
void Ball_Atom(float r, Color c1, Color c2) {
    BallBase(r, c1);
    for (int k = 0; k < 3; ++k) {
        rlPushMatrix();
        rlRotatef(k * 60.0f, 0, 0, 1);
        EllipseRing(0, 0, 0.74f * r, 0.26f * r, 0.08f * r, c2);
        Circ(0.74f * r, 0, 0.09f * r, WHITE);
        rlPopMatrix();
    }
    Circ(0, 0, 0.18f * r, OUT);
    Circ(0, 0, 0.11f * r, c2);
}

// 6. «Квадранты» — четыре сектора и кольцо.
void Ball_Quadrant(float r, Color c1, Color c2) {
    Circ(0, 0, r, OUT);
    for (int i = 0; i < 4; ++i)
        DrawCircleSector({0, 0}, 0.86f * r, i * 90.0f, i * 90.0f + 90.0f, 12, (i % 2) ? c2 : c1);
    Ring(0, 0, 0.42f * r, 0.54f * r, OUT);
    Circ(0, 0, 0.42f * r, c1);
    Circ(0, 0, 0.18f * r, c2);
}

// 7. «Рунный шар» — треугольная руна и три точки.
void Ball_Rune(float r, Color c1, Color c2) {
    BallBase(r, c1);
    DrawPolyLinesEx({0, 0}, 3, 0.56f * r, 0, 0.1f * r, c2);
    DrawPoly({0, 0}, 3, 0.2f * r, 180, OUT);
    for (int i = 0; i < 3; ++i) {
        float a = (60 + i * 120) * DEG2RAD;
        Circ(cosf(a) * 0.64f * r, sinf(a) * 0.64f * r, 0.09f * r, c2);
    }
}

// ============================================================ КОРАБЛИ
// Размеры: x в [-1.35h, 1.35h], y в [-0.2h, 0.9h]; кабина над корпусом.

// 0. «Наконечник» — клиновидный корпус.
void Ship_Arrow(float h, Color c1, Color c2) {
    float t = 0.1f * h;
    R(-1.38f * h, 0.12f * h, 0.24f * h, 0.36f * h, c2);
    QuadO({1.35f * h, 0.28f * h}, {-0.2f * h, -0.05f * h}, {-1.15f * h, 0.05f * h}, {-1.1f * h, 0.75f * h}, c1, t);
    TriO({0.8f * h, 0.3f * h}, {-0.55f * h, 0.18f * h}, {-0.6f * h, 0.52f * h}, c2, t * 0.7f);
}

// 1. «Скат» — широкое плоское тело с плавниками.
void Ship_Manta(float h, Color c1, Color c2) {
    float t = 0.1f * h;
    TriO({-0.6f * h, 0.3f * h}, {-1.35f * h, -0.25f * h}, {-0.2f * h, 0.15f * h}, c2, t);
    TriO({-0.4f * h, 0.45f * h}, {-1.3f * h, 0.9f * h}, {0.1f * h, 0.55f * h}, c2, t);
    Ellipse(0, 0.35f * h, 1.3f * h, 0.42f * h, OUT);
    Ellipse(0, 0.35f * h, 1.18f * h, 0.31f * h, c1);
    Ellipse(0.3f * h, 0.38f * h, 0.6f * h, 0.12f * h, c2);
    Circ(0.95f * h, 0.28f * h, 0.08f * h, OUT);
}

// 2. «Ракета» — капсула, обтекатель и стабилизаторы.
void Ship_Rocket(float h, Color c1, Color c2) {
    float t = 0.09f * h;
    TriO({-0.55f * h, 0.05f * h}, {-1.25f * h, -0.3f * h}, {-1.0f * h, 0.05f * h}, c2, t);
    TriO({-0.55f * h, 0.65f * h}, {-1.25f * h, 1.0f * h}, {-1.0f * h, 0.65f * h}, c2, t);
    RR(-1.1f * h, 0.0f, 2.0f * h, 0.7f * h, 0.9f, OUT);
    RR(-1.02f * h, 0.08f * h, 1.84f * h, 0.54f * h, 0.9f, c1);
    TriO({1.38f * h, 0.35f * h}, {0.75f * h, 0.02f * h}, {0.75f * h, 0.68f * h}, c2, t);
    Circ(0.3f * h, 0.35f * h, 0.18f * h, OUT);
    Circ(0.3f * h, 0.35f * h, 0.1f * h, WHITE);
    R(-0.7f * h, 0.3f * h, 0.6f * h, 0.1f * h, c2);
}

// 3. «Диск» — летающая тарелка с прозрачным куполом.
void Ship_Saucer(float h, Color c1, Color c2) {
    // купол полупрозрачный — сквозь него виден пилот
    DrawCircleSector({0, 0.3f * h}, 0.62f * h, 180, 360, 24, Fade(c2, 0.35f));
    DrawRing({0, 0.3f * h}, 0.56f * h, 0.64f * h, 180, 360, 24, OUT);
    Ellipse(0, 0.72f * h, 0.55f * h, 0.12f * h, Fade(c2, 0.7f));
    Ellipse(0, 0.35f * h, 1.35f * h, 0.38f * h, OUT);
    Ellipse(0, 0.35f * h, 1.24f * h, 0.28f * h, c1);
    Ellipse(0, 0.3f * h, 1.0f * h, 0.1f * h, Mix(c1, WHITE, 0.3f));
    for (int i = -2; i <= 2; ++i) Circ(i * 0.45f * h, 0.45f * h, 0.08f * h, c2);
}

// 4. «Сокол» — корпус-клин и крылья крестом.
void Ship_Falcon(float h, Color c1, Color c2) {
    float t = 0.09f * h;
    QuadO({-0.3f * h, 0.25f * h}, {-1.25f * h, -0.35f * h}, {-0.9f * h, -0.35f * h}, {0.15f * h, 0.25f * h}, c2, t);
    QuadO({-0.3f * h, 0.35f * h}, {-1.25f * h, 0.95f * h}, {-0.9f * h, 0.95f * h}, {0.15f * h, 0.35f * h}, c2, t);
    TriO({1.38f * h, 0.3f * h}, {-1.15f * h, 0.0f}, {-1.15f * h, 0.6f * h}, c1, t);
    Line(-0.9f * h, 0.3f * h, 0.9f * h, 0.3f * h, 0.1f * h, c2);
    Circ(-1.15f * h, 0.3f * h, 0.12f * h, c2);
}

// 5. «Кои» — рыба с хвостом, плавником и чешуёй.
void Ship_Koi(float h, Color c1, Color c2) {
    float t = 0.09f * h;
    TriO({-0.7f * h, 0.35f * h}, {-1.38f * h, -0.1f * h}, {-1.38f * h, 0.8f * h}, c2, t);
    Ellipse(0.1f * h, 0.35f * h, 1.05f * h, 0.48f * h, OUT);
    Ellipse(0.1f * h, 0.35f * h, 0.95f * h, 0.38f * h, c1);
    TriO({0.1f * h, 0.55f * h}, {-0.35f * h, 0.95f * h}, {0.35f * h, 0.65f * h}, c2, t * 0.8f);
    Ring(-0.3f * h, 0.3f * h, 0.12f * h, 0.19f * h, c2);
    Ring(0.1f * h, 0.42f * h, 0.12f * h, 0.19f * h, c2);
    Circ(0.78f * h, 0.24f * h, 0.13f * h, WHITE);
    Circ(0.82f * h, 0.24f * h, 0.07f * h, OUT);
}

// 6. «Баржа» — угловатый корпус с полосами и соплами.
void Ship_Barge(float h, Color c1, Color c2) {
    R(-1.38f * h, 0.12f * h, 0.26f * h, 0.16f * h, OUT);
    R(-1.38f * h, 0.42f * h, 0.26f * h, 0.16f * h, OUT);
    R(-1.18f * h, 0.0f, 2.26f * h, 0.7f * h, OUT);
    R(-1.08f * h, 0.1f * h, 2.06f * h, 0.5f * h, c1);
    for (int i = 0; i < 4; ++i) R((-0.95f + i * 0.45f) * h, 0.1f * h, 0.18f * h, 0.5f * h, c2);
    TriO({1.08f * h, 0.0f}, {1.38f * h, 0.35f * h}, {1.08f * h, 0.7f * h}, c2, 0.09f * h);
}

// 7. «Стрекоза» — сегментированное тело и прозрачные крылья.
void Ship_Dragonfly(float h, Color c1, Color c2) {
    float t = 0.07f * h;
    Color wing = Fade(c2, 0.75f);
    TriO({-0.1f * h, 0.25f * h}, {-1.2f * h, -0.25f * h}, {-0.85f * h, 0.2f * h}, wing, t);
    TriO({-0.1f * h, 0.4f * h}, {-1.2f * h, 0.9f * h}, {-0.85f * h, 0.45f * h}, wing, t);
    TriO({0.25f * h, 0.25f * h}, {-0.45f * h, -0.3f * h}, {-0.3f * h, 0.2f * h}, wing, t);
    RR(-1.38f * h, 0.24f * h, 1.5f * h, 0.22f * h, 1.0f, OUT);
    RR(-1.32f * h, 0.28f * h, 1.38f * h, 0.14f * h, 1.0f, c1);
    Circ(0.4f * h, 0.35f * h, 0.34f * h, OUT);
    Circ(0.4f * h, 0.35f * h, 0.26f * h, c1);
    Circ(1.0f * h, 0.33f * h, 0.3f * h, OUT);
    Circ(1.0f * h, 0.33f * h, 0.22f * h, c1);
    Circ(1.1f * h, 0.26f * h, 0.1f * h, c2);
}

using IconFn = void (*)(float, Color, Color);
const IconFn CUBES[CUBE_COUNT] = {Cube_Sentinel, Cube_Cyclops, Cube_Tiger, Cube_Rune,
                                  Cube_Circuit, Cube_Checker, Cube_Visor, Cube_Target};
const IconFn BALLS[BALL_COUNT] = {Ball_Wheel, Ball_Eye, Ball_Split, Ball_Gear,
                                  Ball_Star, Ball_Atom, Ball_Quadrant, Ball_Rune};
const IconFn SHIPS[SHIP_COUNT] = {Ship_Arrow, Ship_Manta, Ship_Rocket, Ship_Saucer,
                                  Ship_Falcon, Ship_Koi, Ship_Barge, Ship_Dragonfly};
const char* CUBE_NAMES[CUBE_COUNT] = {"Sentinel", "Cyclops", "Tiger", "Rune", "Circuit", "Checker", "Visor", "Target"};
const char* BALL_NAMES[BALL_COUNT] = {"Wheel", "Eye", "Split", "Gear", "Star Core", "Atom", "Quadrant", "Rune Orb"};
const char* SHIP_NAMES[SHIP_COUNT] = {"Arrowhead", "Manta", "Rocket", "Saucer", "Falcon", "Koi", "Barge", "Dragonfly"};

const Color PALETTE[PALETTE_SIZE] = {
    {255, 70, 70, 255},   {255, 145, 40, 255},  {255, 225, 50, 255},  {150, 235, 60, 255},
    {40, 205, 100, 255},  {40, 225, 215, 255},  {70, 165, 255, 255},  {80, 90, 245, 255},
    {165, 80, 245, 255},  {245, 90, 205, 255},  {250, 250, 250, 255}, {55, 55, 70, 255},
};

int Wrap(int id, int n) { return ((id % n) + n) % n; }

// Общая обёртка: перенос/поворот/масштаб, отключение отсечения граней
// (иначе треугольники с "неправильным" обходом не видны).
template <typename Fn>
void WithTransform(Vector2 center, float rotation, float sx, float sy, Fn fn) {
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlPushMatrix();
    rlTranslatef(center.x, center.y, 0);
    rlRotatef(rotation, 0, 0, 1);
    rlScalef(sx, sy, 1);
    fn();
    rlPopMatrix();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

} // namespace

Color Palette(int index) { return PALETTE[Wrap(index, PALETTE_SIZE)]; }

int Count(PlayerMode mode) {
    switch (mode) {
        case PlayerMode::Ball: return BALL_COUNT;
        case PlayerMode::Ship: return SHIP_COUNT;
        default: return CUBE_COUNT;
    }
}

const char* Name(PlayerMode mode, int id) {
    switch (mode) {
        case PlayerMode::Ball: return BALL_NAMES[Wrap(id, BALL_COUNT)];
        case PlayerMode::Ship: return SHIP_NAMES[Wrap(id, SHIP_COUNT)];
        default: return CUBE_NAMES[Wrap(id, CUBE_COUNT)];
    }
}

void DrawCube(int id, Vector2 center, float size, float rotation, Color c1, Color c2) {
    WithTransform(center, rotation, 1, 1, [&] { CUBES[Wrap(id, CUBE_COUNT)](size * 0.5f, c1, c2); });
}

void DrawBall(int id, Vector2 center, float size, float rotation, Color c1, Color c2) {
    WithTransform(center, rotation, 1, 1, [&] { BALLS[Wrap(id, BALL_COUNT)](size * 0.5f, c1, c2); });
}

void DrawShip(int id, Vector2 center, float size, float rotation, bool flipY, Color c1, Color c2, int cubeId) {
    float h = size * 0.5f;
    WithTransform(center, rotation, 1, flipY ? -1.0f : 1.0f, [&] {
        rlTranslatef(0, -0.3f * h, 0);          // центрируем силуэт по вертикали
        if (cubeId >= 0) {                      // пилот-куб в кабине
            rlPushMatrix();
            rlTranslatef(-0.15f * h, -0.35f * h, 0);
            CUBES[Wrap(cubeId, CUBE_COUNT)](0.42f * h, c1, c2);
            rlPopMatrix();
        }
        SHIPS[Wrap(id, SHIP_COUNT)](h, c1, c2);
    });
}

void DrawIcon(PlayerMode mode, int id, Vector2 center, float size, float rotation,
              Color c1, Color c2, int cubeIdForShip) {
    switch (mode) {
        case PlayerMode::Cube: DrawCube(id, center, size, rotation, c1, c2); break;
        case PlayerMode::Ball: DrawBall(id, center, size, rotation, c1, c2); break;
        case PlayerMode::Ship: DrawShip(id, center, size, rotation, false, c1, c2, cubeIdForShip); break;
    }
}

} // namespace icons
