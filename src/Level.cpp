// Level.cpp — парсер формата уровня и отрисовка объектов.
#include "Level.h"
#include "config.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool ParseColor(const std::string& args, Color* out) {
    std::istringstream in(args);
    int r, g, b;
    if (!(in >> r >> g >> b)) return false;
    auto c = [](int v) { return (unsigned char)std::clamp(v, 0, 255); };
    *out = {c(r), c(g), c(b), 255};
    return true;
}

Color Brighten(Color c, float k) {
    auto f = [k](unsigned char v) { return (unsigned char)std::min(255.0f, v + (255 - v) * k); };
    return {f(c.r), f(c.g), f(c.b), c.a};
}

Color Darken(Color c, float k) {
    auto f = [k](unsigned char v) { return (unsigned char)(v * (1.0f - k)); };
    return {f(c.r), f(c.g), f(c.b), c.a};
}

} // namespace

Color PortalColor(PortalType type) {
    switch (type) {
        case PortalType::Cube:          return {70, 235, 100, 255};
        case PortalType::Ball:          return {255, 110, 50, 255};
        case PortalType::Ship:          return {255, 80, 220, 255};
        case PortalType::GravityFlip:   return {255, 220, 40, 255};
        case PortalType::GravityNormal: return {70, 180, 255, 255};
    }
    return WHITE;
}

bool Level::LoadFromFile(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return LoadFromString(ss.str(), error);
}

bool Level::LoadFromString(const std::string& text, std::string* error) {
    tiles_.clear();
    portals_.clear();
    name_ = "Untitled";
    std::vector<std::string> grid;

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = Trim(line);
        if (t.rfind("//", 0) == 0) continue;                 // комментарий
        if (!t.empty() && t[0] == '@') {                     // метаданные
            size_t sp = t.find_first_of(" \t");
            std::string key = t.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
            std::string val = sp == std::string::npos ? "" : Trim(t.substr(sp));
            if (key == "name") name_ = val;
            else if (key == "bg") ParseColor(val, &bgColor_);
            else if (key == "ground") ParseColor(val, &groundColor_);
            continue;
        }
        if (t.empty() && grid.empty()) continue;             // пустые строки до сетки
        grid.push_back(line);
    }
    while (!grid.empty() && Trim(grid.back()).empty()) grid.pop_back();

    if (grid.empty()) {
        if (error) *error = "level has no grid";
        return false;
    }

    rows_ = (int)grid.size();
    cols_ = 0;
    for (auto& g : grid) cols_ = std::max(cols_, (int)g.size());
    tiles_.assign((size_t)rows_ * cols_, Tile::Empty);

    auto portalTypeOf = [](char ch, PortalType* out) {
        switch (ch) {
            case 'C': *out = PortalType::Cube; return true;
            case 'B': *out = PortalType::Ball; return true;
            case 'S': *out = PortalType::Ship; return true;
            case 'G': *out = PortalType::GravityFlip; return true;
            case 'N': *out = PortalType::GravityNormal; return true;
            default: return false;
        }
    };
    auto charAt = [&](int r, int c) { return c < (int)grid[r].size() ? grid[r][c] : ' '; };

    int finishCol = -1;
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < (int)grid[r].size(); ++c) {
            char ch = grid[r][c];
            Tile& t = tiles_[(size_t)r * cols_ + c];
            if (ch == '#') t = Tile::Block;
            else if (ch == '^') t = Tile::SpikeUp;
            else if (ch == 'v') t = Tile::SpikeDown;
            else if (ch == 'E' && (finishCol < 0 || c < finishCol)) finishCol = c;
        }
    }

    // Порталы: вертикальный ряд одинаковых букв сливается в один высокий портал.
    // Портал выступает на (PORTAL_HEIGHT - TILE)/2 выше и ниже своих клеток,
    // поэтому одиночная буква даёт портал высотой 3 клетки.
    const float ext = (cfg::PORTAL_HEIGHT - cfg::TILE) * 0.5f;
    for (int c = 0; c < cols_; ++c) {
        for (int r = 0; r < rows_; ++r) {
            PortalType pt;
            char ch = charAt(r, c);
            if (!portalTypeOf(ch, &pt)) continue;
            if (r > 0 && charAt(r - 1, c) == ch) continue;       // не начало ряда
            int r1 = r;
            while (r1 + 1 < rows_ && charAt(r1 + 1, c) == ch) ++r1;
            Portal p;
            p.type = pt;
            p.col = c;
            p.row = r;
            float cx = c * cfg::TILE + cfg::TILE * 0.5f;
            float top = r * cfg::TILE - ext;
            float bottom = (r1 + 1) * cfg::TILE + ext;
            p.rect = {cx - cfg::PORTAL_WIDTH * 0.5f, top, cfg::PORTAL_WIDTH, bottom - top};
            portals_.push_back(p);
        }
    }
    std::sort(portals_.begin(), portals_.end(),
              [](const Portal& a, const Portal& b) { return a.rect.x < b.rect.x; });
    finishX_ = (finishCol >= 0 ? finishCol : cols_) * cfg::TILE;
    if (error) error->clear();
    return true;
}

