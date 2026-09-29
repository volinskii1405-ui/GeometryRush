// Menu.cpp
#include "Menu.h"
#include "IconRenderer.h"
#include "UI.h"
#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

const float W = (float)cfg::SCREEN_W;
const float H = (float)cfg::SCREEN_H;
const Color MENU_BG = {40, 70, 170, 255};

// Навигация фокусом по вертикальному списку из n пунктов.
void NavigateFocus(int* focus, int n, bool horizontal = false) {
    int prev = horizontal ? KEY_LEFT : KEY_UP;
    int next = horizontal ? KEY_RIGHT : KEY_DOWN;
    if (ui::KeyPressed(prev) || (!horizontal && ui::KeyPressed(KEY_W))) *focus = (*focus + n - 1) % n;
    if (ui::KeyPressed(next) || (!horizontal && ui::KeyPressed(KEY_S))) *focus = (*focus + 1) % n;
}

Color C1(const Settings& s) { return icons::Palette(s.primary); }
Color C2(const Settings& s) { return icons::Palette(s.secondary); }

void Title(const char* text, float y, float size) {
    ui::TextShadowCentered(text, W / 2, y, size, WHITE);
}

} // namespace

// ============================================================ главное меню
MenuAction Menu::MainMenu(float time, const Settings& s) {
    ui::MenuBackground(time, MENU_BG);

    // логотип с пульсацией
    float pulse = 1.0f + 0.03f * sinf(time * 3.0f);
    float ts = 92 * pulse;
    ui::TextShadowCentered("GEOMETRY", W / 2, 70, ts, WHITE);
    ui::TextShadowCentered("RUSH", W / 2, 70 + ts, ts, icons::Palette(s.primary));

    // три текущие иконки
    float iy = 325;
    icons::DrawCube(s.cubeIcon, {W / 2 - 150, iy}, 64, sinf(time * 2) * 8, C1(s), C2(s));
    icons::DrawBall(s.ballIcon, {W / 2, iy}, 64, time * 120.0f, C1(s), C2(s));
    icons::DrawShip(s.shipIcon, {W / 2 + 150, iy + sinf(time * 3) * 6}, 56, sinf(time * 3 + 1) * 10, false,
                    C1(s), C2(s), s.cubeIcon);

    const char* labels[] = {"PLAY", "EDITOR", "ICONS", "QUIT"};
    const MenuAction acts[] = {MenuAction::OpenLevels, MenuAction::OpenEditor, MenuAction::OpenIcons, MenuAction::Quit};
    NavigateFocus(&focus_, 4);
    MenuAction result = MenuAction::None;
    for (int i = 0; i < 4; ++i) {
        Rectangle r = {W / 2 - 150, 396.0f + i * 58.0f, 300, 48};
        if (ui::Hovered(r)) focus_ = i;
        if (ui::Button(r, labels[i], focus_ == i, 26)) result = acts[i];
    }
    if (ui::AcceptPressed()) result = acts[focus_];
    if (ui::BackPressed()) result = MenuAction::Quit;

    ui::Text("Space / Up / LMB - jump    Esc - pause", 20, H - 34, 20, Fade(WHITE, 0.7f));
    ui::Text("v1.0", W - 70, H - 34, 20, Fade(WHITE, 0.5f));
    return result;
}

