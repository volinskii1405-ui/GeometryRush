#include "Map.h"
#include "Aircraft.h"
#include "Autopilot.h"
#include "Terrain.h"
#include "UiText.h"
#include "World.h"

#include <string>

using namespace fs;
using ui::L;

namespace {

float Sc() { return fmaxf(fminf(GetScreenHeight() / 900.0f, GetScreenWidth() / 1500.0f), 0.5f); }

const Color kMagenta{230, 80, 230, 255}, kCyanM{80, 220, 240, 255}, kDim{170, 176, 186, 255};

// Короткое имя аэродрома для кнопок и точек маршрута.
const char* ShortName(int i, bool ascii)
{
    bool main = world::GetAirport(i).main;
    if (ascii) return main ? "ISLAND" : "PASS";
    return main ? L("Island", "Остров") : L("Pass", "Перевал");
}

// id: 100+2·аэродром+торец — заход, 1 — очистить, 2 — NAV, 3 — VNAV,
// 10..13 — высота выбранной точки −1000/−100/+100/+1000, 14 — без высоты, 15 — удалить точку.
struct Btn { Rectangle r; std::string label; int id; };

std::vector<Btn> Buttons(Rectangle area, bool hasSel)
{
    const float S = Sc();
    const float x = area.x + area.width + 20 * S, w = fminf(GetScreenWidth() - x - 20 * S, 300 * S);
    float y = area.y + 64 * S;
    std::vector<Btn> b;
    for (int i = 0; i < world::AirportCount(); ++i) {
        const Airport& ap = world::GetAirport(i);
        for (int end = 0; end < 2; ++end) {
            if (!ap.main && end == 1) continue;   // на «Перевал» — только с юга на север: с другой стороны хребет
            b.push_back({{x, y, w, 30 * S}, TextFormat("%s %s %s", L("Approach", "Заход"), ShortName(i, false), ap.rwy.ident[end]),
                         100 + 2 * i + end});
            y += 36 * S;
        }
    }
    y += 8 * S;
    const float w3 = (w - 12 * S) / 3;
    b.push_back({{x, y, w3, 30 * S}, "NAV (F2)", 2});
    b.push_back({{x + w3 + 6 * S, y, w3, 30 * S}, "VNAV (F3)", 3});
    b.push_back({{x + 2 * (w3 + 6 * S), y, w3, 30 * S}, L("Clear all", "Очистить"), 1});
    if (hasSel) {
        y += 36 * S + 30 * S;   // место под подпись выбранной точки
        const float w4 = (w - 18 * S) / 4;
        const char* alt[4] = {"-1000", "-100", "+100", "+1000"};
        for (int i = 0; i < 4; ++i) b.push_back({{x + i * (w4 + 6 * S), y, w4, 30 * S}, alt[i], 10 + i});
        y += 36 * S;
        b.push_back({{x, y, w * 0.48f, 30 * S}, L("No altitude", "Без высоты"), 14});
        b.push_back({{x + w * 0.52f, y, w * 0.48f, 30 * S}, L("Delete (Del)", "Удалить (Del)"), 15});
    }
    return b;
}

} // namespace

