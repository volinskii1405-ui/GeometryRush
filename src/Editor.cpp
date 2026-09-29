// Editor.cpp
#include "Editor.h"
#include "UI.h"
#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

const float W = (float)cfg::SCREEN_W;
const float H = (float)cfg::SCREEN_H;
const int   DEFAULT_ROWS = 12;
const int   DEFAULT_COLS = 80;
const float TOOLBAR_Y = 590;   // ниже — панель инструментов
const float TOPBAR_H = 64;     // выше — верхняя панель

struct Tool { char ch; const char* name; };
const Tool TOOLS[] = {
    {'#', "Block"}, {'^', "Spike"}, {'v', "Spike v"}, {'C', "Cube"}, {'B', "Ball"},
    {'S', "Ship"},  {'G', "Gravity"}, {'N', "Normal"}, {'E', "Finish"}, {'.', "Erase"},
};
const int TOOL_COUNT = sizeof(TOOLS) / sizeof(TOOLS[0]);

struct Theme { Color bg, ground; };
const Theme THEMES[] = {
    {{40, 90, 200, 255}, {30, 70, 170, 255}},   {{150, 40, 110, 255}, {110, 30, 90, 255}},
    {{30, 140, 110, 255}, {20, 100, 80, 255}},  {{200, 110, 30, 255}, {150, 70, 20, 255}},
    {{90, 50, 180, 255}, {60, 30, 140, 255}},   {{170, 40, 50, 255}, {120, 25, 35, 255}},
    {{40, 40, 60, 255}, {25, 25, 40, 255}},     {{30, 150, 190, 255}, {20, 100, 140, 255}},
};
const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool IsPortal(char ch) { return ch == 'C' || ch == 'B' || ch == 'S' || ch == 'G' || ch == 'N'; }

PortalType PortalOf(char ch) {
    switch (ch) {
        case 'B': return PortalType::Ball;
        case 'S': return PortalType::Ship;
        case 'G': return PortalType::GravityFlip;
        case 'N': return PortalType::GravityNormal;
        default: return PortalType::Cube;
    }
}

// Иконка инструмента внутри прямоугольника r.
void DrawToolIcon(char ch, Rectangle r, float alpha) {
    float cx = r.x + r.width / 2, cy = r.y + r.height / 2, k = r.width * 0.36f;
    Color white = Fade(WHITE, alpha);
    switch (ch) {
        case '#':
            DrawRectangleRec({cx - k, cy - k, 2 * k, 2 * k}, Fade(Color{20, 30, 70, 255}, alpha));
            DrawRectangleLinesEx({cx - k, cy - k, 2 * k, 2 * k}, 3, white);
            break;
        case '^':
            DrawTriangle({cx, cy - k}, {cx - k, cy + k}, {cx + k, cy + k}, Fade(BLACK, alpha));
            DrawTriangleLines({cx, cy - k}, {cx - k, cy + k}, {cx + k, cy + k}, white);
            break;
        case 'v':
            DrawTriangle({cx, cy + k}, {cx + k, cy - k}, {cx - k, cy - k}, Fade(BLACK, alpha));
            DrawTriangleLines({cx, cy + k}, {cx + k, cy - k}, {cx - k, cy - k}, white);
            break;
        case 'E':
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 2; ++j)
                    DrawRectangleRec({cx - k * 0.5f + j * k * 0.5f, cy - k + i * k * 0.5f, k * 0.5f, k * 0.5f},
                                     Fade((i + j) % 2 ? WHITE : BLACK, alpha));
            break;
        case '.':
            DrawLineEx({cx - k, cy - k}, {cx + k, cy + k}, 5, Fade(Color{255, 80, 80, 255}, alpha));
            DrawLineEx({cx + k, cy - k}, {cx - k, cy + k}, 5, Fade(Color{255, 80, 80, 255}, alpha));
            break;
        default:
            if (IsPortal(ch)) {
                Color pc = PortalColor(PortalOf(ch));
                DrawEllipse((int)cx, (int)cy, k * 0.55f, k * 1.1f, Fade(pc, 0.35f * alpha));
                DrawEllipseLines((int)cx, (int)cy, k * 0.55f, k * 1.1f, Fade(pc, alpha));
                char s[2] = {ch, 0};
                ui::TextCentered(s, cx, cy - 9, 18, white);
            }
            break;
    }
}

} // namespace

