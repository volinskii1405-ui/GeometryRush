// UI.cpp
#include "UI.h"
#include "config.h"
#include <algorithm>
#include <cmath>

namespace ui {
namespace {
bool g_inputEnabled = true;
}

void SetInputEnabled(bool enabled) { g_inputEnabled = enabled; }
bool InputEnabled() { return g_inputEnabled; }
bool KeyPressed(int key) { return g_inputEnabled && IsKeyPressed(key); }
bool AcceptPressed() { return KeyPressed(KEY_ENTER) || KeyPressed(KEY_KP_ENTER) || KeyPressed(KEY_SPACE); }
bool BackPressed() { return KeyPressed(KEY_ESCAPE) || KeyPressed(KEY_BACKSPACE); }

float TextWidth(const char* text, float size) {
    return MeasureTextEx(GetFontDefault(), text, size, size / 10.0f).x;
}

void Text(const char* text, float x, float y, float size, Color color) {
    DrawTextEx(GetFontDefault(), text, {std::round(x), std::round(y)}, size, size / 10.0f, color);
}

void TextCentered(const char* text, float cx, float y, float size, Color color) {
    Text(text, cx - TextWidth(text, size) * 0.5f, y, size, color);
}

void TextShadowCentered(const char* text, float cx, float y, float size, Color color) {
    float o = std::max(2.0f, size / 16.0f);
    TextCentered(text, cx + o, y + o, size, Fade(BLACK, 0.6f));
    TextCentered(text, cx, y, size, color);
}

bool Hovered(Rectangle r) { return g_inputEnabled && CheckCollisionPointRec(GetMousePosition(), r); }
bool Clicked(Rectangle r) { return Hovered(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }

bool Button(Rectangle r, const char* label, bool focused, float fontSize, Color accent) {
    bool hover = Hovered(r);
    bool active = hover || focused;
    Rectangle rr = r;
    if (active) rr = {r.x - 4, r.y - 3, r.width + 8, r.height + 6};
    DrawRectangleRounded({rr.x + 4, rr.y + 6, rr.width, rr.height}, 0.35f, 8, Fade(BLACK, 0.35f));
    DrawRectangleRounded(rr, 0.35f, 8, active ? accent : Color{30, 36, 60, 235});
    DrawRectangleRoundedLinesEx(rr, 0.35f, 8, 3, active ? WHITE : Fade(accent, 0.8f));
    TextCentered(label, rr.x + rr.width * 0.5f, rr.y + (rr.height - fontSize) * 0.5f, fontSize,
                 active ? Color{15, 15, 30, 255} : WHITE);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void Panel(Rectangle r, Color fill, Color border) {
    DrawRectangleRounded({r.x + 6, r.y + 8, r.width, r.height}, 0.08f, 8, Fade(BLACK, 0.35f));
    DrawRectangleRounded(r, 0.08f, 8, fill);
    DrawRectangleRoundedLinesEx(r, 0.08f, 8, 3, border);
}

void ProgressBar(Rectangle r, float t, Color fill, Color back) {
    t = std::clamp(t, 0.0f, 1.0f);
    DrawRectangleRounded(r, 1.0f, 8, back);
    if (t > 0.001f) {
        float w = std::max(r.height, r.width * t);
        DrawRectangleRounded({r.x, r.y, w, r.height}, 1.0f, 8, fill);
    }
    DrawRectangleRoundedLinesEx(r, 1.0f, 8, 2, Fade(WHITE, 0.8f));
}

void MenuBackground(float time, Color base) {
    const float W = (float)cfg::SCREEN_W, H = (float)cfg::SCREEN_H;
    Color top = {(unsigned char)(base.r * 0.9f), (unsigned char)(base.g * 0.9f), (unsigned char)(base.b * 0.9f), 255};
    Color bot = {(unsigned char)(base.r * 0.25f), (unsigned char)(base.g * 0.25f), (unsigned char)(base.b * 0.3f), 255};
    DrawRectangleGradientV(0, 0, (int)W, (int)H, top, bot);
    // плывущие квадраты двух слоёв
    for (int layer = 0; layer < 2; ++layer) {
        float speed = layer == 0 ? 20.0f : 55.0f;
        float size = layer == 0 ? 120.0f : 60.0f;
        float spacing = layer == 0 ? 260.0f : 170.0f;
        int n = (int)(W / spacing) + 3;
        for (int i = 0; i < n; ++i) {
            float x = std::fmod(i * spacing - time * speed, n * spacing);
            if (x < -spacing) x += n * spacing;
            float y = H * (0.2f + 0.6f * std::fmod(i * 0.618f + layer * 0.3f, 1.0f));
            float rot = time * (layer ? 25.0f : -12.0f) + i * 30.0f;
            DrawRectanglePro({x, y, size, size}, {size / 2, size / 2}, rot, Fade(WHITE, layer ? 0.05f : 0.035f));
        }
    }
    // сетка пола
    float gy = H - 64;
    DrawRectangle(0, (int)gy, (int)W, (int)(H - gy), Fade(BLACK, 0.35f));
    DrawRectangle(0, (int)gy, (int)W, 3, Fade(WHITE, 0.7f));
    float off = std::fmod(time * 200.0f, 80.0f);
    for (float x = -off; x < W; x += 80.0f) DrawLineEx({x, gy}, {x - 40, H}, 2, Fade(WHITE, 0.08f));
}

} // namespace ui
