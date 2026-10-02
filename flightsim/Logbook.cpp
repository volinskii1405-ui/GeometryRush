#include "Logbook.h"
#include "raylib.h"

#include <cstdio>

namespace {
const char* Path() { return TextFormat("%sflightlog.txt", GetApplicationDirectory()); }
}

void Logbook::Load()
{
    entries_.clear();
    FILE* f = fopen(Path(), "r");
    if (!f) return;
    LogEntry e;
    while (fscanf(f, "%lld %d %d %d %f %d %f %f %d %d", &e.when, &e.aircraft, &e.fromAirport, &e.toAirport, &e.durationS, &e.landings,
                  &e.bestFpm, &e.sumFpm, &e.result, &e.mission) == 10)
        entries_.push_back(e);
    fclose(f);
}

void Logbook::Add(const LogEntry& e)
{
    entries_.push_back(e);
    FILE* f = fopen(Path(), "a");
    if (!f) return;
    fprintf(f, "%lld %d %d %d %.1f %d %.1f %.1f %d %d\n", e.when, e.aircraft, e.fromAirport, e.toAirport, e.durationS, e.landings, e.bestFpm,
            e.sumFpm, e.result, e.mission);
    fclose(f);
}

float Logbook::TotalHours(int aircraft) const
{
    float s = 0;
    for (const LogEntry& e : entries_)
        if (aircraft < 0 || e.aircraft == aircraft) s += e.durationS;
    return s / 3600.0f;
}

int Logbook::Landings() const
{
    int n = 0;
    for (const LogEntry& e : entries_) n += e.landings;
    return n;
}

int Logbook::Crashes() const
{
    int n = 0;
    for (const LogEntry& e : entries_) n += e.result == 1;
    return n;
}

float Logbook::BestFpm() const
{
    float best = 0;
    for (const LogEntry& e : entries_)
        if (e.landings > 0 && (best == 0 || e.bestFpm < best)) best = e.bestFpm;
    return best;
}

float Logbook::AverageFpm() const
{
    float sum = 0;
    int n = 0;
    for (const LogEntry& e : entries_) {
        sum += e.sumFpm;
        n += e.landings;
    }
    return n ? sum / n : 0.0f;
}
