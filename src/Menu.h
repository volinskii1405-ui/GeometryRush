// Menu.h — экраны меню: главное, выбор уровня, выбор иконки, пауза, победа.
#pragma once
#include "Settings.h"
#include <string>
#include <vector>

enum class MenuAction { None, OpenLevels, OpenIcons, Quit, Back, StartLevel, Resume, Restart, ToMenu, Replay };

struct LevelCard {
    std::string name;
    int bestPercent = 0;
    int lengthTiles = 0;
    int portalCount = 0;
};

struct VictoryStats {
    std::string levelName;
    int   attempts = 0;
    int   jumps = 0;
    float seconds = 0;
    bool  newRecord = false;
};

class Menu {
public:
    // Каждая функция обрабатывает ввод и рисует экран (вызывать между BeginDrawing/EndDrawing).
    MenuAction MainMenu(float time, const Settings& s);
    MenuAction LevelSelect(float time, const std::vector<LevelCard>& levels, const Settings& s);
    MenuAction IconSelect(float time, Settings& s, bool* changed);
    MenuAction Pause(float time);                      // рисуется поверх игры
    MenuAction Victory(float time, const VictoryStats& st, const Settings& s);

    int  SelectedLevel() const { return selectedLevel_; }
    void SetSelectedLevel(int i) { selectedLevel_ = i; }
    void ResetFocus() { focus_ = 0; }

private:
    int focus_ = 0;           // фокус клавиатуры в вертикальных списках кнопок
    int selectedLevel_ = 0;
    int iconTab_ = 0;         // 0 куб, 1 шар, 2 корабль
    float levelSlide_ = 0;    // анимация перелистывания карточки
};