void NavMap::Build(const Terrain& t)
{
    half_ = t.HalfExtent();
    const int n = 512;
    Image img = GenImageColor(n, n, BLACK);
    auto H = [&](int i, int j) {
        float x = -half_ + (i + 0.5f) / n * 2.0f * half_, z = -half_ + (j + 0.5f) / n * 2.0f * half_;
        return t.GroundHeight(x, z);
    };
    const float cellM = 2.0f * half_ / n;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            float h = H(i, j);
            float x = -half_ + (i + 0.5f) / n * 2.0f * half_, z = -half_ + (j + 0.5f) / n * 2.0f * half_;
            Color c;
            if (h < 0.0f) {
                float k = Clampf(1.0f + h / 80.0f, 0.0f, 1.0f);   // мелководье светлее
                c = Color{(unsigned char)Lerp(18, 70, k), (unsigned char)Lerp(52, 130, k), (unsigned char)Lerp(98, 168, k), 255};
            } else {
                Vector3 col;
                if (h < 6) col = {196, 186, 140};
                else if (h < 350) col = Vector3Lerp({96, 136, 74}, {128, 146, 84}, h / 350.0f);
                else if (h < 750) col = Vector3Lerp({128, 146, 84}, {140, 120, 92}, (h - 350) / 400.0f);
                else if (h < 1050) col = Vector3Lerp({140, 120, 92}, {128, 124, 120}, (h - 750) / 300.0f);
                else col = {232, 234, 240};
                col = Vector3Scale(col, 1.0f - 0.22f * t.ForestAmount(x, z));
                // Отмывка рельефа: свет с северо-запада.
                float dx = (H(std::min(i + 1, n - 1), j) - H(std::max(i - 1, 0), j)) / (2 * cellM);
                float dz = (H(i, std::min(j + 1, n - 1)) - H(i, std::max(j - 1, 0))) / (2 * cellM);
                Vector3 nrm = Vector3Normalize({-dx, 1.0f, -dz});
                float shade = Clampf(0.62f + 0.55f * Vector3DotProduct(nrm, Vector3Normalize({-0.6f, 0.7f, -0.6f})), 0.45f, 1.25f);
                col = Vector3Scale(col, shade);
                c = Color{(unsigned char)Clampf(col.x, 0, 255), (unsigned char)Clampf(col.y, 0, 255), (unsigned char)Clampf(col.z, 0, 255), 255};
            }
            ImageDrawPixel(&img, i, j, c);
        }
    tex_ = LoadTextureFromImage(img);
    SetTextureFilter(tex_, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
}

void NavMap::Unload()
{
    if (tex_.id) UnloadTexture(tex_);
    tex_ = {};
}

Rectangle NavMap::Area() const
{
    const float S = Sc();
    const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    float side = fminf(sh - 60 * S, sw - 360 * S);
    return {30 * S, (sh - side) * 0.5f, side, side};
}

Vector2 NavMap::ToScreen(float x, float z) const
{
    Rectangle a = Area();
    float ppm = a.width / (2.0f * half_) * zoom_;
    return {a.x + a.width * 0.5f + (x - center_.x) * ppm, a.y + a.height * 0.5f + (z - center_.y) * ppm};
}

Vector2 NavMap::ToWorld(Vector2 p) const
{
    Rectangle a = Area();
    float ppm = a.width / (2.0f * half_) * zoom_;
    return {center_.x + (p.x - a.x - a.width * 0.5f) / ppm, center_.y + (p.y - a.y - a.height * 0.5f) / ppm};
}

// Готовый заход: начальная точка (IF), точка начала снижения по глиссаде (FF) и порог ВПП
// с высотами по глиссаде (3° на «Остров», 3.5° на «Перевал»).
void NavMap::AddApproach(Autopilot& ap, int airport, int end) const
{
    const Airport& port = world::GetAirport(airport);
    const Runway& rw = port.rwy;
    const float glide = (port.main ? 3.0f : 3.5f) * DEG2RAD, tdz = port.main ? 300.0f : 150.0f;
    const float dists[2] = {port.main ? 18520.0f : 8000.0f, port.main ? 11112.0f : 4000.0f};
    const char* tags[2] = {"IF", "FF"};
    Vector3 d = rw.Dir(end), thr = rw.Threshold(end);
    for (int k = 0; k < 2; ++k) {
        Vector3 p = Vector3Subtract(thr, Vector3Scale(d, dists[k]));
        float alt = roundf(((rw.center.y + (dists[k] + tdz) * tanf(glide)) * M_TO_FT) / 100.0f) * 100.0f;
        ap.AddWaypoint(p, TextFormat("%s%s", tags[k], rw.ident[end]), alt);
    }
    ap.AddWaypoint(thr, TextFormat("RW%s", rw.ident[end]), roundf(rw.center.y * M_TO_FT + 50.0f));
}

