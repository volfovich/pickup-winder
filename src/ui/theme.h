#pragma once

#include <ILI9341_t3.h>
#include <font_Arial.h>
#include <font_ArialBold.h>

namespace ui::theme
{
    constexpr int16_t W        = 320;
    constexpr int16_t H        = 240;
    constexpr int16_t STATUS_H = 26;

    constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    constexpr uint16_t BG         = ILI9341_BLACK;
    constexpr uint16_t FG         = ILI9341_WHITE;
    constexpr uint16_t MUTED      = rgb(140, 140, 140);
    constexpr uint16_t STATUS_BG  = rgb(30, 40, 60);
    constexpr uint16_t BUTTON     = rgb(50, 60, 80);
    constexpr uint16_t BUTTON_HI  = rgb(90, 110, 150);
    constexpr uint16_t OK         = rgb(40, 200, 80);
    constexpr uint16_t WARN       = rgb(255, 150, 0);
    constexpr uint16_t ERR        = rgb(230, 40, 40);
    constexpr uint16_t ACCENT     = rgb(0, 200, 230);

    static const ILI9341_t3_font_t& FONT_TITLE  = Arial_13_Bold;
    static const ILI9341_t3_font_t& FONT_BODY   = Arial_12;
    static const ILI9341_t3_font_t& FONT_BUTTON = Arial_16_Bold;
    static const ILI9341_t3_font_t& FONT_BIG    = Arial_60_Bold;
}
