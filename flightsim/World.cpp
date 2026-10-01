#include "World.h"

namespace {

const Airport kAirports[] = {
    {"Island International", "Аэропорт «Остров»",
     Runway{{"09", "27"}, {0.0f, 60.0f, 0.0f}, 90.0f, 1300.0f, 22.5f, true}, true},
    // Горная площадка на полке среди хребтов: короткая полоса без ILS, только PAPI.
    {"Pereval mountain strip", "Горный аэродром «Перевал»",
     Runway{{"16", "34"}, {9000.0f, 480.0f, -7500.0f}, 165.0f, 600.0f, 15.0f, false}, false},
};

} // namespace

namespace world {

int AirportCount() { return (int)(sizeof(kAirports) / sizeof(kAirports[0])); }

const Airport& GetAirport(int i) { return kAirports[i]; }

int NearestAirport(float x, float z, float* dist)
{
    int best = 0;
    float bestD = 1e30f;
    for (int i = 0; i < AirportCount(); ++i) {
        float d = kAirports[i].rwy.Dist(x, z);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    if (dist) *dist = bestD;
    return best;
}

} // namespace world
