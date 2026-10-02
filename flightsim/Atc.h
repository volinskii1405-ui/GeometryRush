#pragma once
#include "Aircraft.h"

#include <string>
#include <vector>

struct Environment;
struct FlightSetup;
class Autopilot;

// Диспетчер (текстом в окне «РАДИО»): разрешение на взлёт, переход на Подход, заход и посадка,
// новое давление QNH, предупреждение о сдвиге ветра, «уходите на второй круг — полоса занята».
class Atc {
public:
    void Start(const FlightSetup& s, const Environment& env, bool microburst);
    // Возвращает true, если в этом кадре прозвучало новое сообщение (для щелчка радио).
    bool Update(Aircraft& a, const Autopilot& ap, Environment& env, float radioAltFt, const char* windName, float dt);
    void Draw() const;   // 2D, поверх кабины

    bool TrafficVisible() const { return trafficOn_; }
    const Aircraft& Traffic() const { return traffic_; }

private:
    struct Msg { std::string text; float age; };
    void Say(const std::string& text);
    const char* Callsign(const Aircraft& a) const;
    int ApproachEnd(int airport, Vector3 pos) const;

    std::vector<Msg> msgs_;
    bool newMsg_ = false;
    float time_ = 0;
    bool freeFlight_ = true, runwayStart_ = false, microburst_ = false, finalCall_ = false;
    int startAirport_ = 0;
    bool takeoffCleared_ = false, departed_ = false, handedOff_ = false;
    int approachAirport_ = -1, approachEnd_ = 0;
    bool landingCleared_ = false, occupiedWarned_ = false, goAroundOrdered_ = false, landedMsg_ = false;
    float lastLiftoff_ = -1;
    float nextQnhChange_ = 0;
    // самолёт на полосе
    bool trafficOn_ = false, trafficRolling_ = false;
    float trafficSpeed_ = 0;
    Aircraft traffic_;
    Vector3 trafficDir_{};
};