// ============================================================ файл

void Editor::NewLevel(const std::string& dir) {
    grid_.assign(DEFAULT_ROWS, std::string(DEFAULT_COLS, '.'));
    char buf[64];
    int n = 1;
    std::string path;
    do {
        std::snprintf(buf, sizeof buf, "level_%02d.txt", n++);
        path = dir + "/" + buf;
    } while (FileExists(path.c_str()) && n < 1000);
    path_ = path;
    std::snprintf(buf, sizeof buf, "My Level %d", n - 1);
    name_ = buf;
    theme_ = 0;
    bg_ = THEMES[0].bg;
    ground_ = THEMES[0].ground;
    camX_ = 0;
    undo_.clear();
    dirty_ = true;
    editingName_ = false;
    tool_ = 0;
    RebuildPreview();
}

bool Editor::Open(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    grid_.clear();
    name_ = GetFileNameWithoutExt(path.c_str());
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = Trim(line);
        if (t.rfind("//", 0) == 0) continue;
        if (!t.empty() && t[0] == '@') {
            std::istringstream in(t.substr(1));
            std::string key;
            in >> key;
            if (key == "name") { std::getline(in, name_); name_ = Trim(name_); }
            else if (key == "bg" || key == "ground") {
                int r, g, b;
                if (in >> r >> g >> b) {
                    Color c = {(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};
                    (key == "bg" ? bg_ : ground_) = c;
                }
            }
            continue;
        }
        if (t.empty() && grid_.empty()) continue;
        for (char& ch : line) if (ch == ' ') ch = '.';
        grid_.push_back(line);
    }
    while (!grid_.empty() && Trim(grid_.back()).empty()) grid_.pop_back();
    if (grid_.empty()) grid_.assign(DEFAULT_ROWS, std::string(DEFAULT_COLS, '.'));
    size_t width = DEFAULT_COLS;
    for (auto& r : grid_) width = std::max(width, r.size());
    for (auto& r : grid_) r.resize(width, '.');
    path_ = path;
    camX_ = 0;
    undo_.clear();
    dirty_ = false;
    editingName_ = false;
    RebuildPreview();
    return true;
}

int Editor::LastUsedColumn() const {
    int last = -1;
    for (auto& r : grid_)
        for (int c = (int)r.size() - 1; c > last; --c)
            if (r[c] != '.') { last = c; break; }
    return last;
}

std::string Editor::Serialize() const {
    std::ostringstream out;
    out << "// Made with the Geometry Rush editor\n";
    out << "@name " << name_ << "\n";
    out << "@bg " << (int)bg_.r << " " << (int)bg_.g << " " << (int)bg_.b << "\n";
    out << "@ground " << (int)ground_.r << " " << (int)ground_.g << " " << (int)ground_.b << "\n";
    int width = std::max(LastUsedColumn() + 1, 20);
    for (auto& r : grid_) out << r.substr(0, std::min<size_t>(width, r.size())) << "\n";
    return out.str();
}

bool Editor::Save() {
    std::ofstream f(path_, std::ios::binary);
    if (!f) {
        ShowMessage("Cannot save " + path_);
        return false;
    }
    f << Serialize();
    dirty_ = false;
    ShowMessage("Saved " + std::string(GetFileName(path_.c_str())));
    return true;
}

void Editor::ShowMessage(const std::string& msg, float seconds) {
    message_ = msg;
    messageTime_ = seconds;
}

// ============================================================ правка

void Editor::EnsureWidth(int cols) {
    if (grid_.empty() || (int)grid_[0].size() >= cols) return;
    for (auto& r : grid_) r.resize(cols, '.');
}

char Editor::GetCell(int col, int row) const {
    if (row < 0 || row >= (int)grid_.size() || col < 0 || col >= (int)grid_[row].size()) return '.';
    return grid_[row][col];
}