Tile Level::At(int col, int row) const {
    if (col < 0 || row < 0 || col >= cols_ || row >= rows_) return Tile::Empty;
    return tiles_[(size_t)row * cols_ + col];
}

float Level::FloorY() const { return rows_ * cfg::TILE; }
float Level::CeilingY() const { return FloorY() - cfg::CORRIDOR_TILES * cfg::TILE; }

float Level::Progress(float x) const {
    if (finishX_ <= 0) return 0;
    return std::clamp(x / finishX_, 0.0f, 1.0f);
}

// ------------------------------------------------------------------ отрисовка

static void DrawSpike(float x, float y, bool up, Color fill, Color edge) {
    float T = cfg::TILE;
    Vector2 a, b, c;
    if (up) {
        a = {x + T * 0.5f, y + T * 0.08f};
        b = {x + T * 0.06f, y + T};
        c = {x + T * 0.94f, y + T};
    } else {
        a = {x + T * 0.5f, y + T * 0.92f};
        b = {x + T * 0.94f, y};
        c = {x + T * 0.06f, y};
    }
    DrawTriangle(a, b, c, fill);
    DrawLineEx(a, b, 2.5f, edge);
    DrawLineEx(b, c, 2.5f, edge);
    DrawLineEx(c, a, 2.5f, edge);
    // внутренняя "грань" для объёма
    Vector2 m = {(b.x + c.x) * 0.5f, (b.y + c.y) * 0.5f};
    Vector2 inner = {a.x + (m.x - a.x) * 0.45f, a.y + (m.y - a.y) * 0.45f};
    DrawLineEx(inner, m, 1.5f, Fade(edge, 0.35f));
}

static void DrawPortal(const Portal& p, float time) {
    Color col = PortalColor(p.type);
    Vector2 c = {p.rect.x + p.rect.width * 0.5f, p.rect.y + p.rect.height * 0.5f};
    float rx = p.rect.width * 0.5f, ry = p.rect.height * 0.5f;

    // свечение
    for (int i = 3; i >= 1; --i)
        DrawEllipse((int)c.x, (int)c.y, rx + i * 5, ry + i * 5, Fade(col, 0.06f * i));
    DrawEllipse((int)c.x, (int)c.y, rx, ry, Fade(col, 0.18f));

    // кольцо из нескольких эллипсов
    for (int i = 0; i < 4; ++i)
        DrawEllipseLines((int)c.x, (int)c.y, rx - i, ry - i, i < 2 ? col : Fade(WHITE, 0.6f));

    // бегущие искры
    for (int i = 0; i < 6; ++i) {
        float a = time * 2.4f + i * (2.0f * PI / 6.0f);
        Vector2 s = {c.x + cosf(a) * rx * 0.8f, c.y + sinf(a) * ry * 0.8f};
        DrawCircleV(s, 2.5f, Fade(WHITE, 0.8f));
    }

    // символ режима в центре
    float k = 9.0f;
    switch (p.type) {
        case PortalType::Cube:
            DrawRectangleLinesEx({c.x - k, c.y - k, 2 * k, 2 * k}, 3, WHITE);
            break;
        case PortalType::Ball:
            DrawRing(c, k - 3, k, 0, 360, 24, WHITE);
            break;
        case PortalType::Ship:
            DrawTriangle({c.x + k, c.y}, {c.x - k, c.y - k * 0.8f}, {c.x - k, c.y + k * 0.8f}, WHITE);
            break;
        case PortalType::GravityFlip:
            DrawTriangle({c.x, c.y - k * 1.6f}, {c.x - k * 0.7f, c.y - k * 0.4f}, {c.x + k * 0.7f, c.y - k * 0.4f}, WHITE);
            DrawTriangle({c.x, c.y + k * 1.6f}, {c.x + k * 0.7f, c.y + k * 0.4f}, {c.x - k * 0.7f, c.y + k * 0.4f}, WHITE);
            break;
        case PortalType::GravityNormal:
            DrawTriangle({c.x, c.y + k * 1.4f}, {c.x + k * 0.8f, c.y}, {c.x - k * 0.8f, c.y}, WHITE);
            DrawRectangleRec({c.x - 3, c.y - k * 1.2f, 6, k * 1.3f}, WHITE);
            break;
    }
}