int NavMap::HitWaypoint(const Autopilot& ap, Vector2 m) const
{
    int best = -1;
    float bestD = 13.0f * Sc();
    for (int i = 0; i < (int)ap.route.size(); ++i) {
        float d = Vector2Distance(m, ToScreen(ap.route[i].pos.x, ap.route[i].pos.z));
        if (d < bestD) best = i, bestD = d;
    }
    return best;
}

void NavMap::Select(const Autopilot& ap, int i) { sel_ = (i >= 0 && i < (int)ap.route.size()) ? i : -1; }

void NavMap::Update(Autopilot& ap, const Aircraft& a)
{
    if (!open) {
        dragging_ = false;
        return;
    }
    if (sel_ >= (int)ap.route.size()) sel_ = -1;
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) zoom_ = Clampf(zoom_ * powf(1.25f, wheel), 1.0f, 10.0f);
    // При увеличении карта следит за самолётом.
    if (zoom_ <= 1.01f) center_ = {0, 0};
    else {
        float lim = half_ * (1.0f - 1.0f / zoom_);
        center_ = {Clampf(a.pos.x, -lim, lim), Clampf(a.pos.z, -lim, lim)};
    }
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    auto changeAlt = [&](float delta) {
        if (sel_ < 0) return;
        Waypoint& w = ap.route[sel_];
        float base = w.altFt > 0 ? w.altFt : roundf(a.pos.y * M_TO_FT / 100.0f) * 100.0f;
        w.altFt = Clampf(base + delta, 500.0f, 30000.0f);
    };
    auto removeSel = [&]() {
        if (sel_ >= 0) {
            ap.RemoveWaypoint(sel_);
            sel_ = sel_ < (int)ap.route.size() ? sel_ : (int)ap.route.size() - 1;
        } else {
            ap.RemoveLastWaypoint();
        }
    };
    // Клавиши: −/= — высота выбранной точки (с Shift — по 1000), Del — удалить, Shift+Del — всё.
    auto step = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
    if (sel_ >= 0 && (step(KEY_EQUAL) || step(KEY_KP_ADD))) changeAlt(shift ? 1000.0f : 100.0f);
    if (sel_ >= 0 && (step(KEY_MINUS) || step(KEY_KP_SUBTRACT))) changeAlt(shift ? -1000.0f : -100.0f);
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) {
        if (shift) {
            ap.ClearRoute();
            userWp_ = 0;
            sel_ = -1;
        } else {
            removeSel();
        }
    }
    const Rectangle area = Area();
    const Vector2 m = GetMousePosition();
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) dragging_ = false;
    if (dragging_ && sel_ >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        // Перетаскивание точки.
        Vector2 w = ToWorld({Clampf(m.x, area.x, area.x + area.width), Clampf(m.y, area.y, area.y + area.height)});
        ap.route[sel_].pos.x = w.x;
        ap.route[sel_].pos.z = w.y;
        if (sel_ == ap.activeWp) ap.RouteEdited();
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        for (const Btn& b : Buttons(area, sel_ >= 0)) {
            if (!CheckCollisionPointRec(m, b.r)) continue;
            if (b.id == 1) { ap.ClearRoute(); userWp_ = 0; sel_ = -1; }
            else if (b.id == 2) ap.ToggleNav(a);
            else if (b.id == 3) ap.vnavOn = !ap.vnavOn;
            else if (b.id >= 10 && b.id <= 13) changeAlt(b.id == 10 ? -1000.0f : b.id == 11 ? -100.0f : b.id == 12 ? 100.0f : 1000.0f);
            else if (b.id == 14 && sel_ >= 0) ap.route[sel_].altFt = 0;
            else if (b.id == 15) removeSel();
            else if (b.id >= 100) AddApproach(ap, (b.id - 100) / 2, (b.id - 100) % 2);
            return;
        }
        for (int i = 0; i < (int)rowRects_.size() && i < (int)ap.route.size(); ++i)
            if (CheckCollisionPointRec(m, rowRects_[i])) {
                Select(ap, i);
                return;
            }
        if (CheckCollisionPointRec(m, area)) {
            int hit = HitWaypoint(ap, m);
            if (hit >= 0) {   // взять точку: выбрать и тащить
                Select(ap, hit);
                dragging_ = true;
                return;
            }
            // Новая точка берёт высоту предыдущей (можно поменять или убрать).
            float alt = 0;
            for (int i = (int)ap.route.size() - 1; i >= 0 && alt <= 0; --i) alt = ap.route[i].altFt;
            // Щелчок рядом с аэродромом — точка на нём, иначе — точка WPn.
            for (int i = 0; i < world::AirportCount(); ++i) {
                const Runway& rw = world::GetAirport(i).rwy;
                if (Vector2Distance(m, ToScreen(rw.center.x, rw.center.z)) < 14.0f * Sc()) {
                    ap.AddWaypoint({rw.center.x, rw.center.y, rw.center.z}, ShortName(i, true), 0);
                    Select(ap, (int)ap.route.size() - 1);
                    return;
                }
            }
            Vector2 w = ToWorld(m);
            ap.AddWaypoint({w.x, 0, w.y}, TextFormat("WP%d", ++userWp_), alt);
            Select(ap, (int)ap.route.size() - 1);
        }
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && CheckCollisionPointRec(m, area)) {
        int hit = HitWaypoint(ap, m);
        if (hit >= 0) {
            ap.RemoveWaypoint(hit);
            if (sel_ == hit) sel_ = -1;
            else if (sel_ > hit) --sel_;
        } else {
            ap.RemoveLastWaypoint();
            if (sel_ >= (int)ap.route.size()) sel_ = -1;
        }
    }
}

