#include "Audio.h"
#include "Aircraft.h"

#include <cmath>
#include <cstdint>

namespace {

constexpr int RATE = 22050;
constexpr int STREAM_FRAMES = 1024;

uint32_t g_rng = 0x12345678u;
float Noise()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return (g_rng & 0xFFFF) / 32767.5f - 1.0f;
}

Sound MakeSound(const std::vector<float>& s)
{
    std::vector<short> pcm(s.size());
    for (size_t i = 0; i < s.size(); ++i) pcm[i] = (short)(fs::Clampf(s[i], -1.0f, 1.0f) * 32000.0f);
    Wave w{(unsigned)pcm.size(), RATE, 16, 1, pcm.data()};
    return LoadSoundFromWave(w);   // данные копируются, вектор можно освободить
}

// Тон со свипом частоты и мягкой огибающей; shape > 0 делает звук «квадратнее» и резче.
void Tone(std::vector<float>& b, float t0, float dur, float f0, float f1, float amp, float shape = 2.5f)
{
    int s0 = (int)(t0 * RATE), n = (int)(dur * RATE);
    float phase = 0;
    for (int i = 0; i < n && s0 + i < (int)b.size(); ++i) {
        float t = (float)i / n;
        float f = f0 + (f1 - f0) * t;
        phase += 2.0f * PI * f / RATE;
        float env = fminf(fminf(i / (0.006f * RATE), (n - i) / (0.012f * RATE)), 1.0f);
        float v = tanhf(shape * sinf(phase)) / tanhf(shape) * 0.8f + 0.2f * sinf(2.0f * phase);
        b[s0 + i] += amp * env * v;
    }
}

std::vector<float> Buffer(float seconds) { return std::vector<float>((size_t)(seconds * RATE), 0.0f); }

} // namespace

