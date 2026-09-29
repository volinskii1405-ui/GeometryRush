// Game.cpp — главный цикл, машина состояний, рендер мира, HUD и эффекты.
#include "Game.h"
#include "Collision.h"
#include "IconRenderer.h"
#include "UI.h"
#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

const float W = (float)cfg::SCREEN_W;
const float H = (float)cfg::SCREEN_H;

float Hash(int n) {
    float x = sinf(n * 127.1f + 311.7f) * 43758.5453f;
    return x - floorf(x);
}

float Rnd() { return GetRandomValue(0, 10000) / 10000.0f; }

Color Scale(Color c, float k) {
    auto f = [k](unsigned char v) { return (unsigned char)std::clamp(v * k, 0.0f, 255.0f); };
    return {f(c.r), f(c.g), f(c.b), c.a};
}

Color LerpColor(Color a, Color b, float t) {
    return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
            (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}

bool JumpHeld() {
    return IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_UP) || IsKeyDown(KEY_W) || IsMouseButtonDown(MOUSE_BUTTON_LEFT);
}

bool JumpPressed() {
    return IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) ||
           IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

float g_renderAlpha = 1.0f;   // доля шага физики для интерполяции рендера
int   g_stepCounter = 0;

} // namespace

// ============================================================ жизненный цикл

Game::Game() {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(cfg::SCREEN_W, cfg::SCREEN_H, cfg::WINDOW_TITLE);
    SetTargetFPS(cfg::TARGET_FPS);
    SetExitKey(KEY_NULL);   // Esc используется для паузы

    settingsPath_ = AppPath("settings.txt");
    settings_.Load(settingsPath_);
    ScanLevels();
}

Game::~Game() {
    settings_.Save(settingsPath_);
    CloseWindow();
}

std::string Game::AppPath(const std::string& file) const {
    return std::string(GetApplicationDirectory()) + file;
}

void Game::Run() {
    while (!quit_ && !WindowShouldClose()) Frame();
}

void Game::SetState(GameState s) {
    state_ = s;
    stateTime_ = 0;
    menu_.ResetFocus();
}

void Game::ScanLevels() {
    levels_.clear();
    const std::string candidates[] = {AppPath("levels"), "levels", "../levels", "../../levels"};
    for (const std::string& dir : candidates) {
        if (!DirectoryExists(dir.c_str())) continue;
        FilePathList files = LoadDirectoryFilesEx(dir.c_str(), ".txt", false);
        std::vector<std::string> paths;
        for (unsigned i = 0; i < files.count; ++i) paths.push_back(files.paths[i]);
        UnloadDirectoryFiles(files);
        std::sort(paths.begin(), paths.end());

        for (const std::string& p : paths) {
            Level lvl;
            std::string err;
            if (!lvl.LoadFromFile(p, &err)) {
                TraceLog(LOG_WARNING, "Level %s skipped: %s", p.c_str(), err.c_str());
                continue;
            }
            LevelEntry e;
            e.path = p;
            e.id = GetFileNameWithoutExt(p.c_str());
            e.card.name = lvl.Name();
            e.card.lengthTiles = (int)(lvl.FinishX() / cfg::TILE);
            e.card.portalCount = (int)lvl.Portals().size();
            levels_.push_back(e);
        }
        if (!levels_.empty()) {
            TraceLog(LOG_INFO, "Loaded %d level(s) from %s", (int)levels_.size(), dir.c_str());
            return;
        }
    }
    TraceLog(LOG_WARNING, "No levels found");
}

bool Game::StartLevel(int index) {
    if (index < 0 || index >= (int)levels_.size()) return false;
    std::string err;
    if (!level_.LoadFromFile(levels_[index].path, &err)) {
        TraceLog(LOG_ERROR, "Cannot load level: %s", err.c_str());
        return false;
    }
    currentLevel_ = index;
    attempts_ = 1;
    jumps_ = 0;
    playTime_ = 0;
    particles_.clear();
    NewAttempt();
    inputLocked_ = true;
    SetState(GameState::Playing);
    return true;
}

