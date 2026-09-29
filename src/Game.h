// Game.h — состояния игры, главный цикл, рендер мира и эффекты.
#pragma once
#include "Level.h"
#include "Menu.h"
#include "Player.h"
#include "Settings.h"

#include <deque>
#include <string>
#include <vector>

enum class GameState { MainMenu, LevelSelect, IconSelect, Playing, Paused, Victory };

struct LevelEntry {
    std::string path;
    std::string id;       // имя файла без расширения — ключ рекорда
    LevelCard   card;
};

struct Particle {
    Vector2 pos, vel;
    float   life, maxLife;
    float   size;
    float   rot, spin;
    float   gravity;
    Color   color;
    bool    square;
};

class Game {
public:
    Game();
    ~Game();
    void Run();

private:
    // --- жизненный цикл
    void Frame();
    void SetState(GameState s);
    void ScanLevels();
    bool StartLevel(int index);
    void NewAttempt();

    // --- игра
    void UpdatePlaying(float dt);
    void HandleStepEvents(const StepEvents& ev);
    void OnDeath();
    void UpdateCamera(float dt, bool snap);
    void UpdateParticles(float dt);

    // --- рендер
    void DrawWorld();
    void DrawBackground();
    void DrawGroundAndCeiling();
    void DrawTrail();
    void DrawPlayer();
    void DrawParticles();
    void DrawHud();

    // --- эффекты
    void Burst(Vector2 at, int count, float speed, Color a, Color b, float size, float gravity, float life);

    // --- пути
    std::string AppPath(const std::string& file) const;

    GameState state_ = GameState::MainMenu;
    float     stateTime_ = 0;
    bool      quit_ = false;

    Settings  settings_;
    std::string settingsPath_;
    Menu      menu_;

    std::vector<LevelEntry> levels_;
    int       currentLevel_ = -1;
    Level     level_;
    Player    player_;

    // фиксированный шаг
    float accumulator_ = 0;
    bool  pendingPress_ = false;
    bool  inputLocked_ = false;    // ждём отпускания кнопки после входа в уровень

    // статистика
    int   attempts_ = 1;
    int   jumps_ = 0;
    float playTime_ = 0;
    VictoryStats victory_;

    // состояние попытки
    float deathTimer_ = -1;
    float victoryTimer_ = -1;
    float flash_ = 0;

    // камера
    float camX_ = 0, camY_ = 0, camTargetY_ = 0;
    float ceilingAlpha_ = 0;

    float time_ = 0;
    std::deque<Vector2>   trail_;
    std::vector<Particle> particles_;
};
