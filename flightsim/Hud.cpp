#include "Hud.h"
#include "Aircraft.h"
#include "Terrain.h"
#include "Warnings.h"
#include "Ils.h"
#include "Autopilot.h"
#include "UiText.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace fs;

namespace {

float S = 1.0f;   // масштаб интерфейса (высота окна / 900)
const Autopilot* g_ap = nullptr;   // автопилот/директор текущего кадра
float g_baro = 1013.0f;            // установка высотомера текущего кадра
int Fs(float px) { return (int)(px * S + 0.5f); }

const Color kPanel{14, 16, 20, 225};
const Color kTape{40, 44, 52, 235};
const Color kSky{40, 120, 205, 255};
const Color kGround{125, 82, 42, 255};
const Color kAmber{255, 176, 0, 255};
const Color kGreen{60, 230, 90, 255};
const Color kMagenta{240, 80, 230, 255};
const Color kCyan{80, 220, 240, 255};
const Color kRed{240, 40, 40, 255};
const Color kHudGreen{110, 255, 140, 230};

void Text(const char* s, float x, float y, float size, Color c) { ui::Draw(s, x, y, size * S, c); }
void TextC(const char* s, float cx, float y, float size, Color c) { ui::DrawCentered(s, cx, y, size * S, c); }
void TextR(const char* s, float rx, float y, float size, Color c) { ui::DrawRight(s, rx, y, size * S, c); }
void TextOutlined(const char* s, float cx, float y, int size, Color c) { ui::DrawOutlined(s, cx, y, (float)size, c); }

Vector2 Rot(Vector2 v, float deg)
{
    float r = deg * DEG2RAD, c = cosf(r), s = sinf(r);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

bool Blink(float time, float hz = 2.0f) { return fmodf(time * hz, 1.0f) < 0.6f; }

// ---------------------------------------------------------------- PFD

void DrawAttitude(const Aircraft& a, const WarningSystem& w, const Ils& ils, float cx, float cy, float A, float time)
{
    const float pitch = a.PitchDeg(), bank = a.BankDeg();
    const float ppd = (A * 0.5f) / 24.0f;   // пикселей на градус тангажа
    const float half = A * 0.5f;

    BeginScissorMode((int)(cx - half), (int)(cy - half), (int)A, (int)A);
    Vector2 h = Rot({0, pitch * ppd}, -bank);
    Vector2 hc{cx + h.x, cy + h.y};
    DrawRectanglePro({hc.x, hc.y, A * 4, A * 4}, {A * 2, A * 4}, -bank, kSky);
    DrawRectanglePro({hc.x, hc.y, A * 4, A * 4}, {A * 2, 0}, -bank, kGround);
    Vector2 l0 = Rot({-A * 2, 0}, -bank), l1 = Rot({A * 2, 0}, -bank);
    DrawLineEx({hc.x + l0.x, hc.y + l0.y}, {hc.x + l1.x, hc.y + l1.y}, 2.0f * S, WHITE);

    for (float p = -90; p <= 90; p += 2.5f) {
        if (p == 0 || fabsf(p - pitch) > 22.0f) continue;
        bool major = fmodf(fabsf(p), 10.0f) < 0.01f;
        bool mid = !major && fmodf(fabsf(p), 5.0f) < 0.01f;
        float wHalf = (major ? 34.0f : (mid ? 17.0f : 8.0f)) * S;
        float y = (pitch - p) * ppd;
        Vector2 p0 = Rot({-wHalf, y}, -bank), p1 = Rot({wHalf, y}, -bank);
        DrawLineEx({cx + p0.x, cy + p0.y}, {cx + p1.x, cy + p1.y}, 1.5f * S, WHITE);
        if (major) {
            char buf[8];
            snprintf(buf, sizeof buf, "%d", (int)fabsf(p));
            Vector2 lp = Rot({-wHalf - 20 * S, y - 6 * S}, -bank), rp = Rot({wHalf + 6 * S, y - 6 * S}, -bank);
            Text(buf, cx + lp.x, cy + lp.y, 12, WHITE);
            Text(buf, cx + rp.x, cy + rp.y, 12, WHITE);
        }
    }
    EndScissorMode();
    DrawRectangleLinesEx({cx - half, cy - half, A, A}, 1.0f, Color{0, 0, 0, 255});

    // Шкала крена и указатель (указатель вращается вместе с горизонтом).
    const float R = half - 10 * S;
    for (float t : {-60.0f, -45.0f, -30.0f, -20.0f, -10.0f, 0.0f, 10.0f, 20.0f, 30.0f, 45.0f, 60.0f}) {
        float len = (fabsf(t) == 30.0f || fabsf(t) == 60.0f || t == 0.0f) ? 10 * S : 6 * S;
        Vector2 o = Rot({0, -R}, t), i = Rot({0, -R + len}, t);
        DrawLineEx({cx + o.x, cy + o.y}, {cx + i.x, cy + i.y}, 2.0f * S, WHITE);
    }
    {
        Color pc = fabsf(bank) > 35.0f ? kAmber : WHITE;
        Vector2 t0 = Rot({0, -R + 12 * S}, -bank), t1 = Rot({-7 * S, -R + 24 * S}, -bank), t2 = Rot({7 * S, -R + 24 * S}, -bank);
        DrawTriangle({cx + t0.x, cy + t0.y}, {cx + t1.x, cy + t1.y}, {cx + t2.x, cy + t2.y}, pc);
        // «Скольжение»: трапеция под указателем сдвигается по углу скольжения.
        float slip = Clampf(a.beta * RAD2DEG * 1.5f, -12, 12) * S;
        Vector2 s0 = Rot({-8 * S + slip, -R + 27 * S}, -bank), s1 = Rot({8 * S + slip, -R + 27 * S}, -bank);
        DrawLineEx({cx + s0.x, cy + s0.y}, {cx + s1.x, cy + s1.y}, 3.0f * S, pc);
    }

    // Силуэт самолёта (неподвижный).
    const Color sym{255, 220, 0, 255};
    DrawRectangle((int)(cx - 70 * S), (int)(cy - 3 * S), (int)(45 * S), (int)(6 * S), BLACK);
    DrawRectangle((int)(cx - 69 * S), (int)(cy - 2 * S), (int)(43 * S), (int)(4 * S), sym);
    DrawRectangle((int)(cx + 25 * S), (int)(cy - 3 * S), (int)(45 * S), (int)(6 * S), BLACK);
    DrawRectangle((int)(cx + 26 * S), (int)(cy - 2 * S), (int)(43 * S), (int)(4 * S), sym);
    DrawRectangle((int)(cx - 4 * S), (int)(cy - 4 * S), (int)(8 * S), (int)(8 * S), BLACK);
    DrawRectangle((int)(cx - 3 * S), (int)(cy - 3 * S), (int)(6 * S), (int)(6 * S), sym);

    // Планки директора: горизонтальная — куда поставить нос, вертикальная — куда кренить.
    // Совмести жёлтый силуэт с перекрестием — и летишь по выбранному режиму.
    if (g_ap && g_ap->fdOn && !a.crashed) {
        float py = cy - Clampf((g_ap->fdPitch - pitch) * ppd, -half * 0.8f, half * 0.8f);
        float bx = cx + Clampf((g_ap->fdBank - bank) * 2.5f * S, -half * 0.6f, half * 0.6f);
        DrawLineEx({cx - 60 * S, py}, {cx + 60 * S, py}, 4.0f * S, BLACK);
        DrawLineEx({cx - 60 * S, py}, {cx + 60 * S, py}, 2.5f * S, kMagenta);
        DrawLineEx({bx, cy - 60 * S}, {bx, cy + 60 * S}, 4.0f * S, BLACK);
        DrawLineEx({bx, cy - 60 * S}, {bx, cy + 60 * S}, 2.5f * S, kMagenta);
    }

    // ILS: ромбы курса (снизу) и глиссады (справа).
    if (ils.valid) {
        float dot = 22 * S;
        float ly = cy + half - 18 * S;
        for (int d = -2; d <= 2; ++d)
            if (d) DrawCircleLines((int)(cx + d * dot), (int)ly, 3 * S, WHITE);
        float lx = cx - ils.locDots * dot;
        DrawPoly({lx, ly}, 4, 7 * S, 0, kMagenta);
        if (ils.gsValid) {
            float gx = cx + half - 14 * S;
            for (int d = -2; d <= 2; ++d)
                if (d) DrawCircleLines((int)gx, (int)(cy + d * dot), 3 * S, WHITE);
            DrawPoly({gx, cy + ils.gsDots * dot}, 4, 7 * S, 0, kMagenta);
        }
        char buf[48];
        snprintf(buf, sizeof buf, "%s  DME %.1f", ils.name, ils.dmeNm);
        Text(buf, cx - half + 4 * S, cy - half + 4 * S, 12, kMagenta);
    }

    // Радиовысота
    if (w.radioAltFt < 2500.0f && !a.onGround) {
        char buf[16];
        snprintf(buf, sizeof buf, "%d", (int)(w.radioAltFt / 10.0f + 0.5f) * 10);
        Color c = w.radioAltFt < 200 ? kAmber : WHITE;
        DrawRectangle((int)(cx - 30 * S), (int)(cy + half - 46 * S), (int)(60 * S), (int)(20 * S), Color{0, 0, 0, 170});
        TextC(buf, cx, cy + half - 44 * S, 18, c);
    }

    if (w.Active(Alert::PullUp) && Blink(time)) TextOutlined("PULL UP", cx, cy - 50 * S, Fs(26), kRed);
    if (w.Active(Alert::Stall)) TextOutlined("STALL", cx, cy + 30 * S, Fs(22), kRed);
}

void DrawSpeedTape(const Aircraft& a, float x, float cy, float wdt, float hgt, float trendKt)
{
    const float ias = a.ias * MS_TO_KT;
    const float ppk = 3.2f * S;
    const float top = cy - hgt * 0.5f;
    auto Y = [&](float kt) { return cy - (kt - ias) * ppk; };

    DrawRectangle((int)x, (int)top, (int)wdt, (int)hgt, kTape);
    BeginScissorMode((int)x, (int)top, (int)wdt, (int)hgt);

    // Красно-чёрная «пила» ограничения сверху и зона сваливания снизу.
    float lim = a.SpeedLimitKt();
    for (float k = lim; Y(k) > top - 20; k += 4.0f) {
        Color c = ((int)((k - lim) / 4.0f) % 2) ? BLACK : kRed;
        DrawRectangle((int)(x + wdt - 9 * S), (int)Y(k + 4), (int)(8 * S), (int)(4 * ppk + 1), c);
    }
    if (!a.onGround || ias > 40.0f) {
        float vs = a.StallSpeedKt();
        DrawRectangle((int)(x + wdt - 9 * S), (int)Y(vs * 1.15f), (int)(4 * S), (int)((vs * 0.15f) * ppk), kAmber);
        for (float k = vs; Y(k) < top + hgt + 20; k -= 4.0f) {
            Color c = ((int)((vs - k) / 4.0f) % 2) ? BLACK : kRed;
            DrawRectangle((int)(x + wdt - 9 * S), (int)Y(k), (int)(8 * S), (int)(4 * ppk + 1), c);
        }
    }
    for (int k = ((int)(ias - 50) / 5) * 5; k <= ias + 50; k += 5) {
        if (k < 30) continue;
        float y = Y((float)k);
        bool major = k % 10 == 0;
        DrawLineEx({x + wdt - 9 * S - (major ? 12 : 6) * S, y}, {x + wdt - 9 * S, y}, 1.5f * S, WHITE);
        if (k % 20 == 0) {
            char buf[16];
            snprintf(buf, sizeof buf, "%d", k);
            TextR(buf, x + wdt - 24 * S, y - 7 * S, 14, WHITE);
        }
    }
    if (g_ap) {   // заданная скорость
        float ty = Clampf(Y(g_ap->spdTarget), top + 4 * S, top + hgt - 4 * S);
        DrawTriangle({x + wdt - 1 * S, ty}, {x + wdt + 7 * S, ty + 6 * S}, {x + wdt + 7 * S, ty - 6 * S}, kCyan);
    }
    // Тренд скорости через 10 с.
    if (fabsf(trendKt) > 2.0f) {
        float y1 = Y(ias + trendKt);
        DrawLineEx({x + wdt - 4 * S, cy}, {x + wdt - 4 * S, y1}, 2.0f * S, kGreen);
    }
    EndScissorMode();

    char buf[16];
    snprintf(buf, sizeof buf, "%d", (int)(ias + 0.5f));
    DrawRectangle((int)(x + 2 * S), (int)(cy - 13 * S), (int)(wdt - 14 * S), (int)(26 * S), BLACK);
    DrawRectangleLinesEx({x + 2 * S, cy - 13 * S, wdt - 14 * S, 26 * S}, 1.5f, WHITE);
    Color sc = ias > a.SpeedLimitKt() ? kRed : WHITE;
    TextR(buf, x + wdt - 18 * S, cy - 9 * S, 20, sc);
    DrawTriangle({x + wdt - 12 * S, cy - 6 * S}, {x + wdt - 12 * S, cy + 6 * S}, {x + wdt - 4 * S, cy}, WHITE);

    if (g_ap) snprintf(buf, sizeof buf, "SPD %d", (int)g_ap->spdTarget);
    else snprintf(buf, sizeof buf, "IAS kt");
    TextC(buf, x + wdt * 0.5f, top - 16 * S, 13, kCyan);
    if (a.mach > 0.35f) {
        snprintf(buf, sizeof buf, "M .%03d", (int)(a.mach * 1000.0f));
        TextC(buf, x + wdt * 0.5f, top + hgt + 4 * S, 14, WHITE);
    }
}

void DrawAltTape(const Aircraft& a, const Terrain& t, float x, float cy, float wdt, float hgt)
{
    const float baroErr = (g_baro - a.qnh) * 27.3f;   // ошибка высотомера от неверной установки давления
    const float alt = a.pos.y * M_TO_FT + baroErr;
    const float ppf = 0.3f * S;
    const float top = cy - hgt * 0.5f;
    auto Y = [&](float ft) { return cy - (ft - alt) * ppf; };

    DrawRectangle((int)x, (int)top, (int)wdt, (int)hgt, kTape);
    BeginScissorMode((int)x, (int)top, (int)wdt, (int)hgt);
    // Земля под самолётом
    float groundFt = t.SurfaceHeight(a.pos.x, a.pos.z) * M_TO_FT + baroErr;
    float gy = Y(groundFt);
    if (gy < top + hgt) {
        DrawRectangle((int)x, (int)gy, (int)(10 * S), (int)(top + hgt - gy), kAmber);
        for (float yy = gy; yy < top + hgt; yy += 8 * S)
            DrawLineEx({x + 10 * S, yy}, {x + 20 * S, yy + 8 * S}, 1.5f, kAmber);
    }
    if (g_ap) {   // заданная высота
        float ty = Clampf(Y(g_ap->altTarget), top + 6 * S, top + hgt - 6 * S);
        DrawRectangle((int)x, (int)(ty - 6 * S), (int)(7 * S), (int)(12 * S), kCyan);
    }
    for (int k = ((int)(alt - 500) / 100) * 100; k <= alt + 500; k += 100) {
        float y = Y((float)k);
        bool major = k % 500 == 0;
        DrawLineEx({x, y}, {x + (major ? 14 : 8) * S, y}, 1.5f * S, WHITE);
        if (k % 200 == 0) {
            char buf[12];
            snprintf(buf, sizeof buf, "%d", k);
            Text(buf, x + 17 * S, y - 7 * S, 14, WHITE);
        }
    }
    EndScissorMode();

    char buf[16];
    snprintf(buf, sizeof buf, "%d", (int)(alt / 10.0f + (alt >= 0 ? 0.5f : -0.5f)) * 10);
    DrawRectangle((int)(x + 4 * S), (int)(cy - 13 * S), (int)(wdt - 6 * S), (int)(26 * S), BLACK);
    DrawRectangleLinesEx({x + 4 * S, cy - 13 * S, wdt - 6 * S, 26 * S}, 1.5f, WHITE);
    DrawTriangle({x + 4 * S, cy}, {x + 12 * S, cy + 6 * S}, {x + 12 * S, cy - 6 * S}, WHITE);
    TextR(buf, x + wdt - 6 * S, cy - 9 * S, 20, WHITE);
    if (g_ap) TextC(TextFormat("ALT %d", (int)g_ap->altTarget), x + wdt * 0.5f, top - 16 * S, 13, kCyan);
    else TextC("ALT ft", x + wdt * 0.5f, top - 16 * S, 12, kCyan);
    // Установка высотомера: STD или давление в гПа; жёлтая, если ниже 5000 ft она не совпадает с давлением района.
    bool std1013 = fabsf(g_baro - 1013.0f) < 0.5f;
    bool wrong = fabsf(g_baro - a.qnh) > 1.5f && a.pos.y * M_TO_FT < 5000.0f;
    TextC(std1013 ? "STD" : TextFormat("QNH %d", (int)roundf(g_baro)), x + wdt * 0.5f, top + hgt + 4 * S, 13, wrong ? kAmber : kCyan);
}

void DrawVsi(const Aircraft& a, float x, float cy, float wdt, float hgt)
{
    const float fpm = a.vel.y * MS_TO_FPM;
    const float half = hgt * 0.5f;
    auto F = [](float v) {
        float m = fabsf(v), f;
        if (m <= 1000) f = 0.5f * m / 1000.0f;
        else if (m <= 2000) f = 0.5f + 0.25f * (m - 1000.0f) / 1000.0f;
        else f = 0.75f + 0.25f * fminf((m - 2000.0f) / 4000.0f, 1.0f);
        return v < 0 ? -f : f;
    };
    DrawRectangle((int)x, (int)(cy - half), (int)wdt, (int)hgt, kTape);
    for (float v : {-6000.0f, -2000.0f, -1000.0f, -500.0f, 0.0f, 500.0f, 1000.0f, 2000.0f, 6000.0f}) {
        float y = cy - F(v) * (half - 4 * S);
        DrawLineEx({x, y}, {x + (fmodf(fabsf(v), 1000.0f) == 0 ? 8 : 5) * S, y}, 1.5f, WHITE);
    }
    Text("1", x + 10 * S, cy - F(1000) * (half - 4 * S) - 5 * S, 10, WHITE);
    Text("2", x + 10 * S, cy - F(2000) * (half - 4 * S) - 5 * S, 10, WHITE);
    Text("6", x + 10 * S, cy - F(6000) * (half - 4 * S) - 5 * S, 10, WHITE);
    Text("1", x + 10 * S, cy - F(-1000) * (half - 4 * S) - 5 * S, 10, WHITE);
    Text("2", x + 10 * S, cy - F(-2000) * (half - 4 * S) - 5 * S, 10, WHITE);
    Text("6", x + 10 * S, cy - F(-6000) * (half - 4 * S) - 5 * S, 10, WHITE);
    float ny = cy - F(fpm) * (half - 4 * S);
    Color nc = (fpm < -2000.0f) ? kAmber : kGreen;
    DrawLineEx({x + wdt + 6 * S, cy}, {x + 2 * S, ny}, 2.5f * S, nc);
    if (fabsf(fpm) > 400.0f) {
        char buf[12];
        snprintf(buf, sizeof buf, "%d", (int)(fpm / 50.0f) * 50);
        TextC(buf, x + wdt * 0.5f, fpm > 0 ? cy - half - 16 * S : cy + half + 3 * S, 12, nc);
    }
}

void DrawHeadingTape(const Aircraft& a, float cx, float y, float wdt, float hgt)
{
    const float hdg = a.HeadingDeg();
    const float ppd = 4.0f * S;
    DrawRectangle((int)(cx - wdt * 0.5f), (int)y, (int)wdt, (int)hgt, kTape);
    BeginScissorMode((int)(cx - wdt * 0.5f), (int)y, (int)wdt, (int)hgt);
    for (int d = (int)(hdg - 40) / 5 * 5 - 5; d <= hdg + 40; d += 5) {
        float x = cx + WrapDeg180((float)d - hdg) * ppd;
        int dd = (int)WrapDeg360((float)d);
        bool major = dd % 10 == 0;
        DrawLineEx({x, y}, {x, y + (major ? 9 : 5) * S}, 1.5f, WHITE);
        if (major) {
            char buf[16];
            const char* lbl = buf;
            if (dd == 0) lbl = "N";
            else if (dd == 90) lbl = "E";
            else if (dd == 180) lbl = "S";
            else if (dd == 270) lbl = "W";
            else snprintf(buf, sizeof buf, "%02d", dd / 10);
            TextC(lbl, x, y + 10 * S, 12, WHITE);
        }
    }
    if (g_ap) {   // заданный курс
        float bx = cx + Clampf(WrapDeg180(g_ap->hdgBug - hdg) * ppd, -wdt * 0.5f + 5 * S, wdt * 0.5f - 5 * S);
        DrawRectangle((int)(bx - 5 * S), (int)y, (int)(10 * S), (int)(5 * S), kCyan);
        DrawRectangle((int)(bx - 2 * S), (int)y, (int)(4 * S), (int)(9 * S), kCyan);
    }
    if (g_ap && g_ap->navOn) {   // заданный путевой угол маршрута — пурпурная метка
        float nx = cx + Clampf(WrapDeg180(g_ap->navTrack - hdg) * ppd, -wdt * 0.5f + 5 * S, wdt * 0.5f - 5 * S);
        DrawTriangle({nx, y + 10 * S}, {nx + 5 * S, y + 2 * S}, {nx - 5 * S, y + 2 * S}, Color{230, 80, 230, 255});
    }
    // Путевой угол (куда реально движемся) — зелёный ромб.
    if (sqrtf(a.vel.x * a.vel.x + a.vel.z * a.vel.z) > 5.0f) {
        float trk = WrapDeg360(atan2f(a.vel.x, -a.vel.z) * RAD2DEG);
        float tx = cx + WrapDeg180(trk - hdg) * ppd;
        DrawPoly({tx, y + 4 * S}, 4, 4 * S, 0, kGreen);
    }
    EndScissorMode();
    char buf[8];
    snprintf(buf, sizeof buf, "%03d", (int)(hdg + 0.5f) % 360);
    DrawRectangle((int)(cx - 22 * S), (int)(y - 20 * S), (int)(44 * S), (int)(19 * S), BLACK);
    DrawRectangleLinesEx({cx - 22 * S, y - 20 * S, 44 * S, 19 * S}, 1.0f, WHITE);
    TextC(buf, cx, y - 18 * S, 16, WHITE);
}

// ---------------------------------------------------------------- двигатель и конфигурация

// Стрелочный указатель оборотов одного двигателя.
void DrawEngineGauge(const Aircraft& a, const Controls& c, int i, float cx, float cy, float r)
{
    const AircraftType& t = a.Type();
    const Engine& e = a.engines[i];
    auto ang = [](float pct) { return 135.0f + 270.0f * Clampf(pct, 0, 110) / 110.0f; };
    Color ring = e.fire ? kRed : Color{70, 74, 82, 255};
    DrawRing({cx, cy}, r - 4 * S, r, ang(0), ang(110), 40, ring);
    DrawRing({cx, cy}, r - 4 * S, r, ang(100), ang(110), 8, kRed);
    DrawCircleSector({cx, cy}, r - 5 * S, ang(0), ang(e.n1), 40, Color{230, 230, 230, 60});
    for (int p = 0; p <= 100; p += 20) {
        float tt = ang((float)p) * DEG2RAD;
        DrawLineEx({cx + cosf(tt) * (r - 10 * S), cy + sinf(tt) * (r - 10 * S)}, {cx + cosf(tt) * r, cy + sinf(tt) * r}, 1.5f, WHITE);
    }
    float tt = ang(e.n1) * DEG2RAD;
    DrawLineEx({cx, cy}, {cx + cosf(tt) * (r - 3 * S), cy + sinf(tt) * (r - 3 * S)}, 3.0f * S, WHITE);
    float tc = ang(t.idleN1 + (100.0f - t.idleN1) * c.throttle) * DEG2RAD;   // заданный РУДом режим
    DrawCircle((int)(cx + cosf(tc) * (r + 5 * S)), (int)(cy + sinf(tc) * (r + 5 * S)), 3.5f * S, kCyan);
    char buf[16];
    snprintf(buf, sizeof buf, "%.1f", e.n1);
    DrawRectangle((int)(cx + 2 * S), (int)(cy + 3 * S), (int)(46 * S), (int)(18 * S), BLACK);
    TextR(buf, cx + 45 * S, cy + 4 * S, 14, WHITE);
    const char* lbl = t.jet ? (t.engineCount > 1 ? TextFormat("N1 %d", i + 1) : "N1") : "RPM %";
    TextC(lbl, cx - 2 * S, cy - r * 0.5f, 11, kCyan);
    if (e.fire) TextC("FIRE", cx, cy + r * 0.55f, 13, kRed);
    else if (!e.Running() || a.fuel <= 0) TextC(e.shutdown ? "OFF" : "FAIL", cx, cy + r * 0.55f, 13, kAmber);
    else if (a.reverser > 0.5f) TextC("REV", cx, cy + r * 0.55f, 13, kGreen);
}

void DrawVBar(float x, float y, float w, float h, float frac, Color c, const char* label, const char* topLbl, const char* botLbl)
{
    DrawRectangle((int)x, (int)y, (int)w, (int)h, kTape);
    DrawRectangleLinesEx({x, y, w, h}, 1.0f, Color{120, 124, 132, 255});
    float my = y + h * (1.0f - Clampf(frac, 0, 1));
    DrawRectangle((int)(x - 3 * S), (int)(my - 3 * S), (int)(w + 6 * S), (int)(6 * S), c);
    TextC(label, x + w * 0.5f, y - 16 * S, 12, kCyan);
    if (topLbl) Text(topLbl, x + w + 4 * S, y - 2 * S, 10, Color{170, 170, 170, 255});
    if (botLbl) Text(botLbl, x + w + 4 * S, y + h - 11 * S, 10, Color{170, 170, 170, 255});
}

void Annunciator(const char* s, float x, float y, float w, bool on, Color c)
{
    DrawRectangle((int)x, (int)y, (int)w, (int)(18 * S), on ? Color{(unsigned char)(c.r / 4), (unsigned char)(c.g / 4), (unsigned char)(c.b / 4), 255} : Color{30, 32, 36, 255});
    DrawRectangleLinesEx({x, y, w, 18 * S}, 1.0f, on ? c : Color{60, 62, 68, 255});
    TextC(s, x + w * 0.5f, y + 3 * S, 11, on ? c : Color{80, 82, 88, 255});
}

void DrawSystems(const Aircraft& a, const Controls& c, float x, float y)
{
    const AircraftType& t = a.Type();
    if (t.engineCount > 1) {
        DrawEngineGauge(a, c, 0, x + 46 * S, y + 60 * S, 38 * S);
        DrawEngineGauge(a, c, 1, x + 134 * S, y + 60 * S, 38 * S);
    } else {
        DrawEngineGauge(a, c, 0, x + 90 * S, y + 62 * S, 46 * S);
    }
    // Топливо: остаток и сколько ещё можно лететь при текущем расходе.
    {
        char buf[64];
        float mins = a.fuelFlow > 1e-4f ? a.fuel / a.fuelFlow / 60.0f : 0.0f;
        Color fc = a.fuel < t.maxFuel * 0.08f ? kAmber : WHITE;
        if (a.fuel <= 0) fc = kRed;
        snprintf(buf, sizeof buf, "FUEL %.0f kg", a.fuel);
        Text(buf, x + 8 * S, y + 108 * S, 13, fc);
        if (mins > 0) {
            snprintf(buf, sizeof buf, "%d:%02d  %.0f kg/h", (int)mins / 60, (int)mins % 60, a.fuelFlow * 3600.0f);
            Text(buf, x + 8 * S, y + 124 * S, 11, Color{170, 170, 170, 255});
        }
    }

    // РУД
    DrawVBar(x + 190 * S, y + 25 * S, 14 * S, 100 * S, c.throttle, WHITE, "THR", "MAX", "IDLE");

    // Закрылки: рычаг (голубой треугольник) и фактическое положение (белая метка)
    {
        float bx = x + 248 * S, by = y + 25 * S, bh = 100 * S;
        const float full = t.flapDeg[3];
        DrawRectangle((int)bx, (int)by, (int)(14 * S), (int)bh, kTape);
        for (int i = 0; i < 4; ++i) {
            float yy = by + bh * t.flapDeg[i] / full;
            DrawLineEx({bx + 14 * S, yy}, {bx + 20 * S, yy}, 1.0f, WHITE);
            Text(t.flapNames[i], bx + 22 * S, yy - 6 * S, 10, Color{170, 170, 170, 255});
        }
        float fy = by + bh * a.flaps / full;
        DrawRectangle((int)(bx - 3 * S), (int)(fy - 3 * S), (int)(20 * S), (int)(6 * S), a.Failed(Failure::FlapsJam) ? kAmber : WHITE);
        float ly = by + bh * t.flapDeg[c.flapsLever] / full;
        DrawTriangle({bx - 4 * S, ly}, {bx - 11 * S, ly - 5 * S}, {bx - 11 * S, ly + 5 * S}, kCyan);
        TextC("FLAPS", bx + 7 * S, by - 16 * S, 12, kCyan);
    }

    // Триммер руля высоты
    DrawVBar(x + 308 * S, y + 25 * S, 12 * S, 100 * S, 0.5f + 0.5f * c.trim, kGreen, "TRIM", "NU", "ND");

    // Шасси: три лампы
    {
        float gx = x + 355 * S, gy = y + 25 * S;
        TextC("GEAR", gx + 32 * S, gy - 16 * S, 12, kCyan);
        if (!t.retractableGear) {
            TextC("FIXED", gx + 32 * S, gy + 14 * S, 12, kGreen);
        } else {
            auto lamp = [&](float lx, float ly) {
                bool down = a.gear >= 0.999f, up = a.gear <= 0.001f;
                bool disagree = (c.gearDown && !down) || (!c.gearDown && !up);
                Color col = down ? kGreen : (disagree ? kRed : Color{60, 62, 68, 255});
                const char* txt = down ? "DN" : (up ? "UP" : "");
                DrawRectangle((int)lx, (int)ly, (int)(26 * S), (int)(20 * S), Color{(unsigned char)(col.r / 4), (unsigned char)(col.g / 4), (unsigned char)(col.b / 4), 255});
                DrawRectangleLinesEx({lx, ly, 26 * S, 20 * S}, 1.5f, col);
                TextC(txt, lx + 13 * S, ly + 4 * S, 11, col);
            };
            lamp(gx + 19 * S, gy);
            lamp(gx, gy + 26 * S);
            lamp(gx + 38 * S, gy + 26 * S);
            Text(c.gearDown ? "LEVER DN" : "LEVER UP", gx + 2 * S, gy + 52 * S, 11, Color{170, 170, 170, 255});
        }
    }

    // Табло
    float ax = x + 345 * S, ay = y + 100 * S;
    Annunciator("SPD BRK", ax, ay, 84 * S, a.speedbrake > 0.02f, kAmber);
    Annunciator("PARK BRK", ax, ay + 22 * S, 84 * S, c.parkingBrake, kAmber);
    Annunciator(a.reverser > 0.05f ? "REVERSE" : "BRAKES", ax, ay + 44 * S, 84 * S,
                a.reverser > 0.05f || (c.brakes > 0.05f && !c.parkingBrake), kGreen);
    Annunciator("ANTI ICE", ax, ay + 66 * S, 84 * S, c.antiIce, kGreen);

    // Положение штурвала и педалей
    float sx = x + 20 * S, sy = y + 150 * S, ss = 80 * S;
    DrawRectangle((int)sx, (int)sy, (int)ss, (int)ss, kTape);
    DrawLineEx({sx + ss / 2, sy}, {sx + ss / 2, sy + ss}, 1.0f, Color{90, 94, 102, 255});
    DrawLineEx({sx, sy + ss / 2}, {sx + ss, sy + ss / 2}, 1.0f, Color{90, 94, 102, 255});
    DrawCircle((int)(sx + ss / 2 + c.roll * ss / 2), (int)(sy + ss / 2 + c.pitch * ss / 2), 5 * S, kCyan);
    DrawCircleLines((int)(sx + ss / 2 + a.aileron / (t.maxAilDeg * DEG2RAD) * ss / 2),
                    (int)(sy + ss / 2 + a.elevator / (t.maxElevDeg * DEG2RAD) * ss / 2), 8 * S, WHITE);
    Text("STICK", sx, sy - 15 * S, 11, kCyan);
    DrawRectangle((int)sx, (int)(sy + ss + 8 * S), (int)ss, (int)(10 * S), kTape);
    DrawRectangle((int)(sx + ss / 2 + c.yaw * ss / 2 - 3 * S), (int)(sy + ss + 6 * S), (int)(6 * S), (int)(14 * S), kCyan);
    Text("RUDDER", sx, sy + ss + 21 * S, 10, Color{170, 170, 170, 255});
}

void DrawData(const Aircraft& a, const WarningSystem& w, float x, float y)
{
    char buf[64];
    float gs = sqrtf(a.vel.x * a.vel.x + a.vel.z * a.vel.z) * MS_TO_KT;
    float aoa = a.alpha * RAD2DEG;
    Color aoaC = aoa > a.stallAlpha * RAD2DEG - 2.0f ? kRed : (aoa > a.stallAlpha * RAD2DEG - 5.0f ? kAmber : WHITE);
    snprintf(buf, sizeof buf, "AOA  %5.1f", aoa);
    Text(buf, x, y, 13, aoaC);
    snprintf(buf, sizeof buf, "G    %5.2f", a.gLoad);
    Text(buf, x, y + 18 * S, 13, (a.gLoad > 3.5f || a.gLoad < -1.0f) ? kAmber : WHITE);
    snprintf(buf, sizeof buf, "TAS  %5.0f", a.tas * MS_TO_KT);
    Text(buf, x, y + 36 * S, 13, WHITE);
    snprintf(buf, sizeof buf, "GS   %5.0f", gs);
    Text(buf, x, y + 54 * S, 13, WHITE);
    snprintf(buf, sizeof buf, "VS   %5.0f", a.vel.y * MS_TO_FPM);
    Text(buf, x, y + 72 * S, 13, WHITE);
    snprintf(buf, sizeof buf, "OAT  %+4.0f°C", a.oat);
    Text(buf, x, y + 90 * S, 13, a.Icing() ? kCyan : WHITE);
    // Температура тормозов: белая — норма, жёлтая — горячие, красная — сейчас спустят шины.
    float bt = fmaxf(a.brakeTemp[0], a.brakeTemp[1]);
    snprintf(buf, sizeof buf, "BRK %3.0f/%3.0f", a.brakeTemp[0], a.brakeTemp[1]);
    Text(buf, x, y + 108 * S, 13, bt > 500.0f ? kRed : (bt > Aircraft::BRAKES_HOT ? kAmber : WHITE));
    if (w.timeToImpact > 0) {
        snprintf(buf, sizeof buf, "TERR %4.0fs", w.timeToImpact);
        Text(buf, x + 100 * S, y + 90 * S, 13, kAmber);
    }
}

// Сообщения об отказах с подсказкой, что делать (как экран ECAM).
void DrawEcam(const Aircraft& a, float x, float y, float w)
{
    using ui::L;
    const AircraftType& t = a.Type();
    struct Msg { std::string title, action; Color c; };
    std::vector<Msg> msgs;
    if (a.lost[(int)Part::WingL] || a.lost[(int)Part::WingR])
        msgs.push_back({a.lost[(int)Part::WingL] && a.lost[(int)Part::WingR] ? "WINGS LOST"
                        : a.lost[(int)Part::WingL] ? "L WING LOST" : "R WING LOST",
                        L("Aircraft uncontrollable", "Самолёт неуправляем"), kRed});
    if (a.lost[(int)Part::Tail])
        msgs.push_back({"STABILIZER LOST", L("No pitch control", "Нет управления по тангажу"), kRed});
    if (a.overstressed && !a.Broken())
        msgs.push_back({"OVERSTRESS", TextFormat(L("%.1f G (limit %.1f / %.1f G): structure damaged, land", "%.1f G (предел %.1f / %.1f G): конструкция повреждена, садитесь"),
                                                 a.peakG > -a.minG * a.LimitG() / -a.LimitNegG() ? a.peakG : a.minG, a.LimitG(), a.LimitNegG()),
                        kAmber});
    for (int i = 0; i < t.engineCount; ++i) {
        const Engine& e = a.engines[i];
        std::string eng = t.engineCount > 1 ? TextFormat("ENG %d", i + 1) : "ENGINE";
        if (e.fire)
            msgs.push_back({eng + " FIRE", L("J - fire handle: shut down + extinguish", "J — пожарный кран: выключить и потушить"), kRed});
        else if (e.shutdown)
            msgs.push_back({eng + " SHUT DOWN", L("Fly on the other engine, land ASAP", "Летите на втором двигателе, садитесь"), kAmber});
        else if (e.failed)
            msgs.push_back({eng + " FAIL", t.engineCount > 1 ? L("Hold heading with rudder (Q/E)", "Держите курс педалями (Q/E)")
                                                            : L("Glide: ~1.3 x stall speed, pick a field", "Планируйте на ~1.3 скорости сваливания"), kAmber});
    }
    if (a.fuel <= 0)
        msgs.push_back({"FUEL EMPTY", L("Engines out - glide to a runway", "Двигатели встали — планируйте к полосе"), kRed});
    else if (a.fuel < t.maxFuel * 0.08f)
        msgs.push_back({"FUEL LOW", L("Land at the nearest airport", "Садитесь на ближайший аэродром"), kAmber});
    if (a.Failed(Failure::FuelLeak) && a.fuel > 0)
        msgs.push_back({"FUEL LEAK", L("Fuel is draining fast", "Топливо быстро уходит"), kAmber});
    if (a.Failed(Failure::GearHydraulics) && a.gear < 0.999f)
        msgs.push_back({"GEAR UNSAFE", L("H - alternate gear extension (gravity)", "H — аварийный выпуск шасси (под весом)"), kAmber});
    if (a.Failed(Failure::FlapsJam))
        msgs.push_back({"FLAPS JAMMED", L("Fly the approach ~20 kt faster", "Заход на скорости на ~20 kt выше"), kAmber});
    if (a.ice > 0.05f) {
        char ib[32];
        snprintf(ib, sizeof ib, "ICE %d%%", (int)(a.ice * 100.0f + 0.5f));
        if (t.kind == AircraftKind::Prop)
            msgs.push_back({ib, L("No wing de-ice: leave the cloud, descend to warmer air, +10 kt",
                                  "Обогрева крыла нет: выйдите из облака, снизьтесь в тепло, +10 kt"), kAmber});
        else if (!a.lastAntiIce)
            msgs.push_back({ib, L("I - anti-ice ON; stall speed is higher", "I — включить обогрев; скорость сваливания выше"), kAmber});
        else
            msgs.push_back({ib, L("Anti-ice ON - ice is shedding", "Обогрев включён — лёд сходит"), kGreen});
    } else if (a.Icing() && !a.lastAntiIce && t.kind != AircraftKind::Prop) {
        msgs.push_back({"ICING CONDITIONS", L("Cloud below +2 C: I - anti-ice ON", "Облако и ниже +2 °C: I — включить обогрев"), kAmber});
    }
    for (int i = 0; i < 2; ++i)
        if (a.tireFlat[i])
            msgs.push_back({i == 0 ? "L MAIN TIRE FLAT" : "R MAIN TIRE FLAT",
                            L("Fuse plug released: hold straight with rudder", "Сработала плавкая пробка: держите педалями"), kAmber});
    if (fmaxf(a.brakeTemp[0], a.brakeTemp[1]) > Aircraft::BRAKES_HOT)
        msgs.push_back({"BRAKES HOT", L("Let them cool; over 550 C the tires deflate", "Дайте остыть; выше 550 °C спускают шины"), kAmber});
    if (fabsf(g_baro - a.qnh) > 1.5f && a.pos.y * M_TO_FT < 5000.0f && !a.onGround) {
        char bb[40];
        snprintf(bb, sizeof bb, "BARO %+d FT", (int)roundf((g_baro - a.qnh) * 27.3f));
        msgs.push_back({bb, TextFormat(L("Set QNH %d: F5/F6, F7 - STD/QNH", "Выставьте QNH %d: F5/F6, F7 — STD/QNH"), (int)roundf(a.qnh)), kAmber});
    }
    if (a.tailStrike) msgs.push_back({"TAIL STRIKE", L("Lower pitch on rotation/flare", "Меньше тангаж при отрыве и выравнивании"), kAmber});
    for (const Msg& m : msgs) {
        Text(m.title.c_str(), x, y, 15, m.c);
        Text(m.action.c_str(), x + 6 * S, y + 18 * S, 11, Color{200, 204, 210, 255});
        y += 40 * S;
    }
    (void)w;
}

// ---------------------------------------------------------------- HUD в кабине (проекция на стекло)

void DrawCockpitHud(const Aircraft& a, const Camera3D& cam)
{
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    Vector3 eye = cam.position;
    Vector3 fwdCam = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    float hdg = a.HeadingDeg() * DEG2RAD;
    Vector3 flatF{sinf(hdg), 0, -cosf(hdg)};
    Vector3 flatR{cosf(hdg), 0, sinf(hdg)};

    auto proj = [&](Vector3 dir, Vector2* out) {
        if (Vector3DotProduct(dir, fwdCam) < 0.25f) return false;
        *out = GetWorldToScreen(Vector3Add(eye, Vector3Scale(dir, 1000.0f)), cam);
        return out->x > -sw && out->x < 2 * sw && out->y > -sh && out->y < 2 * sh;
    };

    float pitch = a.PitchDeg();
    for (int p = -30; p <= 30; p += 5) {
        if (fabsf(p - pitch) > 18.0f) continue;
        float pr = p * DEG2RAD;
        Vector3 d = Vector3Add(Vector3Scale(flatF, cosf(pr)), {0, sinf(pr), 0});
        float spanDeg = p == 0 ? 25.0f : 6.0f, gapDeg = p == 0 ? 3.0f : 2.0f;
        Vector2 a0, a1, b0, b1;
        auto side = [&](float deg) {
            float t = tanf(deg * DEG2RAD);
            return Vector3Normalize(Vector3Add(d, Vector3Scale(flatR, t)));
        };
        if (proj(side(-spanDeg), &a0) && proj(side(-gapDeg), &a1) && proj(side(gapDeg), &b0) && proj(side(spanDeg), &b1)) {
            DrawLineEx(a0, a1, 2.0f, kHudGreen);
            DrawLineEx(b0, b1, 2.0f, kHudGreen);
            if (p != 0) {
                char buf[8];
                snprintf(buf, sizeof buf, "%d", p);
                ui::Draw(buf, b1.x + 6, b1.y - 9, 16, kHudGreen);
                ui::Draw(buf, a0.x - 28, a0.y - 9, 16, kHudGreen);
            }
        }
    }
    // Вектор скорости (куда реально летит самолёт)
    if (Vector3Length(a.vel) > 15.0f) {
        Vector2 fpv;
        if (proj(Vector3Normalize(a.vel), &fpv)) {
            DrawCircleLines((int)fpv.x, (int)fpv.y, 9, kHudGreen);
            DrawLineEx({fpv.x - 24, fpv.y}, {fpv.x - 9, fpv.y}, 2.0f, kHudGreen);
            DrawLineEx({fpv.x + 9, fpv.y}, {fpv.x + 24, fpv.y}, 2.0f, kHudGreen);
            DrawLineEx({fpv.x, fpv.y - 9}, {fpv.x, fpv.y - 18}, 2.0f, kHudGreen);
        }
    }
    // Продольная ось самолёта
    Vector2 bore;
    if (proj(a.Forward(), &bore)) {
        DrawLineEx({bore.x - 18, bore.y}, {bore.x - 8, bore.y + 8}, 2.0f, kHudGreen);
        DrawLineEx({bore.x - 8, bore.y + 8}, {bore.x, bore.y}, 2.0f, kHudGreen);
        DrawLineEx({bore.x, bore.y}, {bore.x + 8, bore.y + 8}, 2.0f, kHudGreen);
        DrawLineEx({bore.x + 8, bore.y + 8}, {bore.x + 18, bore.y}, 2.0f, kHudGreen);
    }
}

void DrawCockpitFrame(float panelTop)
{
    const int sw = GetScreenWidth();
    const Color frame{22, 24, 28, 255};
    DrawRectangle(0, 0, sw, (int)(14 * S), frame);
    DrawTriangle({0, 0}, {0, panelTop}, {sw * 0.035f, 0}, frame);
    DrawTriangle({(float)sw, 0}, {sw * 0.965f, 0}, {(float)sw, panelTop}, frame);
    // Центральная стойка остекления
    DrawRectangle((int)(sw * 0.5f - 5 * S), 0, (int)(10 * S), (int)(panelTop * 0.32f), frame);
    DrawRectangle(0, (int)(panelTop - 22 * S), sw, (int)(22 * S), Color{30, 32, 36, 255});   // козырёк приборной доски
}

// ---------------------------------------------------------------- предупреждения и экраны

void DrawAlerts(const WarningSystem& w, float time)
{
    const int sw = GetScreenWidth();
    bool warn = w.AnyWarning(), caut = w.AnyCaution();
    float bx = sw * 0.5f - 170 * S, by = 22 * S;
    bool on = Blink(time, 2.5f);
    DrawRectangle((int)bx, (int)by, (int)(160 * S), (int)(34 * S), warn && on ? Color{200, 20, 20, 255} : Color{50, 20, 20, 200});
    TextC("MASTER WARNING", bx + 80 * S, by + 10 * S, 12, warn ? WHITE : Color{120, 70, 70, 255});
    DrawRectangle((int)(bx + 180 * S), (int)by, (int)(160 * S), (int)(34 * S), caut && on ? Color{220, 150, 0, 255} : Color{50, 40, 10, 200});
    TextC("MASTER CAUTION", bx + 260 * S, by + 10 * S, 12, caut ? BLACK : Color{120, 100, 50, 255});

    // Активные сигналы по приоритету
    const Alert order[] = {Alert::Structure, Alert::PullUp, Alert::EngFire, Alert::Overspeed, Alert::Stall, Alert::Terrain, Alert::SinkRate,
                           Alert::TooLowGear, Alert::BankAngle, Alert::EngFail, Alert::GearUnsafe, Alert::FuelLow, Alert::FlapsJam,
                           Alert::Overstress, Alert::Ice, Alert::BrakesHot};
    float y = 92 * S;
    bool first = true;
    for (Alert a : order) {
        if (!w.Active(a)) continue;
        if (a == Alert::Terrain && w.Active(Alert::PullUp)) continue;
        bool red = WarningSystem::IsWarning(a);
        Color c = red ? kRed : kAmber;
        int size = Fs(first ? 46 : 26);
        if (!first || on || !red) TextOutlined(WarningSystem::Text(a), sw * 0.5f, y, size, c);
        y += size + 8 * S;
        first = false;
    }
}

void Overlay(const char* title, Color tc, const std::vector<std::string>& lines)
{
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, Color{0, 0, 0, 150});
    float w = 900 * S, h = (100 + lines.size() * 24) * S;
    float x = (sw - w) * 0.5f, y = (sh - h) * 0.42f;
    DrawRectangle((int)x, (int)y, (int)w, (int)h, Color{18, 20, 26, 240});
    DrawRectangleLinesEx({x, y, w, h}, 2.0f, tc);
    TextC(title, sw * 0.5f, y + 18 * S, 34, tc);
    for (size_t i = 0; i < lines.size(); ++i) Text(lines[i].c_str(), x + 30 * S, y + (74 + i * 24) * S, 16, Color{220, 224, 230, 255});
}

// Строка режимов автопилота (FMA): зелёным — активные режимы, голубым — взведённые.
void DrawFma(float x, float y)
{
    if (!g_ap) return;
    const Autopilot& ap = *g_ap;
    const float h = 22 * S;
    float cx = x;
    auto box = [&](const char* main, Color mc, const char* armed, float w) {
        DrawRectangle((int)cx, (int)y, (int)w, (int)h, Color{0, 0, 0, 190});
        DrawRectangleLinesEx({cx, y, w, h}, 1.0f, Color{70, 74, 82, 255});
        Text(main, cx + 6 * S, y + 4 * S, 13, mc);
        if (armed) TextR(armed, cx + w - 6 * S, y + 4 * S, 12, kCyan);
        cx += w + 3 * S;
    };
    const char* thr = ap.athrOn ? (ap.retard ? "RETARD" : (ap.vert == Autopilot::Vert::GoAround || ap.vert == Autopilot::Vert::TakeOff ? "TOGA" : "SPEED")) : "MAN THR";
    box(TextFormat("A/THR %s", thr), ap.athrOn ? kGreen : Color{150, 150, 150, 255}, nullptr, 150 * S);
    const char* latArmed = (ap.appArmed && !ap.locCaptured) ? "LOC" : nullptr;
    box(ap.lat == Autopilot::Lat::Loc ? "LOC" : ap.lat == Autopilot::Lat::Nav ? "NAV" : TextFormat("HDG %03d", (int)ap.hdgBug % 360), kGreen,
        latArmed, 110 * S);
    const char* vertArmed = ((ap.appArmed || ap.locCaptured) && !ap.gsCaptured) ? "G/S" : nullptr;
    const char* vert = "";
    switch (ap.vert) {
    case Autopilot::Vert::Gs: vert = "G/S"; break;
    case Autopilot::Vert::TakeOff: vert = "TO"; break;
    case Autopilot::Vert::GoAround: vert = "GA"; break;
    case Autopilot::Vert::Vs: vert = TextFormat("V/S %+d", (int)ap.vsTarget); break;
    default: vert = TextFormat("ALT %d", (int)ap.altTarget); break;
    }
    box(vert, kGreen, vertArmed, 130 * S);
    box(ap.apOn ? "AP" : "AP OFF", ap.apOn ? kGreen : Color{150, 150, 150, 255}, ap.fdOn ? "FD" : nullptr, 90 * S);
}

void DrawApAndCallouts(const CalloutSystem* callouts, float time)
{
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    if (g_ap && g_ap->apOffTimer > 0.0f && Blink(time, 3.0f)) {
        const char* msg = g_ap->apOffReason[0] ? g_ap->apOffReason : ui::L("AP OFF", "АВТОПИЛОТ ОТКЛЮЧЁН");
        TextOutlined(msg, sw * 0.5f, 62 * S, Fs(20), kRed);
    }
    if (callouts && callouts->showTimer > 0.0f && callouts->shown != Callout::Count) {
        Color c = callouts->shown == Callout::Retard ? kAmber : WHITE;
        unsigned char alpha = (unsigned char)(255 * Clampf(callouts->showTimer / 0.4f, 0, 1));
        c.a = alpha;
        TextOutlined(CalloutSystem::Text(callouts->shown), sw * 0.5f, sh * 0.40f, Fs(38), c);
    }
}

void DrawMissionPanel(const HudInfo& info)
{
    if (!info.missionTitle) return;
    float x = 14 * S, y = 14 * S, w = 430 * S;
    float h = (34 + 20 * info.missionLines.size()) * S;
    DrawRectangle((int)x, (int)y, (int)w, (int)h, Color{0, 0, 0, 150});
    DrawRectangleLinesEx({x, y, w, h}, 1.0f, Color{90, 160, 220, 200});
    Text(info.missionTitle, x + 10 * S, y + 7 * S, 16, Color{140, 200, 255, 255});
    for (size_t i = 0; i < info.missionLines.size(); ++i)
        Text(info.missionLines[i].c_str(), x + 10 * S, y + (30 + 20 * i) * S, 13, WHITE);
}

std::vector<std::string> HelpLines(const Aircraft& a)
{
    using ui::L;
    const AircraftType& t = a.Type();
    std::vector<std::string> v = {
        L("Stick: W/S or Up/Down - push/pull,  A/D or Left/Right - roll", "Штурвал: W/S или ↑/↓ — от себя/на себя,  A/D или ←/→ — крен"),
        L("Rudder & nose wheel: Q / E        Mouse yoke: M (mouse = stick)", "Педали и носовое колесо: Q / E      Штурвал-мышь: M"),
        L("Throttle: Shift / Ctrl (hold), Z - idle, X - full / TO-GA", "РУД: Shift / Ctrl (держать), Z — малый газ, X — взлётный / уход TO/GA"),
        L("Reverse: hold Ctrl at idle after touchdown", "Реверс: держать Ctrl на малом газе после касания"),
        L("Trim: [ / ] or Home / End      Flaps: F extend, V retract", "Триммер: [ / ] или Home / End      Закрылки: F выпустить, V убрать"),
        L("Gear: G   Alternate gear: H   Brakes: Space/B   Parking brake: P", "Шасси: G   Аварийный выпуск: H   Тормоза: Space/B   Стояночный: P"),
        L("Speed brake: / or K   Fire handle: J   Camera: C (mouse to look)", "Интерцепторы: / или K   Пожарный кран: J   Камера: C (мышь — обзор)"),
        L("Anti-ice: I (in cloud below +2 C)   Watch brake temperature: BRK", "Обогрев от обледенения: I (в облаке ниже +2 °C)   Температура тормозов: BRK"),
        L("Map and route: N   NAV (fly the route): F2   Altimeter: F5/F6, F7 STD/QNH", "Карта и маршрут: N   NAV (по маршруту): F2   Высотомер: F5/F6, F7 STD/QNH"),
        L("Autopilot: T - AP, Y - auto throttle, L - ILS, U - V/S, O - FD", "Автопилот: T — AP, Y — автомат тяги, L — ILS, U — V/S, O — директор"),
        L("Targets: 9/0 heading, -/= altitude, ,/. speed, ;/' vertical speed", "Задатчики: 9/0 курс, -/= высота, ,/. скорость, ;/' вертикальная"),
        L("Wind: F8   Pause/menu: Esc   Restart: R", "Ветер: F8   Пауза/меню: Esc   Заново: R"),
        "",
    };
    v.push_back(TextFormat(L("%s: rotate ~%d kt, approach ~%d kt with full flaps, Vmo %d kt",
                             "%s: отрыв ~%d kt, заход ~%d kt с полными закрылками, Vmo %d kt"),
                           ui::ru ? t.nameRu : t.nameEn, (int)t.rotateKt, (int)t.approachKt, (int)t.vmo));
    return v;
}

} // namespace