void Editor::SetCell(int col, int row, char ch) {
    if (row < 0 || row >= (int)grid_.size() || col < 0) return;
    if (GetCell(col, row) == ch) return;
    if (!stroke_) {                       // одна запись отмены на весь мазок
        PushUndo();
        stroke_ = true;
    }
    EnsureWidth(col + 40);
    if (ch == 'E')                        // финиш может быть только один
        for (auto& r : grid_)
            for (char& c : r) if (c == 'E') c = '.';
    grid_[row][col] = ch;
    dirty_ = true;
    RebuildPreview();
}

void Editor::PushUndo() {
    undo_.push_back(grid_);
    if (undo_.size() > 200) undo_.erase(undo_.begin());
}

void Editor::Undo() {
    if (undo_.empty()) {
        ShowMessage("Nothing to undo", 1.2f);
        return;
    }
    grid_ = undo_.back();
    undo_.pop_back();
    dirty_ = true;
    RebuildPreview();
}

void Editor::RebuildPreview() {
    std::string err;
    preview_.LoadFromString(Serialize(), &err);
}

// ============================================================ кадр

EditorAction Editor::UpdateDraw(float dt, float time) {
    messageTime_ -= dt;
    const float T = cfg::TILE;
    const float camY = preview_.FloorY() - cfg::CAMERA_FLOOR_SCREEN_Y;
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    EditorAction result = EditorAction::None;

    // ---------------- ввод имени
    if (editingName_) {
        int ch;
        while ((ch = GetCharPressed()) != 0)
            if (ch >= 32 && ch < 127 && name_.size() < 28) { name_ += (char)ch; dirty_ = true; }
        if (IsKeyPressed(KEY_BACKSPACE) && !name_.empty()) { name_.pop_back(); dirty_ = true; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
            editingName_ = false;
            if (Trim(name_).empty()) name_ = "Untitled";
            RebuildPreview();
        }
    } else if (ui::InputEnabled()) {
        // ---------------- горячие клавиши
        for (int i = 0; i < TOOL_COUNT; ++i)
            if (IsKeyPressed(i == 9 ? KEY_ZERO : KEY_ONE + i)) tool_ = i;
        if (IsKeyPressed(KEY_Q)) tool_ = (tool_ + TOOL_COUNT - 1) % TOOL_COUNT;
        if (IsKeyPressed(KEY_E)) tool_ = (tool_ + 1) % TOOL_COUNT;
        if (ctrl && IsKeyPressed(KEY_Z)) Undo();
        if (ctrl && IsKeyPressed(KEY_S)) Save();
        if (IsKeyPressed(KEY_T)) {
            theme_ = (theme_ + 1) % THEME_COUNT;
            bg_ = THEMES[theme_].bg;
            ground_ = THEMES[theme_].ground;
            dirty_ = true;
            RebuildPreview();
        }
        if (IsKeyPressed(KEY_HOME)) camX_ = 0;
        if (IsKeyPressed(KEY_END)) camX_ = std::max(0.0f, (LastUsedColumn() + 1) * T - W * 0.6f);
        if (IsKeyPressed(KEY_F5) || IsKeyPressed(KEY_ENTER)) result = EditorAction::TestPlay;
        if (IsKeyPressed(KEY_ESCAPE)) result = EditorAction::Back;

        // ---------------- прокрутка
        float pan = (IsKeyDown(KEY_LEFT_SHIFT) ? 1800.0f : 700.0f) * dt;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) camX_ += pan;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) camX_ -= pan;
        Vector2 wheel = GetMouseWheelMoveV();
        camX_ -= (wheel.y + wheel.x) * T * 3;
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) camX_ -= GetMouseDelta().x;
        camX_ = std::max(camX_, -T * 2);
    }

    // ---------------- рисование мышью
    Vector2 m = GetMousePosition();
    bool inGrid = m.y > TOPBAR_H && m.y < TOOLBAR_Y;
    int col = (int)std::floor((m.x + camX_) / T);
    int row = (int)std::floor((m.y + camY) / T);
    bool validCell = inGrid && col >= 0 && row >= 0 && row < (int)grid_.size();
    if (!editingName_ && ui::InputEnabled() && validCell) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) SetCell(col, row, TOOLS[tool_].ch);
        else if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) SetCell(col, row, '.');
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) stroke_ = false;

    // ---------------- отрисовка
    DrawWorld(time);
    if (validCell) {
        Rectangle cell = {col * T - camX_, row * T - camY, T, T};
        DrawToolIcon(TOOLS[tool_].ch, cell, 0.55f);
        DrawRectangleLinesEx(cell, 2, WHITE);
        char buf[32];
        std::snprintf(buf, sizeof buf, "%d, %d", col, row);
        ui::Text(buf, cell.x + T + 4, cell.y - 16, 16, Fade(WHITE, 0.8f));
    }
    DrawTopBar();
    DrawToolbar();

    // кнопки верхней панели
    if (ui::Button({W - 520, 10, 110, 44}, "TEST", false, 22)) result = EditorAction::TestPlay;
    if (ui::Button({W - 400, 10, 110, 44}, "SAVE", false, 22)) Save();
    if (ui::Button({W - 280, 10, 110, 44}, "THEME", false, 22)) {
        theme_ = (theme_ + 1) % THEME_COUNT;
        bg_ = THEMES[theme_].bg;
        ground_ = THEMES[theme_].ground;
        dirty_ = true;
        RebuildPreview();
    }
    if (ui::Button({W - 160, 10, 140, 44}, "BACK", false, 22)) result = EditorAction::Back;

    if (messageTime_ > 0) {
        float a = std::min(1.0f, messageTime_ * 2);
        float w = ui::TextWidth(message_.c_str(), 24) + 40;
        DrawRectangleRounded({W / 2 - w / 2, 80, w, 40}, 0.5f, 8, Fade(BLACK, 0.7f * a));
        ui::TextCentered(message_.c_str(), W / 2, 88, 24, Fade(WHITE, a));
    }

    // тест и выход — всегда с сохранением, чтобы ничего не потерять
    if (result != EditorAction::None) {
        editingName_ = false;
        if (dirty_) Save();
    }
    return result;
}