// ============================================================ выбор уровня
MenuAction Menu::LevelSelect(float time, const std::vector<LevelCard>& levels, const Settings& s) {
    ui::MenuBackground(time, {60, 40, 150, 255});
    Title("SELECT LEVEL", 40, 56);

    if (levels.empty()) {
        ui::TextCentered("No levels found in 'levels' folder", W / 2, H / 2 - 20, 30, WHITE);
        if (ui::Button({W / 2 - 110, H - 120, 220, 56}, "BACK", true) || ui::BackPressed()) return MenuAction::Back;
        return MenuAction::None;
    }
    int n = (int)levels.size();
    selectedLevel_ = std::clamp(selectedLevel_, 0, n - 1);

    Rectangle leftBtn = {70, H / 2 - 50, 80, 100};
    Rectangle rightBtn = {W - 150, H / 2 - 50, 80, 100};
    if (ui::Button(leftBtn, "<", false, 50) || ui::KeyPressed(KEY_LEFT) || ui::KeyPressed(KEY_A)) {
        selectedLevel_ = (selectedLevel_ + n - 1) % n;
        levelSlide_ = -1;
    }
    if (ui::Button(rightBtn, ">", false, 50) || ui::KeyPressed(KEY_RIGHT) || ui::KeyPressed(KEY_D)) {
        selectedLevel_ = (selectedLevel_ + 1) % n;
        levelSlide_ = 1;
    }
    levelSlide_ *= expf(-GetFrameTime() * 12.0f);

    const LevelCard& L = levels[selectedLevel_];
    Rectangle card = {W / 2 - 380 + levelSlide_ * 120, 150, 760, 380};
    bool hover = ui::Hovered(card);
    ui::Panel(card, hover ? Color{50, 60, 110, 240} : Color{30, 36, 70, 235}, hover ? WHITE : Fade(WHITE, 0.6f));

    char buf[128];
    std::snprintf(buf, sizeof buf, "%d / %d", selectedLevel_ + 1, n);
    ui::TextCentered(buf, card.x + card.width / 2, card.y + 20, 22, Fade(WHITE, 0.6f));
    ui::TextShadowCentered(L.name.c_str(), card.x + card.width / 2, card.y + 60, 52, WHITE);

    // иконки режимов, которые есть в уровне (все три демонстрируют игрока)
    float cy = card.y + 175;
    icons::DrawCube(s.cubeIcon, {card.x + card.width / 2 - 110, cy}, 56, 0, C1(s), C2(s));
    icons::DrawBall(s.ballIcon, {card.x + card.width / 2, cy}, 56, time * 90, C1(s), C2(s));
    icons::DrawShip(s.shipIcon, {card.x + card.width / 2 + 110, cy}, 50, sinf(time * 2) * 8, false, C1(s), C2(s), s.cubeIcon);

    {
        std::string badge = L.custom ? "CUSTOM" : (L.difficulty.empty() ? "" : L.difficulty);
        if (!badge.empty()) {
            Color bc = L.custom ? Color{120, 200, 255, 255}
                     : badge == "Easy" ? Color{90, 230, 110, 255}
                     : badge == "Normal" ? Color{255, 220, 70, 255}
                     : badge == "Hard" ? Color{255, 140, 50, 255}
                     : Color{255, 70, 90, 255};
            float bw = ui::TextWidth(badge.c_str(), 20) + 24;
            Rectangle br = {card.x + card.width - bw - 20, card.y + 16, bw, 30};
            DrawRectangleRounded(br, 0.5f, 8, bc);
            ui::TextCentered(badge.c_str(), br.x + bw / 2, br.y + 5, 20, Color{20, 20, 30, 255});
        }
    }
    std::snprintf(buf, sizeof buf, "Length: %d tiles    Portals: %d", L.lengthTiles, L.portalCount);
    ui::TextCentered(buf, card.x + card.width / 2, card.y + 230, 22, Fade(WHITE, 0.85f));

    std::snprintf(buf, sizeof buf, "Best: %d%%", L.bestPercent);
    ui::TextCentered(buf, card.x + card.width / 2, card.y + 272, 24, WHITE);
    ui::ProgressBar({card.x + 80, card.y + 305, card.width - 160, 22}, L.bestPercent / 100.0f,
                    L.bestPercent >= 100 ? Color{90, 255, 120, 255} : Color{90, 200, 255, 255},
                    Fade(BLACK, 0.5f));
    ui::TextCentered("click card or press Enter to play", card.x + card.width / 2, card.y + 342, 20,
                     Fade(WHITE, 0.55f + 0.3f * sinf(time * 4)));

    if (ui::Clicked(card) || ui::AcceptPressed()) return MenuAction::StartLevel;
    if (ui::Button({W / 2 - 110, H - 110, 220, 56}, "BACK", false) || ui::BackPressed()) return MenuAction::Back;
    return MenuAction::None;
}

