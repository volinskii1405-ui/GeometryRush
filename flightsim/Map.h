#pragma once
#include "FlightMath.h"

#include <vector>

class Aircraft;
class Autopilot;
class Terrain;

// Карта острова (клавиша N): рельеф, аэродромы, ILS, пройденный путь, ветер и маршрут FMS.
// Мышью на карте строится маршрут для режима NAV автопилота.
class NavMap {
public:
    void Build(const Terrain& t);
    void Unload();
    // Ввод на открытой карте: точки маршрута, масштаб, готовые заходы.
    void Update(Autopilot& ap, const Aircraft& a);
    void Draw(const Aircraft& a, const Autopilot& ap, Vector3 wind, const std::vector<Vector2>& trail) const;

    bool open = false;
    bool HasSelection() const { return open && sel_ >= 0; }
    void Select(const Autopilot& ap, int i);   // выбрать точку маршрута (−1 — снять выбор)

private:
    Rectangle Area() const;
    Vector2 ToScreen(float x, float z) const;
    Vector2 ToWorld(Vector2 p) const;
    void AddApproach(Autopilot& ap, int airport, int end) const;

    Texture2D tex_{};
    float half_ = 20000.0f;    // половина размера острова, м
    float zoom_ = 1.0f;
    Vector2 center_{0, 0};     // центр карты, мир (x, z)
    int userWp_ = 0;           // счётчик для имён WP1, WP2…
    int sel_ = -1;             // выбранная точка маршрута (−1 — нет)
    bool dragging_ = false;    // точку тащат мышью
    mutable std::vector<Rectangle> rowRects_;   // строки списка точек (для выбора щелчком)
    int HitWaypoint(const Autopilot& ap, Vector2 m) const;

};
