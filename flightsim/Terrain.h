#pragma once
#include "FlightMath.h"

#include <vector>

// Процедурный остров: горы, холмы, побережье и ровная площадка аэродрома.
// Высоты хранятся в регулярной сетке; запрос высоты интерполирует ровно по тем же
// треугольникам, что и рисуемая сетка, поэтому колёса стоят точно на картинке.
class Terrain {
public:
    // Главный аэродром (см. World.cpp): ВПП 09/27 вдоль оси X, центр в начале координат.
    static constexpr float kAirportElev = 60.0f;   // м над уровнем моря
    static constexpr float kRunwayHalfLen = 1300.0f;
    static constexpr float kRunwayHalfWidth = 22.5f;

    void Generate(unsigned seed);
    void BuildMeshes(Shader litShader);
    void Unload();
    void Draw() const;

    // Высота грунта (может быть < 0 — это дно моря).
    float GroundHeight(float x, float z) const;
    // Высота поверхности: грунт или вода (уровень моря = 0).
    float SurfaceHeight(float x, float z) const { return fmaxf(GroundHeight(x, z), 0.0f); }
    bool IsWater(float x, float z) const { return GroundHeight(x, z) < 0.0f; }
    Vector3 Normal(float x, float z) const;

    float HalfExtent() const { return -origin_; }
    Vector3 HighestPoint() const;
    // Густота леса 0..1 (та же, что в раскраске рельефа) — для расстановки деревьев.
    float ForestAmount(float x, float z) const;


    float RawHeight(float x, float z) const;

private:
    float At(int i, int j) const { return h_[(size_t)j * n_ + i]; }

    int n_ = 0;            // вершин по стороне
    float cell_ = 80.0f;   // м между вершинами
    float origin_ = 0.0f;  // координата первой вершины
    unsigned seed_ = 1;
    std::vector<float> h_;

    std::vector<Mesh> chunks_;
    Material material_{};
    Mesh water_{};
    bool built_ = false;
};