// ============================================================ выбор иконки
MenuAction Menu::IconSelect(float time, Settings& s, bool* changed) {
    ui::MenuBackground(time, {30, 100, 120, 255});
    Title("ICONS", 22, 50);

    // вкладки режимов
    const char* tabs[] = {"CUBE", "BALL", "SHIP"};
    for (int i = 0; i < 3; ++i) {
        Rectangle r = {W / 2 - 300 + i * 205.0f, 86, 190, 48};
        if (ui::Button(r, tabs[i], iconTab_ == i, 26)) iconTab_ = i;
    }
    if (ui::KeyPressed(KEY_ONE)) iconTab_ = 0;
    if (ui::KeyPressed(KEY_TWO)) iconTab_ = 1;
    if (ui::KeyPressed(KEY_THREE)) iconTab_ = 2;
    if (ui::KeyPressed(KEY_TAB)) iconTab_ = (iconTab_ + 1) % 3;

    PlayerMode mode = (PlayerMode)iconTab_;
    int& sel = s.IconFor(iconTab_);
    int count = icons::Count(mode);

    // клавиатура: стрелки листают иконки
    int before = sel;
    if (ui::KeyPressed(KEY_RIGHT)) sel = (sel + 1) % count;
    if (ui::KeyPressed(KEY_LEFT)) sel = (sel + count - 1) % count;
    if (ui::KeyPressed(KEY_DOWN)) sel = (sel + 4) % count;
    if (ui::KeyPressed(KEY_UP)) sel = (sel + count - 4) % count;
    if (sel != before) *changed = true;

    // сетка 4 x 2
    const float cell = 118, gap = 14;
    float gx = 90, gy = 160;
    for (int i = 0; i < count; ++i) {
        int col = i % 4, row = i / 4;
        Rectangle r = {gx + col * (cell + gap), gy + row * (cell + gap + 22), cell, cell};
        bool active = sel == i;
        bool hover = ui::Hovered(r);
        DrawRectangleRounded(r, 0.2f, 8, active ? Fade(WHITE, 0.28f) : Fade(BLACK, hover ? 0.2f : 0.35f));
        DrawRectangleRoundedLinesEx(r, 0.2f, 8, active ? 4.0f : 2.0f, active ? WHITE : Fade(WHITE, hover ? 0.7f : 0.3f));
        float rot = (hover || active) ? (mode == PlayerMode::Ship ? sinf(time * 3) * 10 : time * 90) : 0;
        if (mode == PlayerMode::Cube && (hover || active)) rot = sinf(time * 3) * 12;
        icons::DrawIcon(mode, i, {r.x + cell / 2, r.y + cell / 2 + (mode == PlayerMode::Ship ? 6 : 0)},
                        mode == PlayerMode::Ship ? 58 : 66, rot, C1(s), C2(s), s.cubeIcon);
        ui::TextCentered(icons::Name(mode, i), r.x + cell / 2, r.y + cell + 4, 18, Fade(WHITE, active ? 1.0f : 0.7f));
        if (ui::Clicked(r)) { sel = i; *changed = true; }
    }

    // палитры
    auto paletteRow = [&](const char* label, int* value, float y) {
        ui::Text(label, 90, y + 10, 22, WHITE);
        for (int i = 0; i < icons::PALETTE_SIZE; ++i) {
            Rectangle r = {230 + i * 44.0f, y, 38, 38};
            DrawRectangleRounded(r, 0.3f, 6, icons::Palette(i));
            bool active = *value == i;
            DrawRectangleRoundedLinesEx(r, 0.3f, 6, active ? 4.0f : 2.0f,
                                        active ? WHITE : (ui::Hovered(r) ? Fade(WHITE, 0.8f) : Fade(BLACK, 0.5f)));
            if (ui::Clicked(r)) { *value = i; *changed = true; }
        }
    };
    paletteRow("Primary", &s.primary, 488);
    paletteRow("Secondary", &s.secondary, 538);

    // след
    Rectangle trailBox = {90, 600, 34, 34};
    DrawRectangleRounded(trailBox, 0.25f, 6, s.trail ? icons::Palette(s.primary) : Fade(BLACK, 0.4f));
    DrawRectangleRoundedLinesEx(trailBox, 0.25f, 6, 3, WHITE);
    if (s.trail) ui::TextCentered("x", trailBox.x + 17, trailBox.y + 4, 26, BLACK);
    ui::Text("Trail  (T)", 136, 606, 24, WHITE);
    if (ui::Clicked({90, 600, 200, 34}) || ui::KeyPressed(KEY_T)) { s.trail = !s.trail; *changed = true; }

    // превью справа
    Rectangle prev = {790, 160, 420, 400};
    ui::Panel(prev, Fade(BLACK, 0.35f), Fade(WHITE, 0.5f));
    ui::TextCentered("PREVIEW", prev.x + prev.width / 2, prev.y + 14, 22, Fade(WHITE, 0.7f));
    float groundY = prev.y + prev.height - 60;
    DrawRectangle((int)prev.x + 20, (int)groundY, (int)prev.width - 40, 3, Fade(WHITE, 0.7f));
    Vector2 pc = {prev.x + prev.width / 2, groundY - 70};
    float big = 110;
    if (s.trail) {
        for (int i = 1; i <= 14; ++i) {
            float px = pc.x - i * 12.0f;
            float py = pc.y + sinf(time * 3 - i * 0.25f) * 14;
            DrawCircleV({px, py}, 16 - i, Fade(icons::Palette(s.primary), 0.5f - i * 0.03f));
        }
    }
    switch (mode) {
        case PlayerMode::Cube: icons::DrawCube(sel, pc, big, sinf(time * 1.5f) * 20, C1(s), C2(s)); break;
        case PlayerMode::Ball: icons::DrawBall(sel, pc, big, time * 120, C1(s), C2(s)); break;
        case PlayerMode::Ship:
            icons::DrawShip(sel, {pc.x, pc.y + sinf(time * 3) * 8}, 100, sinf(time * 3) * 12, false, C1(s), C2(s), s.cubeIcon);
            break;
    }
    ui::TextCentered(icons::Name(mode, sel), prev.x + prev.width / 2, groundY + 16, 30, WHITE);

    ui::Text("1/2/3 or Tab - mode, arrows - icon", 800, 580, 20, Fade(WHITE, 0.6f));
    if (ui::Button({W - 250, H - 90, 200, 56}, "BACK", false) || ui::BackPressed()) return MenuAction::Back;
    return MenuAction::None;
}