void AudioSystem::Init()
{
    InitAudioDevice();
    ready_ = IsAudioDeviceReady();
    if (!ready_) return;

    SetAudioStreamBufferSizeDefault(STREAM_FRAMES);
    engine_ = LoadAudioStream(RATE, 16, 1);
    buf_.assign(STREAM_FRAMES, 0);
    PlayAudioStream(engine_);

    {   // «Whoop-whoop, pull up»: два свипа сирены и две резкие ноты
        auto b = Buffer(1.9f);
        Tone(b, 0.00f, 0.38f, 320, 1050, 0.55f);
        Tone(b, 0.45f, 0.38f, 320, 1050, 0.55f);
        Tone(b, 1.00f, 0.28f, 640, 640, 0.5f, 4.0f);
        Tone(b, 1.34f, 0.32f, 860, 860, 0.5f, 4.0f);
        alerts_[(int)Alert::PullUp] = MakeSound(b);
    }
    {   // «Terrain, terrain»
        auto b = Buffer(1.3f);
        Tone(b, 0.0f, 0.24f, 920, 920, 0.45f);
        Tone(b, 0.30f, 0.26f, 700, 700, 0.45f);
        alerts_[(int)Alert::Terrain] = MakeSound(b);
    }
    {   // «Sink rate»: три коротких сигнала
        auto b = Buffer(1.2f);
        for (int i = 0; i < 3; ++i) Tone(b, 0.15f * i, 0.09f, 1150, 1150, 0.4f);
        alerts_[(int)Alert::SinkRate] = MakeSound(b);
    }
    {   // «Too low, gear»: гудок
        auto b = Buffer(1.1f);
        Tone(b, 0.0f, 0.5f, 420, 420, 0.4f, 6.0f);
        alerts_[(int)Alert::TooLowGear] = MakeSound(b);
    }
    {   // «Bank angle, bank angle»
        auto b = Buffer(1.5f);
        for (int i = 0; i < 2; ++i) {
            Tone(b, 0.45f * i, 0.15f, 1300, 1300, 0.4f);
            Tone(b, 0.45f * i + 0.18f, 0.15f, 980, 980, 0.4f);
        }
        alerts_[(int)Alert::BankAngle] = MakeSound(b);
    }
    {   // трещотка превышения скорости (clacker)
        auto b = Buffer(1.0f);
        for (int c = 0; c < 10; ++c) {
            int s0 = (int)(c * 0.1f * RATE);
            for (int i = 0; i < (int)(0.018f * RATE); ++i) {
                float env = expf(-i / (0.004f * RATE));
                float sq = sinf(2 * PI * 1900.0f * i / RATE) > 0 ? 1.0f : -1.0f;
                b[s0 + i] += env * (0.45f * sq + 0.35f * Noise());
            }
        }
        alerts_[(int)Alert::Overspeed] = MakeSound(b);
    }
    {   // тряска штурвала (stick shaker)
        auto b = Buffer(1.0f);
        float lp = 0;
        for (size_t i = 0; i < b.size(); ++i) {
            float t = (float)i / RATE;
            float mod = 0.5f + 0.5f * sinf(2 * PI * 17.0f * t);
            lp += 0.08f * (Noise() - lp);
            float saw = fmodf(t * 58.0f, 1.0f) * 2.0f - 1.0f;
            b[i] = mod * (0.35f * saw + 0.9f * lp);
        }
        alerts_[(int)Alert::Stall] = MakeSound(b);
    }
    {
        auto b = Buffer(2.5f);
        float lp = 0;
        for (size_t i = 0; i < b.size(); ++i) {
            float t = (float)i / RATE;
            lp += 0.06f * (Noise() - lp);
            b[i] = 2.2f * lp * expf(-t * 1.4f) + 0.3f * Noise() * expf(-t * 6.0f);
        }
        crash_ = MakeSound(b);
    }
    {
        auto b = Buffer(0.3f);
        float lp = 0;
        for (size_t i = 0; i < b.size(); ++i) {
            float t = (float)i / RATE;
            lp += 0.05f * (Noise() - lp);
            b[i] = 2.5f * lp * expf(-t * 14.0f) + 0.25f * sinf(2 * PI * 70 * t) * expf(-t * 12.0f);
        }
        touchdown_ = MakeSound(b);
    }
    {
        auto b = Buffer(0.03f);
        for (size_t i = 0; i < b.size(); ++i) b[i] = 0.5f * Noise() * expf(-(float)i / (0.004f * RATE));
        click_ = MakeSound(b);
    }

    // Записанные сигналы из папки sounds/ рядом с программой заменяют синтезированные.
    // Если файла нет — остаётся синтезированный тон.
    struct File { Alert alert; const char* name; };
    const File files[] = {
        {Alert::PullUp, "pull_up.ogg"},       {Alert::Terrain, "terrain.ogg"},
        {Alert::SinkRate, "sink_rate.ogg"},   {Alert::TooLowGear, "too_low_gear.ogg"},
        {Alert::BankAngle, "bank_angle.ogg"}, {Alert::Overspeed, "overspeed.ogg"},
        {Alert::Stall, "stall.ogg"},
    };
    for (const File& f : files) {
        const char* path = TextFormat("%ssounds/%s", GetApplicationDirectory(), f.name);
        if (!FileExists(path)) continue;
        Sound snd = LoadSound(path);
        if (snd.frameCount == 0) continue;
        UnloadSound(alerts_[(int)f.alert]);
        alerts_[(int)f.alert] = snd;
    }
}

void AudioSystem::Shutdown()
{
    if (!ready_) return;
    UnloadAudioStream(engine_);
    for (Sound& s : alerts_) UnloadSound(s);
    UnloadSound(crash_);
    UnloadSound(touchdown_);
    UnloadSound(click_);
    CloseAudioDevice();
    ready_ = false;
}