void Game::NewAttempt() {
    player_.Reset(level_);
    accumulator_ = 0;
    pendingPress_ = false;
    deathTimer_ = -1;
    victoryTimer_ = -1;
    trail_.clear();
    ceilingAlpha_ = 0;
    g_renderAlpha = 1.0f;
    UpdateCamera(0, true);
}

// ============================================================ кадр

void Game::Frame() {
    float dt = std::min(GetFrameTime(), cfg::MAX_FRAME_TIME);
    time_ += dt;
    stateTime_ += dt;

    if (state_ == GameState::Playing) UpdatePlaying(dt);
    if (state_ != GameState::Paused) UpdateParticles(dt);

    // короткая блокировка ввода после смены экрана (особенно после финиша)
    float gate = state_ == GameState::Victory ? 0.6f : 0.12f;
    ui::SetInputEnabled(stateTime_ > gate);

    BeginDrawing();
    ClearBackground(BLACK);
    switch (state_) {
        case GameState::MainMenu: {
            MenuAction a = menu_.MainMenu(time_, settings_);
            if (a == MenuAction::OpenLevels) SetState(GameState::LevelSelect);
            else if (a == MenuAction::OpenIcons) SetState(GameState::IconSelect);
            else if (a == MenuAction::Quit) quit_ = true;
            break;
        }
        case GameState::LevelSelect: {
            std::vector<LevelCard> cards;
            for (auto& e : levels_) {
                LevelCard c = e.card;
                c.bestPercent = settings_.Best(e.id);
                cards.push_back(c);
            }
            MenuAction a = menu_.LevelSelect(time_, cards, settings_);
            if (a == MenuAction::StartLevel) StartLevel(menu_.SelectedLevel());
            else if (a == MenuAction::Back) SetState(GameState::MainMenu);
            break;
        }
        case GameState::IconSelect: {
            bool changed = false;
            MenuAction a = menu_.IconSelect(time_, settings_, &changed);
            if (changed) settings_.Save(settingsPath_);
            if (a == MenuAction::Back) SetState(GameState::MainMenu);
            break;
        }
        case GameState::Playing:
            DrawWorld();
            DrawHud();
            break;
        case GameState::Paused: {
            DrawWorld();
            DrawHud();
            MenuAction a = menu_.Pause(time_);
            if (a == MenuAction::Resume) {
                inputLocked_ = true;
                SetState(GameState::Playing);
            } else if (a == MenuAction::Restart) {
                ++attempts_;
                NewAttempt();
                inputLocked_ = true;
                SetState(GameState::Playing);
            } else if (a == MenuAction::ToMenu) {
                settings_.Save(settingsPath_);
                SetState(GameState::MainMenu);
            }
            break;
        }
        case GameState::Victory: {
            DrawWorld();
            MenuAction a = menu_.Victory(time_, victory_, settings_);
            if (a == MenuAction::Replay) StartLevel(currentLevel_);
            else if (a == MenuAction::ToMenu) SetState(GameState::MainMenu);
            break;
        }
    }
    EndDrawing();
}

// ============================================================ игровой процесс

