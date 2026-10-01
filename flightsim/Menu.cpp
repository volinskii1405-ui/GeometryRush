#include "Menu.h"
#include "Aircraft.h"
#include "UiText.h"
#include "World.h"

#include <string>
#include <vector>

using ui::L;

namespace {

float S = 1.0f;
const Color kPanel{12, 16, 24, 225};
const Color kAccent{90, 170, 240, 255};
const Color kText{226, 230, 236, 255};
const Color kDim{150, 156, 166, 255};
const Color kGold{255, 205, 70, 255};

// Кнопка: подсветка при наведении/выборе, возвращает true при нажатии мышью.
bool Button(Rectangle r, const char* text, bool selected, float size = 22)
{
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    Color bg = selected || hover ? Color{40, 80, 130, 230} : Color{24, 30, 42, 220};
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1.5f, selected ? kAccent : Color{60, 70, 90, 255});
    ui::DrawCentered(text, r.x + r.width * 0.5f, r.y + (r.height - size * S) * 0.5f, size * S, kText);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// Строка выбора «название: ‹ значение ›». Возвращает -1/+1 при смене значения.
int OptionRow(Rectangle r, const char* label, const char* value, bool selected)
{
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    DrawRectangleRec(r, selected || hover ? Color{34, 56, 90, 230} : Color{22, 28, 40, 220});
    DrawRectangleLinesEx(r, 1.0f, selected ? kAccent : Color{50, 60, 80, 255});
    ui::Draw(label, r.x + 14 * S, r.y + (r.height - 19 * S) * 0.5f, 19 * S, kDim);
    float vx = r.x + r.width * 0.62f;
    ui::DrawCentered(TextFormat("‹  %s  ›", value), vx, r.y + (r.height - 19 * S) * 0.5f, 19 * S, kText);
    int delta = 0;
    if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) delta = m.x < vx ? -1 : 1;
    if (selected) {
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) delta = -1;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) delta = 1;
    }
    return delta;
}

// Перенос текста по словам в пределах ширины.
float Paragraph(const char* text, float x, float y, float w, float size, Color c)
{
    std::string line, word;
    std::string s = text;
    float lh = size * 1.3f;
    auto flush = [&]() {
        ui::Draw(line.c_str(), x, y, size, c);
        y += lh;
        line.clear();
    };
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == ' ') {
            std::string test = line.empty() ? word : line + " " + word;
            if (ui::Measure(test.c_str(), size) > w && !line.empty()) {
                flush();
                line = word;
            } else {
                line = test;
            }
            word.clear();
        } else {
            word += s[i];
        }
    }
    if (!line.empty()) flush();
    return y;
}

std::string Stars(int n)
{
    std::string s;
    for (int i = 0; i < 3; ++i) s += i < n ? "★" : "☆";
    return s;
}

void Dim() { DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 120}); }

// Общая навигация по списку: вверх/вниз, Enter.
void Nav(int& sel, int count)
{
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) sel = (sel + 1) % count;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) sel = (sel + count - 1) % count;
}

bool Enter() { return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE); }

const char* WindName(int w)
{
    switch (w) {
    case 0: return L("Calm", "Штиль");
    case 1: return "270/10";
    case 2: return L("360/15 G25 crosswind", "360/15 порывы 25, боковой");
    default: return L("090/25 turbulent", "090/25 болтанка");
    }
}

} // namespace

void Menu::Open(MenuScreen s)
{
    if (s == MenuScreen::Settings && screen_ != MenuScreen::Settings) settingsBack_ = screen_;
    screen_ = s;
    sel_ = 0;
}

MenuAction Menu::Update(Config& cfg, const MissionRun& run, const Aircraft& a, float time)
{
    S = fmaxf(fminf(GetScreenHeight() / 900.0f, GetScreenWidth() / 1500.0f), 0.55f);
    time_ = time;
    switch (screen_) {
    case MenuScreen::Main: return MainScreen(cfg);
    case MenuScreen::Missions: return MissionsScreen(cfg);
    case MenuScreen::Free: return FreeScreen(cfg);
    case MenuScreen::Settings: return SettingsScreen(cfg);
    case MenuScreen::Pause: return PauseScreen();
    case MenuScreen::Results: return ResultsScreen(cfg, run);
    case MenuScreen::Crash: return CrashScreen(a);
    default: return {};
    }
}