void Editor::DrawWorld(float time) {
    const float T = cfg::TILE;
    const float camY = preview_.FloorY() - cfg::CAMERA_FLOOR_SCREEN_Y;
    DrawRectangleGradientV(0, 0, (int)W, (int)H, bg_, Color{(unsigned char)(bg_.r * 0.35f), (unsigned char)(bg_.g * 0.35f),
                                                          (unsigned char)(bg_.b * 0.35f), 255});
    Camera2D cam{};
    cam.target = {camX_, camY};
    cam.zoom = 1;
    BeginMode2D(cam);

    float floorY = preview_.FloorY();
    int rows = (int)grid_.size();
    // пол
    DrawRectangleRec({camX_ - 10, floorY, W + 20, 400}, Color{(unsigned char)(ground_.r * 0.6f),
                     (unsigned char)(ground_.g * 0.6f), (unsigned char)(ground_.b * 0.6f), 255});
    DrawRectangleRec({camX_ - 10, floorY, W + 20, 3}, Fade(WHITE, 0.8f));

    // сетка
    int c0 = std::max(0, (int)std::floor(camX_ / T)), c1 = (int)((camX_ + W) / T) + 1;
    for (int c = c0; c <= c1; ++c) {
        Color lc = Fade(WHITE, c % 10 == 0 ? 0.22f : 0.07f);
        DrawLineEx({c * T, 0}, {c * T, floorY}, 1, lc);
        if (c % 10 == 0) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%d", c);
            ui::Text(buf, c * T + 3, floorY + 8, 16, Fade(WHITE, 0.5f));
        }
    }
    for (int r = 0; r <= rows; ++r) DrawLineEx({camX_, r * T}, {camX_ + W, r * T}, 1, Fade(WHITE, 0.07f));

    // потолок коридора (корабль / шар / перевёрнутый куб)
    float ceilY = preview_.CeilingY();
    for (float x = std::floor(camX_ / 30) * 30; x < camX_ + W; x += 30)
        DrawLineEx({x, ceilY}, {x + 15, ceilY}, 2, Fade(Color{255, 120, 220, 255}, 0.6f));
    ui::Text("corridor ceiling", camX_ + 10, ceilY - 20, 16, Fade(Color{255, 150, 230, 255}, 0.7f));

    preview_.Draw(camX_, camX_ + W, time);

    // старт игрока
    Rectangle start = {cfg::PLAYER_START_X, floorY - cfg::PLAYER_SIZE, cfg::PLAYER_SIZE, cfg::PLAYER_SIZE};
    DrawRectangleRec(start, Fade(WHITE, 0.2f));
    DrawRectangleLinesEx(start, 2, WHITE);
    ui::Text("START", start.x - 4, start.y - 20, 16, WHITE);
    EndMode2D();
}