void Game::UpdatePlaying(float dt) {
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) {
        SetState(GameState::Paused);
        return;
    }
    if (IsKeyPressed(KEY_R) && deathTimer_ < 0 && victoryTimer_ < 0) {   // быстрый рестарт
        ++attempts_;
        NewAttempt();
        return;
    }

    bool held = JumpHeld();
    bool pressed = JumpPressed();
    if (inputLocked_) {
        if (!held) inputLocked_ = false;
        held = pressed = false;
    }
    pendingPress_ = pendingPress_ || pressed;

    flash_ = std::max(0.0f, flash_ - dt * 3.0f);

    if (deathTimer_ >= 0) {                       // взрыв, затем новая попытка
        deathTimer_ -= dt;
        if (deathTimer_ < 0) {
            ++attempts_;
            NewAttempt();
        }
        return;
    }
    if (victoryTimer_ >= 0) {
        victoryTimer_ -= dt;
        player_.prevPos = player_.pos;                 // игрок уезжает за финиш
        player_.pos.x += cfg::PLAYER_SPEED * dt;
        if (victoryTimer_ < 0) SetState(GameState::Victory);
        return;
    }

    playTime_ += dt;
    accumulator_ += dt;
    while (accumulator_ >= cfg::PHYSICS_DT) {
        StepEvents ev;
        player_.Step(level_, held, pendingPress_, &ev);
        pendingPress_ = false;
        accumulator_ -= cfg::PHYSICS_DT;
        ++g_stepCounter;

        if (!player_.dead && g_stepCounter % 2 == 0) {
            trail_.push_front(player_.Center());
            while ((int)trail_.size() > cfg::TRAIL_LENGTH) trail_.pop_back();
        }
        if (player_.mode == PlayerMode::Ship && player_.holding && g_stepCounter % 6 == 0) {
            Vector2 c = player_.Center();
            float a = (player_.rotation + 180.0f) * DEG2RAD;
            Vector2 back = {c.x + cosf(a) * 22, c.y + sinf(a) * 22 + 4 * player_.GravityDir()};
            Particle p{back, {-180 - Rnd() * 120, (Rnd() - 0.5f) * 90}, 0.35f, 0.35f, 5 + Rnd() * 4,
                       0, 200, 0, LerpColor(Color{255, 220, 80, 255}, Color{255, 90, 30, 255}, Rnd()), false};
            particles_.push_back(p);
        }
        HandleStepEvents(ev);
        if (player_.dead || player_.finished) {
            accumulator_ = 0;
            break;
        }
    }
    g_renderAlpha = (player_.dead || player_.finished) ? 1.0f : accumulator_ / cfg::PHYSICS_DT;

    UpdateCamera(dt, false);
    float targetCeil = player_.CeilingActive() ? 1.0f : 0.0f;
    ceilingAlpha_ += (targetCeil - ceilingAlpha_) * std::min(1.0f, dt * 6.0f);
}

void Game::HandleStepEvents(const StepEvents& ev) {
    Color c1 = icons::Palette(settings_.primary);
    Color c2 = icons::Palette(settings_.secondary);
    Vector2 c = player_.Center();
    if (ev.jumped) ++jumps_;
    if (ev.landed && player_.mode == PlayerMode::Cube) {
        Vector2 foot = {c.x, c.y + cfg::PLAYER_SIZE * 0.5f * player_.GravityDir()};
        Burst(foot, 5, 90, Fade(WHITE, 0.8f), c1, 4, 300 * player_.GravityDir(), 0.3f);
    }
    if (ev.portal) {
        Color pc = PortalColor(ev.portalType);
        Burst(c, 18, 260, pc, WHITE, 6, 0, 0.45f);
        flash_ = 0.6f;
    }
    if (ev.died) OnDeath();
    if (ev.finished) {
        victoryTimer_ = cfg::VICTORY_DELAY;
        const std::string& id = levels_[currentLevel_].id;
        victory_.newRecord = settings_.Best(id) < 100;
        settings_.ReportProgress(id, 100);
        settings_.Save(settingsPath_);
        victory_.levelName = level_.Name();
        victory_.attempts = attempts_;
        victory_.jumps = jumps_;
        victory_.seconds = playTime_;
        for (int i = 0; i < 6; ++i)
            Burst({c.x + 200, c.y - 100}, 14, 500, icons::Palette(i * 2), icons::Palette(i * 2 + 1), 8, 700, 1.6f);
        (void)c2;
    }
}