MenuAction Menu::MainScreen(Config& cfg)
{
    const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    DrawRectangleGradientH(0, 0, (int)(sw * 0.55f), (int)sh, Color{6, 10, 18, 230}, Color{6, 10, 18, 0});
    float x = 80 * S, y = sh * 0.16f;
    ui::Draw(L("FLIGHT SIM", "АВИАСИМУЛЯТОР"), x, y, 64 * S, WHITE, true);
    ui::Draw(L("Island International · 3 aircraft · 14 missions", "Аэропорт «Остров» · 3 самолёта · 14 заданий"), x + 4 * S, y + 74 * S, 20 * S, kDim);

    const char* items[] = {L("Missions", "Задания"), L("Free flight", "Свободный полёт"), L("Settings", "Настройки"), L("Quit", "Выход")};
    Nav(sel_, 4);
    MenuAction act;
    for (int i = 0; i < 4; ++i) {
        Rectangle r{x, y + (140 + i * 62) * S, 360 * S, 50 * S};
        if (Button(r, items[i], sel_ == i, 24) || (sel_ == i && Enter())) {
            if (i == 0) Open(MenuScreen::Missions);
            else if (i == 1) Open(MenuScreen::Free);
            else if (i == 2) Open(MenuScreen::Settings);
            else act.kind = MenuAction::Quit;
        }
        if (CheckCollisionPointRec(GetMousePosition(), r)) sel_ = i;
    }
    int total = 0, earned = 0;
    for (const MissionDef& m : MissionList()) {
        total += 3;
        earned += cfg.Stars(m.id);
    }
    ui::Draw(TextFormat(L("Stars collected: %d / %d", "Собрано звёзд: %d / %d"), earned, total), x, y + 400 * S, 20 * S, kGold);
    ui::Draw(L("Mouse or arrows + Enter.  F1 in flight — controls.", "Мышь или стрелки + Enter.  В полёте F1 — управление."), x, sh - 50 * S,
             16 * S, kDim);
    return act;
}

MenuAction Menu::MissionsScreen(Config& cfg)
{
    const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    Dim();
    const auto& list = MissionList();
    const int n = (int)list.size();
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) {
        Open(MenuScreen::Main);
        return {};
    }
    Nav(selectedMission, n);
    float x = 40 * S, y = 30 * S;
    ui::Draw(L("Missions", "Задания"), x, y, 40 * S, WHITE, true);
    float rowH = fminf(44 * S, (sh - 140 * S) / n);
    float listW = fminf(560 * S, sw * 0.45f);
    MenuAction act;
    for (int i = 0; i < n; ++i) {
        Rectangle r{x, y + 60 * S + i * rowH, listW, rowH - 4 * S};
        bool hover = CheckCollisionPointRec(GetMousePosition(), r);
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (selectedMission == i) {
                act.kind = MenuAction::StartMission;
                act.index = i;
            }
            selectedMission = i;
        }
        bool sel = selectedMission == i;
        DrawRectangleRec(r, sel ? Color{40, 80, 130, 230} : (hover ? Color{30, 50, 80, 220} : kPanel));
        float ts = fminf(19 * S, rowH * 0.5f);
        ui::Draw(TextFormat("%2d. %s", i + 1, L(list[i].titleEn, list[i].titleRu)), r.x + 10 * S, r.y + (r.height - ts) * 0.5f, ts, kText);
        ui::DrawRight(Stars(cfg.Stars(list[i].id)).c_str(), r.x + r.width - 10 * S, r.y + (r.height - ts) * 0.5f, ts, kGold);
    }
    // Описание выбранного задания
    const MissionDef& m = list[selectedMission];
    float px = x + listW + 30 * S, py = y + 60 * S, pw = sw - px - 40 * S;
    DrawRectangle((int)px, (int)py, (int)pw, (int)(sh - py - 40 * S), kPanel);
    float tx = px + 24 * S, ty = py + 20 * S;
    ui::Draw(L(m.titleEn, m.titleRu), tx, ty, 30 * S, WHITE, true);
    ty += 46 * S;
    std::string diff;
    for (int i = 0; i < 3; ++i) diff += i < m.difficulty ? "●" : "○";
    ui::Draw(TextFormat("%s %s   %s %s", L("Difficulty", "Сложность"), diff.c_str(), L("Best:", "Рекорд:"), Stars(cfg.Stars(m.id)).c_str()), tx,
             ty, 18 * S, kGold);
    ty += 36 * S;
    ty = Paragraph(L(m.descEn, m.descRu), tx, ty, pw - 48 * S, 20 * S, kText) + 16 * S;
    const AircraftType& at = GetAircraftType(m.setup.aircraft);
    ui::Draw(TextFormat("%s: %s", L("Aircraft", "Самолёт"), L(at.nameEn, at.nameRu)), tx, ty, 18 * S, kDim);
    ty += 26 * S;
    ui::Draw(TextFormat("%s: %s · %s · %s %s", L("Conditions", "Условия"), Environment::TimeName(m.setup.time),
                        Environment::WeatherName(m.setup.weather), L("wind", "ветер"), WindName(m.setup.wind)),
             tx, ty, 18 * S, kDim);
    ty += 40 * S;
    ui::Draw(L("Stars: 1 — complete, 2 and 3 — precision (see results).", "Звёзды: 1 — выполнить, 2 и 3 — за точность (см. итоги)."), tx, ty,
             16 * S, kDim);
    Rectangle start{px + pw - 300 * S, sh - 110 * S, 260 * S, 50 * S};
    if (Button(start, L("Start  (Enter)", "Начать  (Enter)"), true) || Enter()) {
        act.kind = MenuAction::StartMission;
        act.index = selectedMission;
    }
    if (Button({px + 24 * S, sh - 110 * S, 180 * S, 50 * S}, L("Back (Esc)", "Назад (Esc)"), false)) Open(MenuScreen::Main);
    return act;
}

