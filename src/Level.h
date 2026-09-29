// Level.h — загрузка уровня из текстового файла и его объекты.
#pragma once
#include "raylib.h"
#include <string>
#include <vector>

enum class Tile : unsigned char { Empty, Block, SpikeUp, SpikeDown };

enum class PortalType { Cube, Ball, Ship, GravityFlip, GravityNormal };

struct Portal {
    PortalType type;
    Rectangle  rect;   // зона срабатывания
    int        col, row;
};

class Level {
public:
    // Загружает уровень. При ошибке возвращает false и пишет причину в error.
    bool LoadFromFile(const std::string& path, std::string* error = nullptr);
    bool LoadFromString(const std::string& text, std::string* error = nullptr);

    Tile  At(int col, int row) const;          // вне сетки -> Empty
    bool  IsSolid(int col, int row) const { return At(col, row) == Tile::Block; }

    int   Cols() const { return cols_; }
    int   Rows() const { return rows_; }
    float FloorY() const;                       // верхняя грань пола
    float CeilingY() const;                     // нижняя грань потолка коридора
    float FinishX() const { return finishX_; }
    float Progress(float x) const;              // 0..1

    const std::vector<Portal>& Portals() const { return portals_; }
    const std::string& Name() const { return name_; }
    Color BackgroundColor() const { return bgColor_; }
    Color GroundColor() const { return groundColor_; }

    // Рисует видимую часть уровня (в координатах мира, внутри BeginMode2D).
    void Draw(float viewLeft, float viewRight, float time) const;

private:
    std::vector<Tile>   tiles_;
    std::vector<Portal> portals_;
    int         cols_ = 0, rows_ = 0;
    float       finishX_ = 0.0f;
    std::string name_ = "Untitled";
    Color       bgColor_     = {40, 70, 160, 255};
    Color       groundColor_ = {30, 50, 130, 255};
};

Color PortalColor(PortalType type);