void Game::OnDeath() {
    Color c1 = icons::Palette(settings_.primary);
    Color c2 = icons::Palette(settings_.secondary);
    Vector2 c = player_.Center();
    Burst(c, 28, 420, c1, c2, 9, 600, 0.7f);
    Burst(c, 12, 200, WHITE, c1, 5, 0, 0.4f);
    flash_ = 1.0f;

    int percent = (int)(level_.Progress(player_.pos.x) * 100.0f);
    const std::string& id = levels_[currentLevel_].id;
    if (percent > settings_.Best(id)) {
        settings_.ReportProgress(id, percent);
        settings_.Save(settingsPath_);
    }
    if (cfg::DEATH_RESTART_DELAY <= 0.0f) {
        ++attempts_;
        NewAttempt();
    } else {
        deathTimer_ = cfg::DEATH_RESTART_DELAY;
    }
}

void Game::UpdateCamera(float dt, bool snap) {
    Vector2 c = player_.RenderCenter(g_renderAlpha);
    camX_ = c.x - cfg::CAMERA_PLAYER_SCREEN_X;

    float maxY = level_.FloorY() - cfg::CAMERA_FLOOR_SCREEN_Y;   // пол не выше этой линии экрана
    if (snap) camTargetY_ = maxY;
    float target;
    if (player_.mode != PlayerMode::Cube) {
        target = (level_.CeilingY() + level_.FloorY()) * 0.5f - H * 0.5f;   // центр коридора
    } else {
        target = camTargetY_;
        if (c.y - target < cfg::CAMERA_BAND_TOP) target = c.y - cfg::CAMERA_BAND_TOP;
        if (c.y - target > cfg::CAMERA_BAND_BOTTOM) target = c.y - cfg::CAMERA_BAND_BOTTOM;
    }
    target = std::min(target, maxY);
    camTargetY_ = target;
    if (snap) camY_ = target;
    else camY_ += (target - camY_) * (1.0f - expf(-cfg::CAMERA_SMOOTH * dt));
}

void Game::Burst(Vector2 at, int count, float speed, Color a, Color b, float size, float gravity, float life) {
    for (int i = 0; i < count; ++i) {
        float ang = Rnd() * 2 * PI;
        float sp = speed * (0.3f + 0.7f * Rnd());
        Particle p;
        p.pos = at;
        p.vel = {cosf(ang) * sp, sinf(ang) * sp};
        p.maxLife = p.life = life * (0.6f + 0.4f * Rnd());
        p.size = size * (0.6f + 0.6f * Rnd());
        p.rot = Rnd() * 360;
        p.spin = (Rnd() - 0.5f) * 720;
        p.gravity = gravity;
        p.color = LerpColor(a, b, Rnd());
        p.square = true;
        particles_.push_back(p);
    }
}

void Game::UpdateParticles(float dt) {
    for (Particle& p : particles_) {
        p.vel.y += p.gravity * dt;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        p.rot += p.spin * dt;
        p.life -= dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const Particle& p) { return p.life <= 0; }),
                     particles_.end());
}

// ============================================================ рендер

void Game::DrawWorld() {
    DrawBackground();
    Camera2D cam{};
    cam.target = {camX_, camY_};
    cam.zoom = 1.0f;
    BeginMode2D(cam);

    DrawGroundAndCeiling();
    level_.Draw(camX_, camX_ + W, time_);

    // надпись "Attempt N" в мире у старта
    char buf[48];
    std::snprintf(buf, sizeof buf, "Attempt %d", attempts_);
    float ay = level_.FloorY() - cfg::TILE * 6.5f;
    ui::Text(buf, cfg::PLAYER_START_X + 260 + 3, ay + 3, 56, Fade(BLACK, 0.4f));
    ui::Text(buf, cfg::PLAYER_START_X + 260, ay, 56, WHITE);

    if (settings_.trail) DrawTrail();
    if (!player_.dead) DrawPlayer();
    DrawParticles();
    EndMode2D();

    if (flash_ > 0) DrawRectangle(0, 0, (int)W, (int)H, Fade(WHITE, flash_ * 0.25f));
}