void Level::Draw(float viewLeft, float viewRight, float time) const {
    const float T = cfg::TILE;
    int c0 = std::max(0, (int)std::floor(viewLeft / T) - 1);
    int c1 = std::min(cols_ - 1, (int)std::ceil(viewRight / T) + 1);

    Color fill  = Darken(groundColor_, 0.55f);
    Color inner = Brighten(groundColor_, 0.15f);
    Color edge  = Brighten(groundColor_, 0.75f);
    Color spikeFill = {12, 12, 20, 255};

    // блоки: заливка + внутренний узор
    for (int r = 0; r < rows_; ++r) {
        for (int c = c0; c <= c1; ++c) {
            if (At(c, r) != Tile::Block) continue;
            float x = c * T, y = r * T;
            DrawRectangleRec({x, y, T, T}, fill);
            DrawRectangleLinesEx({x + 7, y + 7, T - 14, T - 14}, 2, Fade(inner, 0.45f));
            if ((c + r) % 3 == 0) DrawRectangleRec({x + 16, y + 16, T - 32, T - 32}, Fade(inner, 0.5f));
        }
    }
    // блоки: светлые контуры только по открытым граням -> группы выглядят цельными
    for (int r = 0; r < rows_; ++r) {
        for (int c = c0; c <= c1; ++c) {
            if (At(c, r) != Tile::Block) continue;
            float x = c * T, y = r * T;
            const float w = 3.0f;
            if (At(c, r - 1) != Tile::Block) DrawRectangleRec({x, y, T, w}, edge);
            if (At(c, r + 1) != Tile::Block) DrawRectangleRec({x, y + T - w, T, w}, edge);
            if (At(c - 1, r) != Tile::Block) DrawRectangleRec({x, y, w, T}, edge);
            if (At(c + 1, r) != Tile::Block) DrawRectangleRec({x + T - w, y, w, T}, edge);
        }
    }
    // шипы
    for (int r = 0; r < rows_; ++r) {
        for (int c = c0; c <= c1; ++c) {
            Tile t = At(c, r);
            if (t == Tile::SpikeUp || t == Tile::SpikeDown)
                DrawSpike(c * T, r * T, t == Tile::SpikeUp, spikeFill, edge);
        }
    }
    // порталы
    for (const Portal& p : portals_) {
        if (p.rect.x + p.rect.width < viewLeft - T || p.rect.x > viewRight + T) continue;
        DrawPortal(p, time);
    }
    // финишная линия
    if (finishX_ > viewLeft - 100 && finishX_ < viewRight + 100) {
        float top = FloorY() - T * 16;
        float h = FloorY() - top;
        for (int i = 0; i < 6; ++i)
            DrawRectangleRec({finishX_ + i * 6.0f, top, 6, h}, Fade(WHITE, 0.25f - i * 0.04f));
        int n = (int)(h / 20);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < 2; ++j) {
                Color cc = ((i + j) % 2) ? WHITE : BLACK;
                DrawRectangleRec({finishX_ - 20 + j * 10.0f, top + i * 20.0f, 10, 20}, Fade(cc, 0.8f));
            }
        }
        float pulse = 0.5f + 0.5f * sinf(time * 4.0f);
        DrawRectangleRec({finishX_ - 22, top, 3, h}, Fade(YELLOW, 0.4f + 0.4f * pulse));
    }
}
