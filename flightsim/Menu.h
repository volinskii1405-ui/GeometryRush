#pragma once
#include "Config.h"
#include "Logbook.h"
#include "Missions.h"

class Aircraft;

enum class MenuScreen { None, Main, Missions, Free, Settings, Pause, Results, Crash, Logbook };

struct MenuAction {
    enum Kind { None, StartMission, StartFree, Resume, Restart, ToMain, Quit, ToggleHelp, SettingsChanged, Replay } kind = None;
    int index = 0;
};

// Экранные меню (немедленный режим: рисует и обрабатывает ввод за один вызов).
class Menu {
public:
    void Open(MenuScreen s);
    MenuScreen Screen() const { return screen_; }
    MenuAction Update(Config& cfg, const MissionRun& run, const Aircraft& a, float time);

    int selectedMission = 0;
    const Logbook* logbook = nullptr;

private:
    MenuAction MainScreen(Config& cfg);
    MenuAction MissionsScreen(Config& cfg);
    MenuAction FreeScreen(Config& cfg);
    MenuAction SettingsScreen(Config& cfg);
    MenuAction PauseScreen();
    MenuAction ResultsScreen(const Config& cfg, const MissionRun& run);
    MenuAction CrashScreen(const Aircraft& a);
    MenuAction LogbookScreen();

    MenuScreen screen_ = MenuScreen::Main;
    MenuScreen settingsBack_ = MenuScreen::Main;
    int sel_ = 0;
    float time_ = 0;
};