void Game::DrawBackground() {
    Color bg = level_.BackgroundColor();
    DrawRectangleGradientV(0, 0, (int)W, (int)H, Scale(bg, 1.05f), Scale(bg, 0.35f));

    // дальний слой: крупные повёрнутые квадраты (параллакс 0.08)
    float fx = camX_ * 0.08f;
    const float sp1 = 300;
    int i0 = (int)floorf(fx / sp1) - 1, i1 = i0 + (int)(W / sp1) + 3;
    for (int i = i0; i <= i1; ++i) {
        float x = i * sp1 - fx + Hash(i) * 120;
        float y = 90 + Hash(i + 11) * 260 - camY_ * 0.05f;
        float size = 70 + Hash(i + 7) * 110;
        float rot = i * 37.0f + time_ * 6.0f;
        DrawRectanglePro({x, y, size, size}, {size / 2, size / 2}, rot, Fade(WHITE, 0.035f));
        DrawPolyLinesEx({x, y}, 4, size * 0.72f, rot + 45, 2, Fade(WHITE, 0.07f));
    }

    // средний слой: силуэты башен (параллакс 0.3)
    float floorScreen = level_.FloorY() - camY_;
    float base = H * 0.8f + (floorScreen - cfg::CAMERA_FLOOR_SCREEN_Y) * 0.3f;
    float mx = camX_ * 0.3f;
    const float sp2 = 95;
    i0 = (int)floorf(mx / sp2) - 1;
    i1 = i0 + (int)(W / sp2) + 3;
    Color tower = Scale(bg, 0.55f);
    for (int i = i0; i <= i1; ++i) {
        float x = i * sp2 - mx;
        float h = 70 + Hash(i * 3 + 1) * 230;
        float w = 60 + Hash(i * 5 + 2) * 30;
        DrawRectangleRec({x, base - h, w, h + 400}, Fade(tower, 0.75f));
        DrawRectangleRec({x, base - h, w, 3}, Fade(WHITE, 0.12f));
        for (int k = 0; k < (int)(h / 36); ++k)
            if (Hash(i * 13 + k) > 0.55f) DrawRectangleRec({x + w * 0.35f, base - h + 16 + k * 36, 10, 10}, Fade(WHITE, 0.08f));
    }
}

void Game::DrawGroundAndCeiling() {
    Color g = level_.GroundColor();
    Color line = LerpColor(g, WHITE, 0.75f);
    float fy = level_.FloorY();
    float left = camX_ - 20, width = W + 40;

    // пол
    DrawRectangleRec({left, fy, width, 900}, Scale(g, 0.55f));
    DrawRectangleGradientV((int)left, (int)fy, (int)width, 90, g, Scale(g, 0.55f));
    const float tw = cfg::TILE * 3;
    for (float x = floorf(camX_ / tw) * tw; x < camX_ + W + tw; x += tw)
        DrawRectangleLinesEx({x + 6, fy + 10, tw - 12, tw - 12}, 2, Fade(WHITE, 0.07f));
    DrawRectangleGradientV((int)left, (int)fy - 14, (int)width, 14, Fade(line, 0.0f), Fade(line, 0.25f));
    DrawRectangleRec({left, fy, width, 3}, line);

    // потолок коридора (выезжает сверху)
    if (ceilingAlpha_ > 0.01f) {
        float cy = level_.CeilingY() - (1.0f - ceilingAlpha_) * 260.0f;
        DrawRectangleRec({left, cy - 900, width, 900}, Fade(Scale(g, 0.55f), ceilingAlpha_));
        DrawRectangleGradientV((int)left, (int)cy - 90, (int)width, 90, Fade(Scale(g, 0.55f), ceilingAlpha_), Fade(g, ceilingAlpha_));
        for (float x = floorf(camX_ / tw) * tw; x < camX_ + W + tw; x += tw)
            DrawRectangleLinesEx({x + 6, cy - tw + 2, tw - 12, tw - 12}, 2, Fade(WHITE, 0.07f * ceilingAlpha_));
        DrawRectangleGradientV((int)left, (int)cy, (int)width, 14, Fade(line, 0.25f * ceilingAlpha_), Fade(line, 0.0f));
        DrawRectangleRec({left, cy - 3, width, 3}, Fade(line, ceilingAlpha_));
    }
}