// ============================================================ пауза
MenuAction Menu::Pause(float time) {
    DrawRectangle(0, 0, (int)W, (int)H, Fade(BLACK, 0.55f));
    Title("PAUSED", 150, 72);
    (void)time;
    const char* labels[] = {"RESUME", "RESTART", "MENU"};
    const MenuAction acts[] = {MenuAction::Resume, MenuAction::Restart, MenuAction::ToMenu};
    NavigateFocus(&focus_, 3);
    MenuAction result = MenuAction::None;
    for (int i = 0; i < 3; ++i) {
        Rectangle r = {W / 2 - 150, 290.0f + i * 80.0f, 300, 60};
        if (ui::Hovered(r)) focus_ = i;
        if (ui::Button(r, labels[i], focus_ == i)) result = acts[i];
    }
    if (ui::KeyPressed(KEY_ENTER) || ui::KeyPressed(KEY_KP_ENTER)) result = acts[focus_];
    if (ui::KeyPressed(KEY_ESCAPE) || ui::KeyPressed(KEY_P)) result = MenuAction::Resume;
    if (ui::KeyPressed(KEY_R)) result = MenuAction::Restart;
    if (ui::KeyPressed(KEY_Q)) result = MenuAction::ToMenu;
    ui::TextCentered("Esc - resume   R - restart   Q - menu", W / 2, H - 60, 22, Fade(WHITE, 0.7f));
    return result;
}

// ============================================================ победа
MenuAction Menu::Victory(float time, const VictoryStats& st, const Settings& s) {
    DrawRectangle(0, 0, (int)W, (int)H, Fade(BLACK, 0.5f));
    Rectangle p = {W / 2 - 330, 90, 660, 520};
    ui::Panel(p, Color{25, 30, 60, 240}, icons::Palette(s.primary));
    float sc = 1.0f + 0.04f * sinf(time * 5);
    ui::TextShadowCentered("LEVEL COMPLETE!", W / 2, p.y + 30, 54 * sc, Color{255, 230, 80, 255});
    ui::TextCentered(st.levelName.c_str(), W / 2, p.y + 100, 32, WHITE);

    icons::DrawCube(s.cubeIcon, {W / 2, p.y + 190}, 80, sinf(time * 2) * 10, C1(s), C2(s));

    char buf[96];
    std::snprintf(buf, sizeof buf, "Attempts: %d", st.attempts);
    ui::TextCentered(buf, W / 2, p.y + 250, 40, WHITE);
    std::snprintf(buf, sizeof buf, "Jumps: %d     Time: %d:%02d", st.jumps, (int)st.seconds / 60, (int)st.seconds % 60);
    ui::TextCentered(buf, W / 2, p.y + 302, 26, Fade(WHITE, 0.85f));
    if (st.newRecord)
        ui::TextCentered("NEW RECORD - 100%", W / 2, p.y + 340, 24, Color{120, 255, 140, 255});

    const char* labels[] = {"REPLAY", "MENU"};
    const MenuAction acts[] = {MenuAction::Replay, MenuAction::ToMenu};
    NavigateFocus(&focus_, 2, true);
    focus_ = std::clamp(focus_, 0, 1);
    MenuAction result = MenuAction::None;
    for (int i = 0; i < 2; ++i) {
        Rectangle r = {W / 2 - 250 + i * 270.0f, p.y + 410, 230, 62};
        if (ui::Hovered(r)) focus_ = i;
        if (ui::Button(r, labels[i], focus_ == i)) result = acts[i];
    }
    if (ui::AcceptPressed()) result = acts[focus_];
    if (ui::BackPressed()) result = MenuAction::ToMenu;
    return result;
}

