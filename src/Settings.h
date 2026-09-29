// Settings.h — выбор иконок/цветов и рекорды, хранятся в settings.txt (key=value).
#pragma once
#include <map>
#include <string>

struct Settings {
    int  cubeIcon  = 0;
    int  ballIcon  = 0;
    int  shipIcon  = 0;
    int  primary   = 3;     // индекс в палитре
    int  secondary = 5;
    bool trail     = true;
    std::map<std::string, int> bestPercent;   // id уровня -> лучший процент

    bool Load(const std::string& path);
    bool Save(const std::string& path) const;

    int  Best(const std::string& levelId) const;
    void ReportProgress(const std::string& levelId, int percent);  // обновит рекорд
    int& IconFor(int mode);                                        // по PlayerMode
};