void DrawVisionEffects(float grey, float red, float frost)
{
    static Texture2D vignette{};
    if (vignette.id == 0) {
        // Прозрачный центр, к краям — непрозрачно (белым, чтобы красить любым цветом).
        const int n = 256;
        Image img = GenImageColor(n, n, BLANK);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                float dx = (x + 0.5f) / n * 2.0f - 1.0f, dy = (y + 0.5f) / n * 2.0f - 1.0f;
                float r = sqrtf(dx * dx + dy * dy);
                float a = SmoothStep(0.35f, 1.0f, r);
                ImageDrawPixel(&img, x, y, Color{255, 255, 255, (unsigned char)(a * 255.0f)});
            }
        vignette = LoadTextureFromImage(img);
        SetTextureFilter(vignette, TEXTURE_FILTER_BILINEAR);
        UnloadImage(img);
    }
    const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    // Пятно-«туннель» заданного размера; за его пределами — сплошной цвет.
    auto tunnel = [&](float sizeK, Color c) {
        float d = fmaxf(sw, sh) * sizeK;
        float x0 = sw * 0.5f - d * 0.5f, y0 = sh * 0.5f - d * 0.5f;
        DrawTexturePro(vignette, {0, 0, (float)vignette.width, (float)vignette.height}, {x0, y0, d, d}, {0, 0}, 0.0f, c);
        if (x0 > 0) {
            DrawRectangle(0, 0, (int)ceilf(x0), (int)sh, c);
            DrawRectangle((int)(x0 + d), 0, (int)ceilf(x0) + 1, (int)sh, c);
        }
        if (y0 > 0) {
            DrawRectangle(0, 0, (int)sw, (int)ceilf(y0), c);
            DrawRectangle(0, (int)(y0 + d), (int)sw, (int)ceilf(y0) + 1, c);
        }
    };
    if (frost > 0.01f) tunnel(1.6f, Color{232, 240, 250, (unsigned char)(Clampf(frost, 0, 1) * 190.0f)});
    if (grey > 0.01f) {
        DrawRectangle(0, 0, (int)sw, (int)sh, Color{100, 100, 100, (unsigned char)(Clampf(grey, 0, 1) * 150.0f)});   // краски тускнеют
        tunnel(::Lerp(1.9f, 0.3f, Clampf(grey, 0, 1)), Color{0, 0, 0, 255});
        float black = SmoothStep(0.75f, 1.0f, grey);
        if (black > 0) DrawRectangle(0, 0, (int)sw, (int)sh, Color{0, 0, 0, (unsigned char)(black * 255.0f)});
    }
    if (red > 0.01f) {
        DrawRectangle(0, 0, (int)sw, (int)sh, Color{170, 0, 0, (unsigned char)(Clampf(red, 0, 1) * 150.0f)});
        tunnel(::Lerp(1.9f, 0.6f, Clampf(red, 0, 1)), Color{110, 0, 0, 255});
    }
}

