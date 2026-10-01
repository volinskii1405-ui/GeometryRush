#pragma once

class Aircraft;
class Terrain;

// Сигнализация: GPWS/TAWS (земля), превышение скорости, крен, сваливание.
enum class Alert { PullUp, Terrain, SinkRate, TooLowGear, BankAngle, Overspeed, Stall, Count };

class WarningSystem {
public:
    void Reset();
    void Update(const Aircraft& a, const Terrain& t, float dt);

    bool Active(Alert a) const { return active_[(int)a]; }
    static const char* Text(Alert a);
    static bool IsWarning(Alert a);  // красная (WARNING) или жёлтая (CAUTION)
    bool AnyWarning() const;
    bool AnyCaution() const;
    // Самое приоритетное голосовое сообщение GPWS (или Alert::Count, если тихо).
    Alert TopVoice() const;

    float radioAltFt = 0;      // радиовысота
    float timeToImpact = -1;   // прогноз столкновения с рельефом, с (-1 — нет)

private:
    static constexpr int N = (int)Alert::Count;
    bool active_[N] = {};
    float hold_[N] = {};
};