void AudioSystem::FillEngine(short* out, int frames, const Aircraft& a, bool paused)
{
    float n1Target = a.crashed ? 0.0f : a.n1;
    float windTarget = a.crashed ? 0.0f : fs::Clampf(a.ias / 140.0f, 0.0f, 1.6f);
    float rollTarget = (a.onGround && !a.crashed) ? fs::Clampf(Vector3Length(a.vel) / 45.0f, 0.0f, 1.0f) : 0.0f;
    float master = paused ? 0.0f : 1.0f;

    for (int i = 0; i < frames; ++i) {
        n1Smooth_ += 0.0005f * (n1Target * master - n1Smooth_);
        windSmooth_ += 0.0005f * (windTarget * master - windSmooth_);
        rollSmooth_ += 0.0008f * (rollTarget * master - rollSmooth_);

        float nf = n1Smooth_ / 100.0f;
        float whineF = 180.0f + 22.0f * n1Smooth_;
        whinePhase_ += 2 * PI * whineF / RATE;
        whine2Phase_ += 2 * PI * whineF * 1.51f / RATE;
        if (whinePhase_ > 2 * PI) whinePhase_ -= 2 * PI;
        if (whine2Phase_ > 2 * PI) whine2Phase_ -= 2 * PI;

        float n = Noise();
        lpRoar_ += (0.03f + 0.12f * nf) * (n - lpRoar_);
        lpWind_ += 0.25f * (n - lpWind_);
        lpWind2_ += 0.02f * (lpWind_ - lpWind2_);
        lpRumble_ += 0.012f * (Noise() - lpRumble_);
        rumblePhase_ += 2 * PI * 31.0f / RATE;

        float whine = (0.035f * sinf(whinePhase_) + 0.015f * sinf(whine2Phase_)) * nf * nf;
        float roar = 0.55f * lpRoar_ * fs::SmoothStep(0.2f, 1.0f, nf) * (0.3f + nf);
        float wind = 0.35f * (lpWind_ - lpWind2_) * windSmooth_ * windSmooth_;
        float rumble = rollSmooth_ * (1.4f * lpRumble_ + 0.04f * sinf(rumblePhase_));
        out[i] = (short)(fs::Clampf(whine + roar + wind + rumble, -1.0f, 1.0f) * 30000.0f);
    }
}

void AudioSystem::Update(const Aircraft& a, const WarningSystem& w, bool paused)
{
    if (!ready_) return;

    for (int guard = 0; guard < 4 && IsAudioStreamProcessed(engine_); ++guard) {
        FillEngine(buf_.data(), STREAM_FRAMES, a, paused);
        UpdateAudioStream(engine_, buf_.data(), STREAM_FRAMES);
    }

    // Голос GPWS — только самое приоритетное сообщение; трещотка и тряска звучат параллельно.
    Alert voice = paused ? Alert::Count : w.TopVoice();
    if (voice != lastVoice_ && lastVoice_ != Alert::Count) StopSound(alerts_[(int)lastVoice_]);
    if (voice != Alert::Count && !IsSoundPlaying(alerts_[(int)voice])) PlaySound(alerts_[(int)voice]);
    lastVoice_ = voice;

    for (Alert s : {Alert::Overspeed, Alert::Stall}) {
        Sound& snd = alerts_[(int)s];
        if (!paused && w.Active(s)) {
            if (!IsSoundPlaying(snd)) PlaySound(snd);
        } else if (IsSoundPlaying(snd)) {
            StopSound(snd);
        }
    }
}

void AudioSystem::PlayCrash()
{
    if (!ready_) return;
    for (Sound& s : alerts_) StopSound(s);
    PlaySound(crash_);
}

void AudioSystem::PlayTouchdown(float strength)
{
    if (!ready_) return;
    SetSoundVolume(touchdown_, fs::Clampf(strength, 0.15f, 1.0f));
    PlaySound(touchdown_);
}

void AudioSystem::PlayClick()
{
    if (ready_) PlaySound(click_);
}
