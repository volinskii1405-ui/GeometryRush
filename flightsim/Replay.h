#pragma once
#include "Aircraft.h"

#include <deque>

// Запись последних 40 секунд полёта (30 кадров/с) для повтора с разных камер.
struct ReplayFrame {
    float t = 0;
    Vector3 pos{}, vel{};
    Quaternion rot{0, 0, 0, 1};
    float gear = 1, flaps = 0, elevator = 0, aileron = 0, rudder = 0, speedbrake = 0, reverser = 0, noseSteer = 0, ice = 0;
    float n1[2] = {0, 0};
    bool fire[2] = {false, false};
    float ias = 0, gLoad = 1, alt = 0;
    bool crashed = false, crashFire = true, onGround = false, touchdown = false;
    float crashAge = -1, touchdownFpm = 0;
    bool lost[(int)Part::Count] = {};
    Debris debris[(int)Part::Count];
};

class ReplayRecorder {
public:
    static constexpr float kLength = 40.0f, kStep = 1.0f / 30.0f;

    void Clear() { frames_.clear(); next_ = 0; }
    void Record(const Aircraft& a, float t, float crashAge, bool touchdown)
    {
        if (touchdown) pendingTouchdown_ = true;
        if (t < next_) return;
        next_ = t + kStep;
        ReplayFrame f;
        f.t = t;
        f.pos = a.pos; f.vel = a.vel; f.rot = a.rot;
        f.gear = a.gear; f.flaps = a.flaps; f.elevator = a.elevator; f.aileron = a.aileron; f.rudder = a.rudder;
        f.speedbrake = a.speedbrake; f.reverser = a.reverser; f.noseSteer = a.noseSteer; f.ice = a.ice;
        for (int i = 0; i < 2; ++i) { f.n1[i] = a.engines[i].n1; f.fire[i] = a.engines[i].fire; }
        f.ias = a.ias; f.gLoad = a.gLoad; f.alt = a.pos.y;
        f.crashed = a.crashed; f.crashFire = a.crashFire; f.onGround = a.onGround;
        f.touchdown = pendingTouchdown_;
        f.touchdownFpm = a.touchdownFpm;
        pendingTouchdown_ = false;
        f.crashAge = crashAge;
        for (int i = 0; i < (int)Part::Count; ++i) { f.lost[i] = a.lost[i]; f.debris[i] = a.debris[i]; }
        frames_.push_back(f);
        while (!frames_.empty() && frames_.back().t - frames_.front().t > kLength) frames_.pop_front();
    }
    bool Empty() const { return frames_.size() < 2; }
    float Start() const { return frames_.empty() ? 0 : frames_.front().t; }
    float End() const { return frames_.empty() ? 0 : frames_.back().t; }

    // Состояние самолёта в момент t (между кадрами — интерполяция); touchdown — было ли касание в (prevT, t].
    void Apply(Aircraft& out, float t, float prevT, float* crashAge, bool* touchdown) const
    {
        *touchdown = false;
        if (frames_.empty()) return;
        size_t i = 0;
        while (i + 1 < frames_.size() && frames_[i + 1].t < t) ++i;
        const ReplayFrame& a = frames_[i];
        const ReplayFrame& b = frames_[i + 1 < frames_.size() ? i + 1 : i];
        float k = b.t > a.t ? fs::Clampf((t - a.t) / (b.t - a.t), 0.0f, 1.0f) : 0.0f;
        out.pos = Vector3Lerp(a.pos, b.pos, k);
        out.vel = Vector3Lerp(a.vel, b.vel, k);
        out.rot = QuaternionSlerp(a.rot, b.rot, k);
        auto L = [k](float x, float y) { return x + (y - x) * k; };
        out.gear = L(a.gear, b.gear); out.flaps = L(a.flaps, b.flaps); out.elevator = L(a.elevator, b.elevator);
        out.aileron = L(a.aileron, b.aileron); out.rudder = L(a.rudder, b.rudder); out.speedbrake = L(a.speedbrake, b.speedbrake);
        out.reverser = L(a.reverser, b.reverser); out.noseSteer = L(a.noseSteer, b.noseSteer); out.ice = a.ice;
        for (int e = 0; e < 2; ++e) { out.engines[e].n1 = L(a.n1[e], b.n1[e]); out.engines[e].fire = a.fire[e]; }
        out.ias = L(a.ias, b.ias); out.tas = out.ias; out.gLoad = L(a.gLoad, b.gLoad);
        out.crashed = a.crashed; out.crashFire = a.crashFire; out.onGround = a.onGround;
        out.touchdownFpm = a.touchdownFpm;
        for (int p = 0; p < (int)Part::Count; ++p) { out.lost[p] = a.lost[p]; out.debris[p] = a.debris[p]; }
        *crashAge = a.crashAge;
        for (const ReplayFrame& f : frames_)
            if (f.touchdown && f.t > prevT && f.t <= t) *touchdown = true;
    }
    const ReplayFrame* FrameAt(float t) const
    {
        if (frames_.empty()) return nullptr;
        size_t i = 0;
        while (i + 1 < frames_.size() && frames_[i + 1].t < t) ++i;
        return &frames_[i];
    }

private:
    std::deque<ReplayFrame> frames_;
    float next_ = 0;
    bool pendingTouchdown_ = false;
};
