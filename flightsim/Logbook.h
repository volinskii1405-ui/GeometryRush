#pragma once
#include <string>
#include <vector>

// Бортовой журнал: каждый полёт — строка в flightlog.txt рядом с программой.
struct LogEntry {
    long long when = 0;      // время начала (unix)
    int aircraft = 0;        // AircraftKind
    int fromAirport = 0;
    int toAirport = -1;      // где последний раз сел (-1 — не садился, -2 — вне аэродрома)
    float durationS = 0;     // налёт, с
    int landings = 0;
    float bestFpm = 0;       // самое мягкое касание (модуль, ft/мин), 0 — нет посадок
    float sumFpm = 0;        // сумма модулей — для средней
    int result = 0;          // 0 — нормально, 1 — авария
    int mission = -1;
};

class Logbook {
public:
    void Load();
    void Add(const LogEntry& e);   // дописать в файл
    const std::vector<LogEntry>& Entries() const { return entries_; }

    // Итоги
    float TotalHours(int aircraft = -1) const;
    int Flights() const { return (int)entries_.size(); }
    int Landings() const;
    int Crashes() const;
    float BestFpm() const;      // 0 — нет посадок
    float AverageFpm() const;

private:
    std::vector<LogEntry> entries_;
};