void Game::DrawTrail() {
    if (trail_.empty() || player_.dead) return;
    Color c = icons::Palette(settings_.primary);
    Vector2 head = player_.RenderCenter(g_renderAlpha);
    int n = (int)trail_.size();
    Vector2 prev = head;
    for (int i = 0; i < n; ++i) {
        float t = 1.0f - (float)i / n;
        float thick = cfg::PLAYER_SIZE * 0.5f * t;
        DrawLineEx(prev, trail_[i], thick, Fade(c, 0.45f * t));
        DrawCircleV(trail_[i], thick * 0.5f, Fade(c, 0.45f * t));
        prev = trail_[i];
    }
}

void Game::DrawPlayer() {
    Vector2 c = player_.RenderCenter(g_renderAlpha);
    float rot = player_.RenderRotation(g_renderAlpha);
    Color c1 = icons::Palette(settings_.primary);
    Color c2 = icons::Palette(settings_.secondary);
    // мягкое свечение
    DrawCircleV(c, cfg::PLAYER_SIZE * 0.95f, Fade(c1, 0.12f));
    switch (player_.mode) {
        case PlayerMode::Cube:
            icons::DrawCube(settings_.cubeIcon, c, cfg::PLAYER_SIZE + 2, rot, c1, c2);
            break;
        case PlayerMode::Ball:
            icons::DrawBall(settings_.ballIcon, c, cfg::PLAYER_SIZE + 2, rot, c1, c2);
            break;
        case PlayerMode::Ship:
            icons::DrawShip(settings_.shipIcon, c, cfg::PLAYER_SIZE * 1.25f, rot, player_.gravityFlipped, c1, c2,
                            settings_.cubeIcon);
            break;
    }
}

void Game::DrawParticles() {
    for (const Particle& p : particles_) {
        float a = std::clamp(p.life / p.maxLife, 0.0f, 1.0f);
        if (p.square)
            DrawRectanglePro({p.pos.x, p.pos.y, p.size, p.size}, {p.size / 2, p.size / 2}, p.rot, Fade(p.color, a));
        else
            DrawCircleV(p.pos, p.size * (0.5f + 0.5f * a), Fade(p.color, a));
    }
}

void Game::DrawHud() {
    float progress = level_.Progress(player_.pos.x);
    Rectangle bar = {W / 2 - 260, 16, 520, 18};
    ui::ProgressBar(bar, progress, icons::Palette(settings_.primary), Fade(BLACK, 0.45f));
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d%%", (int)(progress * 100.0f));
    ui::Text(buf, bar.x + bar.width + 14, bar.y - 2, 24, WHITE);

    std::snprintf(buf, sizeof buf, "Attempt %d", attempts_);
    ui::Text(buf, 18, 14, 24, WHITE);
    if (currentLevel_ >= 0) {
        std::snprintf(buf, sizeof buf, "Best %d%%", settings_.Best(levels_[currentLevel_].id));
        ui::Text(buf, 18, 42, 20, Fade(WHITE, 0.7f));
    }
    ui::Text(level_.Name().c_str(), 18, H - 32, 20, Fade(WHITE, 0.55f));
    ui::Text("Esc - pause   R - restart", W - 270, H - 32, 20, Fade(WHITE, 0.45f));
}