void DrawHud(const Aircraft& a, const Controls& c, const WarningSystem& w, const Terrain& t,
             const Camera3D& cam, const HudInfo& info)
{
    using ui::L;
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    S = fminf(sh / 900.0f, sw / 1500.0f);
    g_ap = info.ap;
    g_baro = c.baro;
    if (S < 0.5f) S = 0.5f;

    static float prevIas = 0, trend = 0;
    float dt = GetFrameTime();
    if (dt > 0 && !info.paused) {
        float acc = (a.ias - prevIas) / dt * MS_TO_KT;
        trend = Lerp(trend, acc * 10.0f, fminf(dt * 2.0f, 1.0f));
        prevIas = a.ias;
    }

    const float panelH = 300 * S;
    const float panelTop = sh - panelH;

    if (info.cam == CamMode::Cockpit && !a.crashed) {
        DrawCockpitHud(a, cam);
        DrawCockpitFrame(panelTop);
    }

    DrawRectangle(0, (int)panelTop, sw, (int)panelH, kPanel);
    DrawLineEx({0, panelTop}, {(float)sw, panelTop}, 2.0f, Color{60, 64, 72, 255});

    // PFD
    Ils ils = ComputeIls(a);
    const float A = 205 * S;
    const float px = 14 * S;
    const float cy = panelTop + 28 * S + A * 0.5f;
    const float tapeW = 78 * S;
    const float attCx = px + tapeW + 10 * S + A * 0.5f;
    DrawSpeedTape(a, px, cy, tapeW, A, trend);
    DrawAttitude(a, w, ils, attCx, cy, A, info.time);
    DrawAltTape(a, t, attCx + A * 0.5f + 8 * S, cy, 80 * S, A);
    DrawVsi(a, attCx + A * 0.5f + 92 * S, cy, 26 * S, A * 0.9f);
    DrawHeadingTape(a, attCx, cy + A * 0.5f + 28 * S, A, 24 * S);
    DrawFma(px, panelTop - 28 * S);

    // Двигатели, механизация, шасси
    float sysX = attCx + A * 0.5f + 135 * S;
    DrawSystems(a, c, sysX, panelTop + 18 * S);
    DrawData(a, w, sysX + 120 * S, panelTop + 160 * S);

    // Справочная колонка и сообщения об отказах
    float ix = sysX + 455 * S;
    if (ix + 220 * S < sw) {
        char buf[128];
        snprintf(buf, sizeof buf, "%s · %s (C)", ui::ru ? a.Type().nameRu : a.Type().nameEn, CamModeName(info.cam));
        Text(buf, ix, panelTop + 14 * S, 14, WHITE);
        snprintf(buf, sizeof buf, L("Wind %s (F8)   %s", "Ветер %s (F8)   %s"), info.windName,
                 info.gamepad ? L("gamepad", "геймпад") : (info.mouseYoke ? L("mouse yoke", "штурвал-мышь") : L("keyboard", "клавиатура")));
        Text(buf, ix, panelTop + 34 * S, 12, Color{180, 184, 190, 255});
        snprintf(buf, sizeof buf, "VS %d   VLIM %d kt   QNH %d   DA %d ft", (int)a.StallSpeedKt(), (int)a.SpeedLimitKt(), (int)roundf(a.qnh),
                 (int)(a.DensityAltFt() / 10.0f) * 10);
        Text(buf, ix, panelTop + 52 * S, 12, Color{150, 154, 160, 255});
        float ecamY = panelTop + 78 * S;
        if (g_ap && g_ap->RouteActive()) {   // следующая точка маршрута
            const Waypoint& w = g_ap->route[g_ap->activeWp];
            float gsMs = sqrtf(a.vel.x * a.vel.x + a.vel.z * a.vel.z);
            int ete = gsMs > 10.0f ? (int)(g_ap->navDistM / gsMs) : 0;
            snprintf(buf, sizeof buf, "%s %s  %.1f NM  %03d°%s%s", g_ap->navOn ? "NAV" : "WPT", w.name, g_ap->navDistM / 1852.0f,
                     (int)roundf(g_ap->navTrack) % 360, ete ? TextFormat("  %d:%02d", ete / 60, ete % 60) : "",
                     w.altFt > 0 ? TextFormat("  %d ft", (int)w.altFt) : "");
            Text(buf, ix, panelTop + 68 * S, 12, Color{230, 80, 230, 255});
            ecamY += 12 * S;
        }
        DrawEcam(a, ix, ecamY, sw - ix - 14 * S);
        Text(L("F1 - help   T - autopilot   Esc - menu", "F1 — справка   T — автопилот   Esc — меню"), ix, panelTop + panelH - 26 * S, 12,
             Color{150, 154, 160, 255});
    }

    if (info.touchdownMsgTimer > 0) {
        char buf[160];
        float fpm = -info.lastTouchdownFpm;
        const char* rating = fpm < 120 ? L("BUTTER", "ИДЕАЛЬНО") : fpm < 300 ? L("SMOOTH", "МЯГКО") : fpm < 600 ? L("FIRM", "ЖЁСТКО") : L("HARD", "ГРУБО");
        snprintf(buf, sizeof buf, L("TOUCHDOWN %.0f fpm - %s   centerline %.1f m", "КАСАНИЕ %.0f fpm — %s   от оси %.1f м"), -fpm, rating,
                 info.touchdownCenterline);
        TextOutlined(buf, sw * 0.5f, panelTop - 64 * S, Fs(22), fpm < 600 ? kGreen : kAmber);
    }

    DrawMissionPanel(info);
    DrawAlerts(w, info.time);
    DrawApAndCallouts(info.callouts, info.time);

    if (info.showHelp && !a.crashed) Overlay(L("CONTROLS", "УПРАВЛЕНИЕ"), kCyan, HelpLines(a));
}