void Editor::DrawTopBar() {
    DrawRectangle(0, 0, (int)W, (int)TOPBAR_H, Fade(BLACK, 0.6f));
    Rectangle nameBox = {16, 12, 380, 40};
    bool hover = ui::Hovered(nameBox);
    DrawRectangleRounded(nameBox, 0.3f, 6, editingName_ ? Fade(WHITE, 0.25f) : Fade(WHITE, hover ? 0.15f : 0.08f));
    DrawRectangleRoundedLinesEx(nameBox, 0.3f, 6, 2, editingName_ ? WHITE : Fade(WHITE, 0.5f));
    std::string shown = name_;
    if (editingName_ && std::fmod(GetTime(), 1.0) < 0.5) shown += "_";
    ui::Text(shown.c_str(), nameBox.x + 12, nameBox.y + 9, 24, WHITE);
    if (!editingName_) ui::Text("click to rename", nameBox.x + nameBox.width + 10, 24, 16, Fade(WHITE, 0.45f));
    if (ui::Clicked(nameBox)) editingName_ = true;
    else if (editingName_ && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !hover) editingName_ = false;
    if (dirty_) ui::Text("*unsaved", 560, 24, 18, Color{255, 220, 90, 255});
}

void Editor::DrawToolbar() {
    DrawRectangle(0, (int)TOOLBAR_Y, (int)W, (int)(H - TOOLBAR_Y), Fade(BLACK, 0.7f));
    const float size = 64, gap = 10;
    float x0 = 20;
    for (int i = 0; i < TOOL_COUNT; ++i) {
        Rectangle r = {x0 + i * (size + gap), TOOLBAR_Y + 12, size, size};
        bool active = tool_ == i, hover = ui::Hovered(r);
        DrawRectangleRounded(r, 0.2f, 6, active ? Fade(WHITE, 0.3f) : Fade(WHITE, hover ? 0.15f : 0.06f));
        DrawRectangleRoundedLinesEx(r, 0.2f, 6, active ? 3.0f : 1.0f, active ? WHITE : Fade(WHITE, 0.4f));
        DrawToolIcon(TOOLS[i].ch, r, 1.0f);
        char key[4];
        std::snprintf(key, sizeof key, "%d", (i + 1) % 10);
        ui::Text(key, r.x + 4, r.y + 2, 14, Fade(WHITE, 0.6f));
        ui::TextCentered(TOOLS[i].name, r.x + size / 2, r.y + size + 4, 14, Fade(WHITE, active ? 1.0f : 0.6f));
        if (ui::Clicked(r)) tool_ = i;
    }
    float hx = x0 + TOOL_COUNT * (size + gap) + 10;
    const char* help[] = {
        "LMB paint   RMB erase   Ctrl+Z undo",
        "A/D, wheel, MMB drag - scroll  Home/End",
        "1-0 / Q,E tools   T theme   Ctrl+S save",
        "F5/Enter test play   Esc back (autosave)",
    };
    for (int i = 0; i < 4; ++i) ui::Text(help[i], hx, TOOLBAR_Y + 14 + i * 26.0f, 16, Fade(WHITE, 0.65f));
}
