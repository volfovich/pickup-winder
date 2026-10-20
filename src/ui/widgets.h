#pragma once

#include "ui/theme.h"
#include "ui/touch.h"

namespace ui
{
    ILI9341_t3& tft();

    struct Button
    {
        int16_t x, y, w, h;
        const char* label;
        uint16_t color = theme::BUTTON;

        void draw(bool highlighted = false) const;
        bool contains(touch::Point p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
    };

    // Title bar at the top of every screen, with the driver state on the right.
    void drawStatusBar(const char* title);

    // Prints text centred horizontally in [x, x+w) with its top at y.
    void printCentered(const char* text, int16_t x, int16_t w, int16_t y, const ILI9341_t3_font_t& font, uint16_t color);
}
