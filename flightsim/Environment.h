#pragma once
#include "FlightMath.h"
#include "UiText.h"

// Время суток и погода: всё, от чего зависит освещение, небо, туман и облака.
enum class TimeOfDay { Day, Sunset, Night, Count };
enum class WeatherKind { Clear, Scattered, Overcast, Rain, Fog, Count };

struct Environment {
    TimeOfDay time = TimeOfDay::Day;
    WeatherKind weather = WeatherKind::Clear;

    Vector3 sunDir{};          // направление на солнце (ночью — на луну)
    Vector3 sunLight{};        // цвет и сила прямого света
    Vector3 ambient{};         // рассеянный свет неба
    Color zenith{}, horizon{}; // цвета неба
    Color fogColor{};
    float visibility = 60000;  // м
    bool clouds = false, overcast = false, rain = false;
    float cloudBase = 0, cloudTop = 0;   // м над уровнем моря
    bool night = false, dusk = false;    // ночные огни; огни в сумерках
    bool showSun = true;
    float seaTempC = 15;       // температура у моря; с высотой падает на 6.5 °C/км
    float qnh = 1013.0f;       // давление у моря, гПа (сообщает диспетчер)
    float thermals = 0;        // сила термиков 0..1.5 (солнце греет сушу)

    float IsaDev() const { return seaTempC - 15.0f; }
    static const char* TempName(int t)
    {
        switch (t) {
        case 1: return ui::L("Heat (+20 C above standard)", "Жара (на 20 °C теплее нормы)");
        case 2: return ui::L("Frost (-20 C below standard)", "Мороз (на 20 °C холоднее нормы)");
        default: return ui::L("Standard", "Обычная");
        }
    }

    float OatAt(float altM) const { return seaTempC - 0.0065f * altM; }
    bool InCloud(float altM) const { return overcast && altM > cloudBase && altM < cloudTop; }

    static const char* TimeName(TimeOfDay t)
    {
        switch (t) {
        case TimeOfDay::Day: return ui::L("Day", "День");
        case TimeOfDay::Sunset: return ui::L("Sunset", "Закат");
        case TimeOfDay::Night: return ui::L("Night", "Ночь");
        default: return "";
        }
    }
    static const char* WeatherName(WeatherKind w)
    {
        switch (w) {
        case WeatherKind::Clear: return ui::L("Clear", "Ясно");
        case WeatherKind::Scattered: return ui::L("Scattered clouds", "Кучевые облака");
        case WeatherKind::Overcast: return ui::L("Overcast 1500 ft", "Сплошная облачность 1500 ft");
        case WeatherKind::Rain: return ui::L("Rain, low clouds", "Дождь, низкие облака");
        case WeatherKind::Fog: return ui::L("Fog: vis 800 m, ceiling 250 ft", "Туман: видимость 800 м, нижний край 250 ft");
        default: return "";
        }
    }

