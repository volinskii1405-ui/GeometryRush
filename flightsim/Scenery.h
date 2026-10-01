#pragma once
#include "FlightMath.h"

#include <vector>

class Terrain;

// Статичные объекты мира: леса, города с домами, дороги, уличные фонари.
// Всё собирается при запуске в крупные меши (по участкам карты) и рисуется одним вызовом на участок.
class Scenery {
public:
    void Build(const Terrain& t, Shader litShader);
    void Unload();
    // lights — ночью и в сумерках зажигаются окна домов.
    void Draw(Vector3 camPos, float drawDist, bool lights) const;
    // Позиции уличных фонарей (для ночного свечения).
    const std::vector<Vector3>& StreetLights() const { return streetLights_; }

    struct Chunk {
        Vector3 center;
        std::vector<Mesh> solid;    // освещаемые (деревья, дома, дороги)
        std::vector<Mesh> glow;     // светящиеся окна
    };

private:
    std::vector<Chunk> chunks_;
    std::vector<Vector3> streetLights_;
    Material lit_{}, unlit_{};
    bool built_ = false;
};