MenuAction Menu::FreeScreen(Config& cfg)
{
    const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    Dim();
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) {
        Open(MenuScreen::Main);
        return {};
    }
    const int rows = 8;
    Nav(sel_, rows + 1);
    float w = 760 * S, x = (sw - w) * 0.5f, y = 60 * S;
    ui::Draw(L("Free flight", "Свободный полёт"), x, y, 40 * S, WHITE, true);
    y += 70 * S;
    const Airport& ap = world::GetAirport(cfg.freeAirport % world::AirportCount());
    const AircraftType& at = GetAircraftType((AircraftKind)cfg.freeAircraft);
    std::string starts[5] = {
        TextFormat(L("Runway %s, ready for takeoff", "ВПП %s, исполнительный старт"), ap.rwy.ident[0]),
        TextFormat(L("Runway %s, ready for takeoff", "ВПП %s, исполнительный старт"), ap.rwy.ident[1]),
        TextFormat(L("Final approach %s", "Заход на посадку %s"), ap.rwy.ident[0]),
        TextFormat(L("Final approach %s", "Заход на посадку %s"), ap.rwy.ident[1]),
        L("In flight above the airport", "В воздухе над аэродромом")};
    const char* fuels[4] = {"25%", "50%", "75%", "100%"};
    struct Row { const char* label; std::string value; int* var; int count; };
    Row r[rows] = {
        {L("Aircraft", "Самолёт"), L(at.nameEn, at.nameRu), &cfg.freeAircraft, (int)AircraftKind::Count},
        {L("Airport", "Аэродром"), L(ap.nameEn, ap.nameRu), &cfg.freeAirport, world::AirportCount()},
        {L("Start", "Старт"), starts[cfg.freeStart % 5], &cfg.freeStart, 5},
        {L("Time of day", "Время суток"), Environment::TimeName((TimeOfDay)cfg.freeTime), &cfg.freeTime, (int)TimeOfDay::Count},
        {L("Weather", "Погода"), Environment::WeatherName((WeatherKind)cfg.freeWeather), &cfg.freeWeather, (int)WeatherKind::Count},
        {L("Wind", "Ветер"), WindName(cfg.freeWind), &cfg.freeWind, 4},
        {L("Fuel", "Топливо"), fuels[cfg.freeFuel % 4], &cfg.freeFuel, 4},
        {L("Failures", "Отказы"), cfg.freeFailures ? L("Random", "Случайные") : L("Off", "Нет"), &cfg.freeFailures, 2},
    };
    for (int i = 0; i < rows; ++i) {
        Rectangle rr{x, y + i * 56 * S, w, 48 * S};
        if (CheckCollisionPointRec(GetMousePosition(), rr)) sel_ = i;
        int d = OptionRow(rr, r[i].label, r[i].value.c_str(), sel_ == i);
        if (d) *r[i].var = (*r[i].var + d + r[i].count) % r[i].count;
    }
    MenuAction act;
    Rectangle go{x + w - 300 * S, y + rows * 56 * S + 20 * S, 300 * S, 54 * S};
    if (CheckCollisionPointRec(GetMousePosition(), go)) sel_ = rows;
    if (Button(go, L("Fly!  (Enter)", "Полетели!  (Enter)"), sel_ == rows, 24) || Enter()) act.kind = MenuAction::StartFree;
    if (Button({x, y + rows * 56 * S + 20 * S, 200 * S, 54 * S}, L("Back (Esc)", "Назад (Esc)"), false)) Open(MenuScreen::Main);
    (void)sh;
    return act;
}