    // temp: 0 — обычная температура, 1 — жара, 2 — мороз.
    static Environment Make(TimeOfDay t, WeatherKind w, int temp = 0)
    {
        Environment e;
        e.time = t;
        e.weather = w;
        auto rgb = [](int r, int g, int b) { return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, 255}; };
        switch (t) {
        case TimeOfDay::Day:
            e.sunDir = Vector3Normalize({0.45f, 0.75f, -0.35f});
            e.sunLight = {0.74f, 0.71f, 0.65f};
            e.ambient = {0.50f, 0.51f, 0.54f};
            e.zenith = rgb(62, 112, 190);
            e.horizon = rgb(196, 212, 228);
            break;
        case TimeOfDay::Sunset:
            e.sunDir = Vector3Normalize({-0.96f, 0.10f, 0.25f});
            e.sunLight = {0.85f, 0.50f, 0.28f};
            e.ambient = {0.30f, 0.27f, 0.32f};
            e.zenith = rgb(52, 70, 128);
            e.horizon = rgb(242, 152, 96);
            e.dusk = true;
            break;
        default:
            e.sunDir = Vector3Normalize({0.3f, 0.6f, 0.25f});   // луна
            e.sunLight = {0.10f, 0.12f, 0.17f};
            e.ambient = {0.07f, 0.08f, 0.11f};
            e.zenith = rgb(4, 7, 16);
            e.horizon = rgb(20, 26, 40);
            e.night = e.dusk = true;
            break;
        }
        e.visibility = 60000;
        auto grey = [&](float k) {
            // Облачность: небо серое, прямого света почти нет.
            Color base = e.night ? rgb(14, 16, 22) : (t == TimeOfDay::Sunset ? rgb(120, 104, 104) : rgb(172, 176, 182));
            e.horizon = base;
            e.zenith = Color{(unsigned char)(base.r * k), (unsigned char)(base.g * k), (unsigned char)(base.b * k), 255};
            e.sunLight = Vector3Scale(e.sunLight, 0.25f);
            e.ambient = Vector3Scale(e.ambient, 1.15f);
            e.showSun = false;
        };
        switch (w) {
        case WeatherKind::Clear: break;
        case WeatherKind::Scattered:
            e.clouds = true;
            e.cloudBase = 1300;
            e.cloudTop = 1900;
            e.visibility = 40000;
            break;
        case WeatherKind::Overcast:
            grey(0.85f);
            e.overcast = true;
            e.cloudBase = 60 + 1500 * fs::FT_TO_M;
            e.cloudTop = 1500;
            e.visibility = 15000;
            break;
        case WeatherKind::Rain:
            grey(0.75f);
            e.overcast = true;
            e.rain = true;
            e.cloudBase = 60 + 900 * fs::FT_TO_M;
            e.cloudTop = 2600;
            e.visibility = 5000;
            break;
        case WeatherKind::Fog:
            grey(0.95f);
            e.overcast = true;
            e.cloudBase = 60 + 250 * fs::FT_TO_M;
            e.cloudTop = 700;
            e.visibility = 800;
            break;
        default: break;
        }
        e.seaTempC = t == TimeOfDay::Day ? 15.0f : (t == TimeOfDay::Sunset ? 10.0f : 3.0f);
        if (w == WeatherKind::Overcast) e.seaTempC -= 2.0f;
        if (w == WeatherKind::Rain) e.seaTempC = fmaxf(e.seaTempC - 5.0f, 2.0f);
        if (w == WeatherKind::Fog) e.seaTempC -= 1.0f;
        if (temp == 1) e.seaTempC += 20.0f;
        if (temp == 2) e.seaTempC -= 20.0f;
        // Антициклон — ясно и давление высокое, циклон — дождь и низкое.
        switch (w) {
        case WeatherKind::Clear: e.qnh = 1022; break;
        case WeatherKind::Scattered: e.qnh = 1015; break;
        case WeatherKind::Overcast: e.qnh = 1006; break;
        case WeatherKind::Rain: e.qnh = 996; break;
        default: e.qnh = 1019; break;
        }
        // Термики: днём над сушей при ясной погоде или кучевых облаках (их «вершины» — облака).
        if (w == WeatherKind::Clear || w == WeatherKind::Scattered)
            e.thermals = t == TimeOfDay::Day ? 1.0f : (t == TimeOfDay::Sunset ? 0.3f : 0.0f);
        if (temp == 1) e.thermals *= 1.4f;
        if (temp == 2) e.thermals *= 0.5f;
        e.fogColor = e.horizon;
        if (e.overcast && !e.night) e.dusk = e.dusk || w == WeatherKind::Fog || w == WeatherKind::Rain;
        return e;
    }

    // Плотность тумана для шейдера: на расстоянии видимости туман почти полный.
    float FogDensity(float camAlt) const
    {
        float vis = visibility;
        if (overcast && camAlt > cloudBase && camAlt < cloudTop) vis = 120.0f;   // внутри облаков
        if (overcast && camAlt >= cloudTop) vis = fmaxf(visibility, 40000.0f);  // над облаками ясно
        return 2.08f / vis;
    }
};
