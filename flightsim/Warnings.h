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

// Голосовой отсчёт радиовысоты на посадке: 1000, 500, «Minimums» (200), 100, 50, 40, 30, 20, 10, «Retard».
enum class Callout { C1000, C500, Minimums, C100, C50, C40, C30, C20, C10, Retard, Count };

class CalloutSystem {
public:
    void Reset();
    void Update(const Aircraft& a, float raFt, float throttle, float dt);
    // Следующее сообщение для проигрывания (Callout::Count — нет).
    Callout Pop();
    static const char* Text(Callout c);
    static const char* FileName(Callout c);

    Callout shown = Callout::Count;   // что показать на экране
    float showTimer = 0;

private:
    static constexpr int N = (int)Callout::Count;
    bool armed_[N] = {};
    Callout queue_[4] = {};
    int queueLen_ = 0;
    float retardTimer_ = 0;
    void Push(Callout c);
};