MenuAction Menu::SettingsScreen(Config& cfg)
{
    const float sw = (float)GetScreenWidth();
    Dim();
    MenuAction act;
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) {
        Open(settingsBack_);
        return act;
    }
    Nav(sel_, 5);
    float w = 700 * S, x = (sw - w) * 0.5f, y = 80 * S;
    ui::Draw(L("Settings", "Настройки"), x, y, 40 * S, WHITE, true);
    y += 80 * S;
    int vals[4] = {cfg.ru ? 1 : 0, cfg.mouseYoke ? 1 : 0, cfg.volume / 10, cfg.showFps ? 1 : 0};
    const char* labels[4] = {L("Language", "Язык"), L("Mouse yoke by default", "Штурвал-мышь по умолчанию"), L("Volume", "Громкость"),
                             L("Show FPS", "Показывать FPS")};
    std::string values[4] = {cfg.ru ? "Русский" : "English", vals[1] ? L("On", "Вкл") : L("Off", "Выкл"), TextFormat("%d%%", cfg.volume),
                             vals[3] ? L("On", "Вкл") : L("Off", "Выкл")};
    int counts[4] = {2, 2, 11, 2};
    for (int i = 0; i < 4; ++i) {
        Rectangle rr{x, y + i * 60 * S, w, 50 * S};
        if (CheckCollisionPointRec(GetMousePosition(), rr)) sel_ = i;
        int d = OptionRow(rr, labels[i], values[i].c_str(), sel_ == i);
        if (d) {
            vals[i] = (vals[i] + d + counts[i]) % counts[i];
            cfg.ru = vals[0] == 1;
            ui::ru = cfg.ru;
            cfg.mouseYoke = vals[1] == 1;
            cfg.volume = vals[2] * 10;
            cfg.showFps = vals[3] == 1;
            cfg.Save();
            act.kind = MenuAction::SettingsChanged;
        }
    }
    Rectangle back{x, y + 4 * 60 * S + 30 * S, 220 * S, 50 * S};
    if (CheckCollisionPointRec(GetMousePosition(), back)) sel_ = 4;
    if (Button(back, L("Back", "Назад"), sel_ == 4) || (sel_ == 4 && Enter())) Open(settingsBack_);
    return act;
}

MenuAction Menu::PauseScreen()
{
    const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    Dim();
    MenuAction act;
    if (IsKeyPressed(KEY_ESCAPE)) {
        act.kind = MenuAction::Resume;
        return act;
    }
    const char* items[] = {L("Continue", "Продолжить"), L("Restart", "Начать заново"), L("Controls (F1)", "Управление (F1)"),
                           L("Settings", "Настройки"), L("Main menu", "Главное меню")};
    Nav(sel_, 5);
    float w = 420 * S, x = (sw - w) * 0.5f, y = sh * 0.22f;
    ui::DrawCentered(L("Pause", "Пауза"), sw * 0.5f, y, 44 * S, WHITE, true);
    for (int i = 0; i < 5; ++i) {
        Rectangle r{x, y + (80 + i * 62) * S, w, 50 * S};
        if (CheckCollisionPointRec(GetMousePosition(), r)) sel_ = i;
        if (Button(r, items[i], sel_ == i) || (sel_ == i && Enter())) {
            if (i == 0) act.kind = MenuAction::Resume;
            else if (i == 1) act.kind = MenuAction::Restart;
            else if (i == 2) act.kind = MenuAction::ToggleHelp;
            else if (i == 3) Open(MenuScreen::Settings);
            else act.kind = MenuAction::ToMain;
        }
    }
    return act;
}

