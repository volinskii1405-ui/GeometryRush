#pragma once
#include "raylib.h"

// Текст интерфейса: шрифт DejaVu Sans с кириллицей и выбор языка.
namespace ui {

extern bool ru;   // язык интерфейса: true — русский

// Строка на текущем языке.
inline const char* L(const char* en, const char* ruText) { return ru ? ruText : en; }

void LoadFonts();
void UnloadFonts();

// size — высота шрифта в пикселях.
void Draw(const char* s, float x, float y, float size, Color c, bool bold = false);
void DrawCentered(const char* s, float cx, float y, float size, Color c, bool bold = false);
void DrawRight(const char* s, float rx, float y, float size, Color c, bool bold = false);
void DrawOutlined(const char* s, float cx, float y, float size, Color c, bool bold = true);
float Measure(const char* s, float size, bool bold = false);

} // namespace ui
