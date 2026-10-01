#pragma once
#include "Ils.h"

class Terrain;
struct Controls;

// Автопилот, автомат тяги и командный пилотажный прибор (Flight Director).
//
// Директор работает всегда (если включён): по выбранным режимам считает, куда поставить нос
// и какой держать крен, и показывает это пурпурными планками на авиагоризонте.
// Автопилот (AP) сам выполняет эти команды штурвалом и триммером, автомат тяги (A/THR) держит скорость.
class Autopilot {
public:
    enum class Lat { Hdg, Loc };
    enum class Vert { TakeOff, Alt, Gs };

    // Установка целей при старте сценария.
    void Reset(float hdgDeg, float altFt, float spdKt, bool takeoff, bool armApp);
    // Вызывается на каждом шаге физики: пересчитывает режимы и команды, при включённом AP — рулит.
    void Update(const Aircraft& a, float raFt, Controls& c, float dt);

    void ToggleAp(const Aircraft& a, float raFt);
    void ToggleAthr(const Aircraft& a);
    void ToggleApp();
    // Отключение автопилота: пилот взялся за штурвал, срабатывание защиты или малая высота.
    void DisconnectAp(const char* reason);
    void DisconnectAthr();

    // ---- выбранные значения (задатчики на панели)
    float hdgBug = 90, altTarget = 3000, spdTarget = 180;

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
    bool takeoff_ = false;
    float prevIas_ = 0, accelKt_ = 0;
};