MenuAction Menu::ResultsScreen(const Config& cfg, const MissionRun& run)
{
    const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    Dim();
    MenuAction act;
    float w = 820 * S, h = 520 * S, x = (sw - w) * 0.5f, y = (sh - h) * 0.4f;
    DrawRectangle((int)x, (int)y, (int)w, (int)h, Color{14, 18, 28, 240});
    DrawRectangleLinesEx({x, y, w, h}, 2.0f, run.Success() ? kGold : Color{230, 70, 60, 255});
    ui::DrawCentered(run.Title().c_str(), sw * 0.5f, y + 18 * S, 24 * S, kDim);
    ui::DrawCentered(run.Success() ? run.resultText.c_str() : L("Mission failed", "Задание не выполнено"), sw * 0.5f, y + 52 * S, 36 * S,
                     run.Success() ? WHITE : Color{255, 90, 80, 255}, true);
    int stars = run.Stars();
    // Звёзды по очереди «загораются».
    for (int i = 0; i < 3; ++i) {
        bool on = i < stars && time_ > 0.4f * (i + 1);
        ui::DrawCentered(on ? "★" : "☆", sw * 0.5f + (i - 1) * 80 * S, y + 100 * S, 64 * S, on ? kGold : Color{90, 90, 100, 255});
    }
    float ty = y + 190 * S;
    if (!run.Success()) {
        ty = Paragraph(run.resultText.c_str(), x + 40 * S, ty, w - 80 * S, 20 * S, Color{255, 150, 140, 255}) + 10 * S;
    }
    for (const MissionRun::Criterion& c : run.criteria) {
        ui::Draw(c.met ? "✓" : "✗", x + 40 * S, ty, 22 * S, c.met ? Color{80, 230, 110, 255} : Color{230, 80, 70, 255});
        ui::Draw(c.text.c_str(), x + 74 * S, ty + 2 * S, 19 * S, kText);
        ty += 34 * S;
    }
    int mi = run.Setup().mission;
    if (mi >= 0) {
        ui::Draw(TextFormat("%s %s", L("Best:", "Рекорд:"), Stars(cfg.Stars(MissionList()[mi].id)).c_str()), x + 40 * S, y + h - 110 * S, 18 * S,
                 kGold);
    }
    bool hasNext = run.Success() && mi >= 0 && mi + 1 < (int)MissionList().size();
    std::vector<const char*> items = {L("Retry (R)", "Ещё раз (R)")};
    if (hasNext) items.push_back(L("Next mission", "Следующее задание"));
    items.push_back(L("Missions", "К заданиям"));
    int n = (int)items.size();
    if (IsKeyPressed(KEY_LEFT)) sel_ = (sel_ + n - 1) % n;
    if (IsKeyPressed(KEY_RIGHT)) sel_ = (sel_ + 1) % n;
    if (IsKeyPressed(KEY_R)) act.kind = MenuAction::Restart;
    float bw = (w - 80 * S - (n - 1) * 16 * S) / n;
    for (int i = 0; i < n; ++i) {
        Rectangle r{x + 40 * S + i * (bw + 16 * S), y + h - 70 * S, bw, 48 * S};
        if (CheckCollisionPointRec(GetMousePosition(), r)) sel_ = i;
        if (Button(r, items[i], sel_ == i, 20) || (sel_ == i && IsKeyPressed(KEY_ENTER))) {
            if (i == 0) act.kind = MenuAction::Restart;
            else if (hasNext && i == 1) {
                act.kind = MenuAction::StartMission;
                act.index = mi + 1;
                selectedMission = mi + 1;
            } else {
                Open(MenuScreen::Missions);
                act.kind = MenuAction::ToMain;
                act.index = 1;   // в список заданий
            }
        }
    }
    return act;
}

MenuAction Menu::CrashScreen(const Aircraft& a)
{
    const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    MenuAction act;
    float w = 760 * S, h = 260 * S, x = (sw - w) * 0.5f, y = (sh - h) * 0.35f;
    DrawRectangle((int)x, (int)y, (int)w, (int)h, Color{20, 12, 14, 235});
    DrawRectangleLinesEx({x, y, w, h}, 2.0f, Color{230, 60, 50, 255});
    ui::DrawCentered(L("CRASH", "АВАРИЯ"), sw * 0.5f, y + 18 * S, 40 * S, Color{255, 80, 70, 255}, true);
    Paragraph(a.crashReason.c_str(), x + 40 * S, y + 80 * S, w - 80 * S, 20 * S, kText);
    if (IsKeyPressed(KEY_R)) act.kind = MenuAction::Restart;
    const char* items[] = {L("Retry (R)", "Ещё раз (R)"), L("Main menu (Esc)", "Главное меню (Esc)")};
    if (IsKeyPressed(KEY_ESCAPE)) act.kind = MenuAction::ToMain;
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT)) sel_ = 1 - sel_;
    for (int i = 0; i < 2; ++i) {
        Rectangle r{x + 40 * S + i * 350 * S, y + h - 70 * S, 330 * S, 48 * S};
        if (CheckCollisionPointRec(GetMousePosition(), r)) sel_ = i;
        if (Button(r, items[i], sel_ == i, 20) || (sel_ == i && IsKeyPressed(KEY_ENTER)))
            act.kind = i == 0 ? MenuAction::Restart : MenuAction::ToMain;
    }
    return act;
}
