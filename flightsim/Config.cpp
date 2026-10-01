#include "Config.h"
#include "raylib.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
const char* Path() { return TextFormat("%sflightsim.cfg", GetApplicationDirectory()); }
}

void Config::Load()
{
    FILE* f = fopen(Path(), "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        std::string key = line;
        int v = atoi(eq + 1);
        if (key == "lang_ru") ru = v != 0;
        else if (key == "mouse_yoke") mouseYoke = v != 0;
        else if (key == "show_fps") showFps = v != 0;
        else if (key == "volume") volume = v;
        else if (key == "free_aircraft") freeAircraft = v;
        else if (key == "free_airport") freeAirport = v;
        else if (key == "free_start") freeStart = v;
        else if (key == "free_time") freeTime = v;
        else if (key == "free_weather") freeWeather = v;
        else if (key == "free_wind") freeWind = v;
        else if (key == "free_fuel") freeFuel = v;
        else if (key == "free_failures") freeFailures = v;
        else if (key.rfind("stars.", 0) == 0) stars[key.substr(6)] = v;
    }
    fclose(f);
}

void Config::Save() const
{
    FILE* f = fopen(Path(), "w");
    if (!f) return;
    fprintf(f, "lang_ru=%d\nmouse_yoke=%d\nshow_fps=%d\nvolume=%d\n", ru, mouseYoke, showFps, volume);
    fprintf(f, "free_aircraft=%d\nfree_airport=%d\nfree_start=%d\nfree_time=%d\nfree_weather=%d\nfree_wind=%d\n", freeAircraft,
            freeAirport, freeStart, freeTime, freeWeather, freeWind);
    fprintf(f, "free_fuel=%d\nfree_failures=%d\n", freeFuel, freeFailures);
    for (const auto& kv : stars) fprintf(f, "stars.%s=%d\n", kv.first.c_str(), kv.second);
    fclose(f);
}
