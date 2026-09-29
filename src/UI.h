// UI.h — простые immediate-mode виджеты: текст, кнопки, панели.
#pragma once
#include "raylib.h"

namespace ui {

// Глобальное разрешение ввода для виджетов (защита от "проброса" кликов между экранами).
void SetInputEnabled(bool enabled);
bool InputEnabled();
bool KeyPressed(int key);                  // IsKeyPressed с учётом блокировки
bool AcceptPressed();                      // Enter / Space
bool BackPressed();                        // Esc / Backspace

float TextWidth(const char* text, float size);
void  Text(const char* text, float x, float y, float size, Color color);
void  TextCentered(const char* text, float cx, float y, float size, Color color);
void  TextShadowCentered(const char* text, float cx, float y, float size, Color color);

bool Hovered(Rectangle r);
bool Clicked(Rectangle r);

// Кнопка: возвращает true при клике. focused — подсветка с клавиатуры.
bool Button(Rectangle r, const char* label, bool focused, float fontSize = 30,
            Color accent = {90, 200, 255, 255});

void Panel(Rectangle r, Color fill, Color border);
void ProgressBar(Rectangle r, float t, Color fill, Color back);

// Анимированный фон меню.
void MenuBackground(float time, Color base);

} // namespace ui
