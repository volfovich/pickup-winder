#pragma once

#include "ui/theme.h"
#include "ui/touch.h"

namespace ui
{
    // The display. Everything draws into its frame buffer; calling this
    // marks the frame dirty so the screen manager sends it out.
    ILI9341_t3n& tft();

    struct Button
    {
        int16_t x, y, w, h;
        const char* label;
        uint16_t color     = theme::BUTTON;
        bool enabled       = true;
        const char* value  = nullptr; // set: drawn as a field (label left, value right)

        void draw(bool highlighted = false) const;
        bool contains(touch::Point p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
    };

    // Press/release handling for a screen's buttons: highlights a button
    // while it is pressed and reports a tap when the finger lifts inside the
    // same button (sliding off cancels). Disabled buttons ignore touches.
    class ButtonGroup
    {
     public:
        ButtonGroup(Button* buttons, uint8_t count) : buttons(buttons), count(count) {}

        void drawAll() const;
        // Returns the index of a tapped button, or -1.
        int8_t handle(const touch::Event& e);
        int8_t pressed() const { return down; }
        void reset() { down = -1; }

     private:
        Button* buttons;
        uint8_t count;
        int8_t down = -1;
    };

    // Title bar at the top of every screen: title left, machine state right.
    void drawStatusBar(const char* title);
    // Redraws the status bar if the driver or run state changed.
    void refreshStatusBar();

    int16_t textWidth(const char* text, const ILI9341_t3_font_t& font);
    void printAt(const char* text, int16_t x, int16_t y, const ILI9341_t3_font_t& font, uint16_t color);
    void printCentered(const char* text, int16_t x, int16_t w, int16_t y, const ILI9341_t3_font_t& font, uint16_t color);
    void printRight(const char* text, int16_t right, int16_t y, const ILI9341_t3_font_t& font, uint16_t color);
    // Word-wraps text into a box of width w; returns the number of lines.
    uint8_t printWrapped(const char* text, int16_t x, int16_t y, int16_t w, int16_t lineH,
                         const ILI9341_t3_font_t& font, uint16_t color);

    // "12,345"
    const char* fmtThousands(uint32_t v, char* buf, size_t size);
    // "4:07" or "1:02:03"
    const char* fmtDuration(uint32_t ms, char* buf, size_t size);

    // Run display shared by the Running / Paused screens: big turn counter
    // with the target, and a progress bar under it.
    constexpr int16_t COUNTER_Y  = 32;
    constexpr int16_t PROGRESS_Y = 102;
    void drawTurnCounter(uint32_t turns, uint32_t target, uint16_t color);
    void drawProgressBar(uint32_t done, uint32_t total, uint16_t color);
}
