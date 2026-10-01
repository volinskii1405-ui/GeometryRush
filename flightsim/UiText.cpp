#include "UiText.h"

#include <vector>

namespace ui {

bool ru = true;

namespace {

// Два размера атласа: мелкий текст чётче из маленького, крупный — из большого.
Font g_small[2], g_large[2];   // [обычный, жирный]
bool g_loaded = false;

const Font& Pick(float size, bool bold)
{
    if (!g_loaded) {
        static Font def = GetFontDefault();
        return def;
    }
    return size <= 22.0f ? g_small[bold ? 1 : 0] : g_large[bold ? 1 : 0];
}

Font LoadOne(const char* file, int px, const std::vector<int>& cps)
{
    const char* path = TextFormat("%sfonts/%s", GetApplicationDirectory(), file);
    if (!FileExists(path)) return Font{};
    Font f = LoadFontEx(path, px, const_cast<int*>(cps.data()), (int)cps.size());
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    return f;
}

} // namespace

void LoadFonts()
{
    std::vector<int> cps;
    for (int c = 32; c < 127; ++c) cps.push_back(c);
    for (int c = 0x400; c <= 0x45F; ++c) cps.push_back(c);   // кириллица
    for (int c : {0xB0, 0xAB, 0xBB, 0xB7, 0xD7, 0x2014, 0x2013, 0x2116, 0x2190, 0x2191, 0x2192, 0x2193, 0x2605, 0x2606, 0x2022,
                  0x2039, 0x203A, 0x25CF, 0x25CB, 0x2713, 0x2717})
        cps.push_back(c);   // ° « » · × — – № стрелки ★ ☆ • ‹ › ● ○ ✓ ✗

    g_small[0] = LoadOne("DejaVuSans.ttf", 22, cps);
    g_small[1] = LoadOne("DejaVuSans-Bold.ttf", 22, cps);
    g_large[0] = LoadOne("DejaVuSans.ttf", 64, cps);
    g_large[1] = LoadOne("DejaVuSans-Bold.ttf", 64, cps);
    g_loaded = g_small[0].texture.id && g_small[1].texture.id && g_large[0].texture.id && g_large[1].texture.id;
    if (!g_loaded) {
        TraceLog(LOG_WARNING, "UI: fonts/ not found, falling back to the default font (no Cyrillic)");
        ru = false;
    }
}

void UnloadFonts()
{
    for (Font* f : {&g_small[0], &g_small[1], &g_large[0], &g_large[1]})
        if (f->texture.id) UnloadFont(*f);
    g_loaded = false;
}

float Measure(const char* s, float size, bool bold)
{
    return MeasureTextEx(Pick(size, bold), s, size, size * 0.02f).x;
}

void Draw(const char* s, float x, float y, float size, Color c, bool bold)
{
    DrawTextEx(Pick(size, bold), s, Vector2{(float)(int)x, (float)(int)y}, size, size * 0.02f, c);
}

void DrawCentered(const char* s, float cx, float y, float size, Color c, bool bold)
{
    Draw(s, cx - Measure(s, size, bold) * 0.5f, y, size, c, bold);
}

void DrawRight(const char* s, float rx, float y, float size, Color c, bool bold)
{
    Draw(s, rx - Measure(s, size, bold), y, size, c, bold);
}

void DrawOutlined(const char* s, float cx, float y, float size, Color c, bool bold)
{
    float x = cx - Measure(s, size, bold) * 0.5f;
    Color o{0, 0, 0, (unsigned char)(c.a * 0.8f)};
    float d = size > 30 ? 2.5f : 1.5f;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            if (dx || dy) Draw(s, x + dx * d, y + dy * d, size, o, bold);
    Draw(s, x, y, size, c, bold);
}

} // namespace ui
