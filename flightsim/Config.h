#pragma once
#include <map>
#include <string>

// Настройки и рекорды — файл flightsim.cfg рядом с программой (key=value).
struct Config {
    bool ru = true;
    bool mouseYoke = false;
    bool showFps = true;
    int volume = 80;           // %

    // последний выбор свободного полёта
    int freeAircraft = 0, freeAirport = 0, freeStart = 0, freeTime = 0, freeWeather = 0, freeWind = 0;
    int freeFuel = 2, freeFailures = 0, freeTemp = 0;

    std::map<std::string, int> stars;   // лучший результат задания (0..3)

    void Load();
    void Save() const;
    int Stars(const std::string& id) const
    {
        auto it = stars.find(id);
        return it == stars.end() ? 0 : it->second;
    }
};
