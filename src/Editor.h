// Editor.h — встроенный редактор уровней: рисование мышью по сетке,
// инструменты, отмена, темы, сохранение в текстовый формат уровня и тест-плей.
#pragma once
#include "raylib.h"
#include "Level.h"
#include <string>
#include <vector>

enum class EditorAction { None, Back, TestPlay };

class Editor {
public:
    // Новый пустой уровень в каталоге dir (имя файла подбирается автоматически).
    void NewLevel(const std::string& dir);
    // Открыть существующий файл уровня.
    bool Open(const std::string& path);

    // Ввод + отрисовка (вызывать между BeginDrawing/EndDrawing).
    EditorAction UpdateDraw(float dt, float time);

    bool Save();                                   // сохранить в Path()
    std::string Serialize() const;                 // текст уровня
    const std::string& Path() const { return path_; }
    void ShowMessage(const std::string& msg, float seconds = 2.5f);

private:
    void EnsureWidth(int cols);
    void SetCell(int col, int row, char ch);
    char GetCell(int col, int row) const;
    void PushUndo();
    void Undo();
    void RebuildPreview();
    int  LastUsedColumn() const;

    void DrawWorld(float time);
    void DrawToolbar();
    void DrawTopBar();

    std::vector<std::string> grid_;                // строки сетки, одинаковой длины
    std::vector<std::vector<std::string>> undo_;
    std::string name_ = "My Level";
    std::string path_;
    Color bg_ = {40, 90, 200, 255};
    Color ground_ = {30, 70, 170, 255};
    int   theme_ = 0;
    int   tool_ = 0;
    float camX_ = 0;
    bool  stroke_ = false;                         // идёт мазок (для одной записи в undo)
    bool  dirty_ = false;
    bool  editingName_ = false;
    std::string message_;
    float messageTime_ = 0;
    Level preview_;
};
