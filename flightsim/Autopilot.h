#pragma once
#include "Ils.h"

#include <vector>

class Terrain;
struct Controls;

// Автопилот, автомат тяги и командный пилотажный прибор (Flight Director).
//
// Директор работает всегда (если включён): по выбранным режимам считает, куда поставить нос
// и какой держать крен, и показывает это пурпурными планками на авиагоризонте.
// Автопилот (AP) сам выполняет эти команды штурвалом и триммером, автомат тяги (A/THR) держит скорость.
// Точка маршрута (FMS): положение, имя и рекомендуемая высота (0 — нет).
struct Waypoint {
    Vector3 pos{};
    char name[12] = "";
    float altFt = 0;
};

class Autopilot {
public:
    enum class Lat { Hdg, Loc, Nav };
    enum class Vert { TakeOff, Alt, Gs, Vs, GoAround };

    // Установка целей при старте сценария.
    void Reset(float hdgDeg, float altFt, float spdKt, bool takeoff, bool armApp);
    // Вызывается на каждом шаге физики: пересчитывает режимы и команды, при включённом AP — рулит.
    void Update(const Aircraft& a, float raFt, Controls& c, float dt);

    void ToggleAp(const Aircraft& a, float raFt);
    void ToggleAthr(const Aircraft& a);
    void ToggleApp();
    // TO/GA: уход на второй круг — нос 12.5°, взлётный режим, набор высоты.
    void Toga(const Aircraft& a);
    // V/S: снижение/набор с заданной вертикальной скоростью до заданной высоты.
    void ToggleVs(const Aircraft& a);
    // Отключение автопилота: пилот взялся за штурвал, срабатывание защиты или малая высота.
    void DisconnectAp(const char* reason);
    void DisconnectAthr();

    // ---- маршрут (бортовой компьютер, FMS) и режим NAV: автопилот ведёт по точкам
    std::vector<Waypoint> route;
    int activeWp = 0;                 // к какой точке летим (== route.size() — маршрут пройден)
    bool navOn = false;
    bool vnavOn = true;               // VNAV: высоты точек маршрута выдерживает автопилот (F3)
    bool vnavActive = false;          // сейчас ведёт по высоте маршрута
    float navTrack = 0, navDistM = 0; // заданный путевой угол и расстояние до активной точки
    float navXtkM = 0;                // боковое уклонение от линии участка, м (+ — правее)
    void ToggleNav(const Aircraft& a);
    void AddWaypoint(Vector3 pos, const char* name, float altFt);
    void RemoveLastWaypoint();
    void RemoveWaypoint(int i);
    void RouteEdited() { resetLeg_ = true; }   // точку передвинули/добавили — пересчитать участок
    void ClearRoute();
    bool RouteActive() const { return activeWp < (int)route.size(); }

    // ---- выбранные значения (задатчики на панели)
    float hdgBug = 90, altTarget = 3000, spdTarget = 180, vsTarget = 0;

    // ---- состояние
    bool apOn = false, athrOn = false, fdOn = true;
    bool appArmed = false, locCaptured = false, gsCaptured = false;
    bool retard = false;              // автомат тяги убрал газ перед касанием
    Lat lat = Lat::Hdg;
    Vert vert = Vert::Alt;

    // ---- команды директора
    float fdPitch = 0, fdBank = 0;    // град

    // ---- сообщения
    float apOffTimer = 0;             // мигающее «AP OFF» и звук отключения
    bool apOffSound = false;          // нужно проиграть сигнал отключения (сбрасывает звук)
    const char* apOffReason = "";

    Ils ils;

private:
    bool takeoff_ = false, goAround_ = false, vsMode_ = false;
    float prevIas_ = 0, accelKt_ = 0;
    float baro_ = 1013.0f;
    // начало активного участка: предыдущая точка или (при «прямо на точку») положение самолёта
    Vector3 legFrom_{};
    bool direct_ = true, resetLeg_ = true;
    float vnavVs_ = 0;   // установка высотомера: автопилот держит высоту по нему, как в жизни
};