void NavMap::Draw(const Aircraft& a, const Autopilot& ap, Vector3 wind, const std::vector<Vector2>& trail) const
{
    if (!open) return;
    const float S = Sc();
    const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    const Rectangle area = Area();
    const float ppm = area.width / (2.0f * half_) * zoom_;
    DrawRectangle(0, 0, (int)sw, (int)sh, Color{8, 10, 14, 215});
    DrawRectangleRec(area, Color{18, 52, 98, 255});

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    Vector2 p0 = ToScreen(-half_, -half_), p1 = ToScreen(half_, half_);
    DrawTexturePro(tex_, {0, 0, (float)tex_.width, (float)tex_.height}, {p0.x, p0.y, p1.x - p0.x, p1.y - p0.y}, {0, 0}, 0, WHITE);
    // Сетка через 5 км.
    for (float g = -half_; g <= half_; g += 5000.0f) {
        Vector2 v0 = ToScreen(g, -half_), v1 = ToScreen(g, half_), h0 = ToScreen(-half_, g), h1 = ToScreen(half_, g);
        DrawLineEx(v0, v1, 1.0f, Color{255, 255, 255, 22});
        DrawLineEx(h0, h1, 1.0f, Color{255, 255, 255, 22});
    }
    // Аэродромы: ВПП, курс ILS (10 NM), подпись.
    for (int i = 0; i < world::AirportCount(); ++i) {
        const Airport& port = world::GetAirport(i);
        const Runway& rw = port.rwy;
        for (int end = 0; end < 2; ++end) {
            if (!rw.ils) continue;
            Vector3 thr = rw.Threshold(end), far = Vector3Subtract(thr, Vector3Scale(rw.Dir(end), 18520.0f));
            Vector2 s0 = ToScreen(thr.x, thr.z), s1 = ToScreen(far.x, far.z);
            DrawLineEx(s0, s1, 1.5f, Color{80, 220, 240, 110});
            ui::Draw(TextFormat("ILS %s", rw.ident[end]), s1.x + 4, s1.y - 7 * S, 12 * S, kCyanM);
        }
        Vector3 t0 = rw.Threshold(0), t1 = rw.Threshold(1);
        DrawLineEx(ToScreen(t0.x, t0.z), ToScreen(t1.x, t1.z), fmaxf(4.0f * S, 2.0f * rw.halfWidth * ppm), Color{235, 235, 235, 255});
        Vector2 c = ToScreen(rw.center.x, rw.center.z);
        ui::Draw(L(port.nameEn, port.nameRu), c.x + 10 * S, c.y + 8 * S, 15 * S, WHITE, true);
        ui::Draw(TextFormat("%s/%s  %d ft", rw.ident[0], rw.ident[1], (int)(rw.center.y * M_TO_FT)), c.x + 10 * S, c.y + 26 * S, 12 * S,
                 kDim);
    }
    // Пройденный путь.
    for (const Vector2& p : trail) DrawCircleV(ToScreen(p.x, p.y), 1.6f * S, Color{255, 220, 90, 200});
    // Маршрут: пройденные участки серые, активный и следующие — пурпурные.
    Vector2 ac = ToScreen(a.pos.x, a.pos.z);
    for (int i = 0; i < (int)ap.route.size(); ++i) {
        const Waypoint& w = ap.route[i];
        Vector2 p = ToScreen(w.pos.x, w.pos.z);
        bool done = i < ap.activeWp;
        if (i > 0) {
            Vector2 q = ToScreen(ap.route[i - 1].pos.x, ap.route[i - 1].pos.z);
            DrawLineEx(q, p, 2.0f * S, done ? Color{150, 150, 150, 160} : kMagenta);
        }
        if (i == ap.activeWp) {
            // Линия «прямо на точку» от самолёта.
            for (float k = 0; k < 1.0f; k += 0.06f) DrawLineEx(Vector2Lerp(ac, p, k), Vector2Lerp(ac, p, k + 0.03f), 1.5f * S, kMagenta);
        }
        Color wc = done ? Color{150, 150, 150, 255} : (i == ap.activeWp ? WHITE : kMagenta);
        if (i == sel_) DrawCircleLines((int)p.x, (int)p.y, 12 * S, Color{255, 230, 90, 255});
        DrawPoly(p, 4, 7 * S, 45, wc);
        DrawPoly(p, 4, 4 * S, 45, Color{30, 20, 40, 255});
        ui::Draw(w.name, p.x + 9 * S, p.y - 16 * S, 13 * S, wc, true);
        if (w.altFt > 0) ui::Draw(TextFormat("%d ft", (int)w.altFt), p.x + 9 * S, p.y, 11 * S, wc);
    }
    // Самолёт: треугольник по курсу.
    {
        float h = a.HeadingDeg() * DEG2RAD;
        Vector2 f{sinf(h), -cosf(h)}, r{cosf(h), sinf(h)};
        float L1 = 13 * S, w1 = 8 * S;
        Vector2 tip{ac.x + f.x * L1, ac.y + f.y * L1};
        Vector2 bl{ac.x - f.x * L1 * 0.6f - r.x * w1, ac.y - f.y * L1 * 0.6f - r.y * w1};
        Vector2 br{ac.x - f.x * L1 * 0.6f + r.x * w1, ac.y - f.y * L1 * 0.6f + r.y * w1};
        DrawTriangle(tip, bl, br, Color{255, 230, 60, 255});
        DrawTriangle(tip, br, bl, Color{255, 230, 60, 255});
        DrawTriangleLines(tip, bl, br, BLACK);
    }
    EndScissorMode();
    DrawRectangleLinesEx(area, 2.0f, Color{90, 96, 108, 255});

    // Север и ветер.
    float nx = area.x + 26 * S, ny = area.y + 30 * S;
    DrawTriangle({nx, ny - 16 * S}, {nx - 7 * S, ny + 4 * S}, {nx + 7 * S, ny + 4 * S}, WHITE);
    ui::DrawCentered("N", nx, ny + 6 * S, 14 * S, WHITE, true);
    float ws = sqrtf(wind.x * wind.x + wind.z * wind.z);
    if (ws > 0.5f) {
        Vector2 wc{area.x + 80 * S, area.y + 30 * S};
        Vector2 dir{wind.x / ws, wind.z / ws};   // куда дует
        Vector2 tail{wc.x - dir.x * 16 * S, wc.y - dir.y * 16 * S}, head{wc.x + dir.x * 16 * S, wc.y + dir.y * 16 * S};
        DrawLineEx(tail, head, 3 * S, kCyanM);
        DrawCircleV(head, 4 * S, kCyanM);
        float from = WrapDeg360(atan2f(-wind.x, wind.z) * RAD2DEG);
        ui::Draw(TextFormat("%03d/%d kt", (int)roundf(from) % 360, (int)roundf(ws * MS_TO_KT)), wc.x + 22 * S, wc.y - 8 * S, 13 * S, kCyanM);
    }
    // Масштабная линейка: 5 км (при сильном увеличении — 1 км).
    {
        float km = zoom_ > 3.0f ? 1.0f : 5.0f;
        float len = km * 1000.0f * ppm;
        float x = area.x + 20 * S, y = area.y + area.height - 24 * S;
        DrawLineEx({x, y}, {x + len, y}, 3 * S, WHITE);
        DrawLineEx({x, y - 5 * S}, {x, y + 5 * S}, 2 * S, WHITE);
        DrawLineEx({x + len, y - 5 * S}, {x + len, y + 5 * S}, 2 * S, WHITE);
        ui::Draw(TextFormat(L("%d km", "%d км"), (int)km), x + len + 8 * S, y - 8 * S, 13 * S, WHITE);
    }

    // Боковая панель: кнопки заходов, список точек, подсказки.
    const float px = area.x + area.width + 20 * S, pw = fminf(sw - px - 20 * S, 300 * S);
    ui::Draw(L("MAP · ROUTE (FMS)", "КАРТА · МАРШРУТ"), px, area.y + 4 * S, 22 * S, WHITE, true);
    ui::Draw(L("Ready approaches:", "Готовые заходы:"), px, area.y + 40 * S, 13 * S, kDim);
    const Vector2 m = GetMousePosition();
    float by = area.y;
    const int sel = sel_ < (int)ap.route.size() ? sel_ : -1;
    const std::vector<Btn> btns = Buttons(area, sel >= 0);
    for (const Btn& b : btns) {
        if (b.id == 10) {   // подпись редактора выбранной точки
            const Waypoint& w = ap.route[sel];
            std::string altS = w.altFt > 0 ? std::string(TextFormat("%d ft", (int)w.altFt)) : std::string(L("no altitude", "без высоты"));
            ui::Draw(TextFormat(L("Point %d %s: %s", "Точка %d %s: %s"), sel + 1, w.name, altS.c_str()), b.r.x, b.r.y - 26 * S, 16 * S,
                     Color{255, 230, 90, 255}, true);
        }
        bool hot = CheckCollisionPointRec(m, b.r);
        bool on = (b.id == 2 && ap.navOn) || (b.id == 3 && ap.vnavOn);
        DrawRectangleRec(b.r, on ? Color{30, 90, 50, 255} : (hot ? Color{60, 66, 80, 255} : Color{36, 40, 50, 255}));
        DrawRectangleLinesEx(b.r, 1.0f, Color{90, 96, 108, 255});
        ui::DrawCentered(b.label.c_str(), b.r.x + b.r.width * 0.5f, b.r.y + b.r.height * 0.5f - 8 * S, 14 * S, WHITE);
        by = b.r.y + b.r.height;
    }
    float y = by + 18 * S;
    float gs = sqrtf(a.vel.x * a.vel.x + a.vel.z * a.vel.z);
    if (ap.route.empty()) {
        rowRects_.clear();
        ui::Draw(L("Route is empty", "Маршрут пуст"), px, y, 15 * S, kDim);
        y += 24 * S;
    } else {
        float total = 0;
        Vector3 prev = a.pos;
        rowRects_.clear();
        for (int i = 0; i < (int)ap.route.size() && y < area.y + area.height - 150 * S; ++i) {
            const Waypoint& w = ap.route[i];
            bool done = i < ap.activeWp;
            float d = 0;
            if (!done) {
                d = Vector2Distance({prev.x, prev.z}, {w.pos.x, w.pos.z});
                total += d;
                prev = w.pos;
            }
            Color c = done ? Color{120, 120, 120, 255} : (i == ap.activeWp ? WHITE : kMagenta);
            rowRects_.push_back({px - 4 * S, y - 2 * S, pw + 8 * S, 21 * S});
            if (i == sel) DrawRectangleRec(rowRects_.back(), Color{255, 230, 90, 40});
            ui::Draw(TextFormat("%s%d %s", i == ap.activeWp ? "> " : "  ", i + 1, w.name), px, y, 15 * S, c, i == ap.activeWp);
            if (!done) ui::DrawRight(TextFormat("%.1f NM", total / 1852.0f), px + pw * 0.72f, y, 14 * S, c);
            if (w.altFt > 0) ui::DrawRight(TextFormat("%d", (int)w.altFt), px + pw, y, 14 * S, c);
            y += 21 * S;
        }
        if (gs > 10.0f && ap.RouteActive()) {
            int ete = (int)(total / gs);
            ui::Draw(TextFormat(L("Total %.1f NM, %d:%02d at current speed", "Всего %.1f NM, %d:%02d на этой скорости"), total / 1852.0f,
                                ete / 60, ete % 60),
                     px, y + 4 * S, 13 * S, kDim);
            y += 22 * S;
        }
    }
    ui::Draw(ap.navOn ? L("NAV: autopilot follows the route", "NAV: автопилот ведёт по маршруту")
                      : L("NAV off: F2 to fly the route", "NAV выключен: F2 — лететь по маршруту"),
             px, y + 6 * S, 14 * S, ap.navOn ? Color{80, 230, 120, 255} : Color{230, 170, 40, 255});
    ui::Draw(ap.vnavOn ? L("VNAV: altitudes of the points are flown", "VNAV: высоты точек выдерживаются")
                       : L("VNAV off: altitude from the AP selector (-/=)", "VNAV выключен: высота — задатчик автопилота (-/=)"),
             px, y + 26 * S, 14 * S, ap.vnavOn ? Color{80, 230, 120, 255} : Color{170, 176, 186, 255});
    const char* help[] = {L("LMB - new point / pick a point and drag it", "ЛКМ — новая точка / взять точку и перетащить"),
                          L("-/= altitude of the point (Shift x1000)", "-/= — высота выбранной точки (с Shift — по 1000)"),
                          L("RMB on a point / Del - delete, Shift+Del - all", "ПКМ по точке / Del — удалить, Shift+Del — всё"),
                          L("Wheel - zoom   N / Esc - close", "Колесо — масштаб   N / Esc — закрыть"),
                          L("T - AP, F2 - NAV, F3 - VNAV, L - ILS", "T — автопилот, F2 — NAV, F3 — VNAV, L — ILS")};
    float hy = area.y + area.height - 5 * 19 * S;
    for (const char* h : help) {
        ui::Draw(h, px, hy, 13 * S, kDim);
        hy += 19 * S;
    }
}
