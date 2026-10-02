#pragma once
#include "raylib.h"
#include "Warnings.h"

#include <vector>

class Aircraft;
struct Controls;

// Весь звук синтезируется при запуске: шум двигателей и ветра — потоком,
// сигналы предупреждений — короткими звуками, которые повторяются, пока сигнал активен.
class AudioSystem {
public:
    void Init();
    void Shutdown();
    void Update(const Aircraft& a, const Controls& c, const WarningSystem& w, CalloutSystem& callouts, bool paused);
    void PlayApDisconnect();
    void PlayCrash();
    void PlayTouchdown(float strength, float speedMs);
    void PlayClick();
    void PlayRadio();   // щелчок тангенты и шум в эфире перед сообщением диспетчера
    void StopAll();

private:
    void FillEngine(short* out, int frames, const Aircraft& a, bool paused);

    bool ready_ = false;
    AudioStream engine_{};
    Sound alerts_[(int)Alert::Count] = {};
    Sound crash_{}, touchdown_{}, click_{}, apOff_{}, chime_{}, radio_{};
    // механика: шасси, закрылки, триммер, шины
    Sound gearThump_{}, gearMotor_{}, flapMotor_{}, trimTick_{}, screech_{};
    float prevGear_ = -1, prevFlaps_ = -1, prevTrim_ = 0, trimTimer_ = 0;
    bool prevCaution_[(int)Alert::Count] = {};
    Sound callouts_[(int)Callout::Count] = {};
    int playingCallout_ = -1;
    Alert lastVoice_ = Alert::Count;

    // состояние синтезатора
    float whinePhase_ = 0, whine2Phase_ = 0, rumblePhase_ = 0;
    float lpRoar_ = 0, lpWind_ = 0, lpWind2_ = 0, lpRumble_ = 0;
    float n1Smooth_ = 0, windSmooth_ = 0, rollSmooth_ = 0;
    std::vector<short> buf_;
};