// ============================================================ список своих уровней
MenuAction Menu::EditorBrowser(float time, const std::vector<LevelCard>& levels) {
    ui::MenuBackground(time, {30, 110, 80, 255});
    Title("LEVEL EDITOR", 30, 56);
    int n = (int)levels.size();
    editorSel_ = n ? std::clamp(editorSel_, 0, n - 1) : 0;
    deleteArmed_ = std::max(0.0f, deleteArmed_ - GetFrameTime());

    Rectangle panel = {W / 2 - 360, 110, 720, 430};
    ui::Panel(panel, Fade(BLACK, 0.4f), Fade(WHITE, 0.5f));
    if (n == 0) {
        ui::TextCentered("No custom levels yet.", W / 2, panel.y + 170, 30, WHITE);
        ui::TextCentered("Press NEW to create one!", W / 2, panel.y + 215, 24, Fade(WHITE, 0.7f));
    }
    if (ui::KeyPressed(KEY_UP) && n) editorSel_ = (editorSel_ + n - 1) % n;
    if (ui::KeyPressed(KEY_DOWN) && n) editorSel_ = (editorSel_ + 1) % n;

    const int visible = 7;
    int first = std::clamp(editorSel_ - visible / 2, 0, std::max(0, n - visible));
    for (int i = first; i < std::min(n, first + visible); ++i) {
        Rectangle r = {panel.x + 20, panel.y + 20 + (i - first) * 58.0f, panel.width - 40, 50};
        bool sel = i == editorSel_, hover = ui::Hovered(r);
        DrawRectangleRounded(r, 0.3f, 8, sel ? Fade(WHITE, 0.25f) : Fade(WHITE, hover ? 0.12f : 0.05f));
        if (sel) DrawRectangleRoundedLinesEx(r, 0.3f, 8, 2, WHITE);
        ui::Text(levels[i].name.c_str(), r.x + 18, r.y + 13, 26, WHITE);
        char buf[48];
        std::snprintf(buf, sizeof buf, "%d tiles   best %d%%", levels[i].lengthTiles, levels[i].bestPercent);
        ui::Text(buf, r.x + r.width - ui::TextWidth(buf, 18) - 18, r.y + 16, 18, Fade(WHITE, 0.6f));
        if (ui::Clicked(r)) {                     // клик по выбранному — открыть
            if (sel) return MenuAction::EditorEdit;
            editorSel_ = i;
        }
    }

    MenuAction result = MenuAction::None;
    float by = H - 150;
    if (ui::Button({W / 2 - 370, by, 170, 56}, "NEW", false, 26) || ui::KeyPressed(KEY_N)) result = MenuAction::EditorNew;
    if (n && (ui::Button({W / 2 - 185, by, 170, 56}, "EDIT", false, 26) || ui::AcceptPressed())) result = MenuAction::EditorEdit;
    if (n) {
        bool armed = deleteArmed_ > 0;
        if (ui::Button({W / 2, by, 170, 56}, armed ? "SURE?" : "DELETE", armed, 26, Color{255, 90, 90, 255}) ||
            ui::KeyPressed(KEY_DELETE)) {
            if (armed) { result = MenuAction::EditorDelete; deleteArmed_ = 0; }
            else deleteArmed_ = 2.5f;
        }
    }
    if (ui::Button({W / 2 + 185, by, 170, 56}, "BACK", false, 26) || ui::BackPressed()) result = MenuAction::Back;
    ui::TextCentered("Levels are saved to the 'mylevels' folder and appear in PLAY", W / 2, H - 70, 20, Fade(WHITE, 0.6f));
    return result;
}
