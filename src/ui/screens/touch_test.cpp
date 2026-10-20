#include "ui/screens/touch_test.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace ui::screens
{
    TouchTest touchTest;

    namespace
    {
        constexpr int16_t AREA_TOP    = 92; // dot drawing area
        constexpr int16_t AREA_BOTTOM = 184;

        // Info lines: labels drawn once, values redrawn one line per pass and
        // only when they change. At 30 MHz SPI the display fills ~1.9 Mpx/s,
        // so each value box (~3,800 px) costs ~2 ms instead of ~10 ms for
        // clearing the whole info area.
        constexpr uint32_t INFO_LINE_MS = 70; // all 3 lines refresh at ~5 Hz
        constexpr int16_t INFO_Y        = 34;
        constexpr int16_t INFO_DY       = 18;
        constexpr int16_t VALUE_X       = 82;
        const char* const INFO_LABELS[] = {"raw", "screen", "loop max"};

        // Accuracy markers: taps on these should land on them
        constexpr touch::Point MARKERS[] = {{20, 100}, {300, 100}, {160, 138}, {20, 176}, {300, 176}};

        enum : int8_t { BTN_CLEAR, BTN_CALIBRATE, BTN_COUNT };
        const Button BUTTONS[BTN_COUNT] = {
            {8, 192, 120, 42, "CLEAR"},
            {176, 192, 136, 42, "CALIBRATE"},
        };

        int8_t buttonAt(touch::Point p)
        {
            for (int8_t i = 0; i < BTN_COUNT; i++)
                if (BUTTONS[i].contains(p)) return i;
            return -1;
        }
    }

    void TouchTest::enter()
    {
        auto& d = tft();
        drawStatusBar("TOUCH TEST");
        d.drawRect(0, AREA_TOP - 4, theme::W, AREA_BOTTOM - AREA_TOP + 8, theme::BUTTON);
        for (auto m : MARKERS)
        {
            d.drawFastHLine(m.x - 5, m.y, 11, theme::MUTED);
            d.drawFastVLine(m.x, m.y - 5, 11, theme::MUTED);
        }
        for (auto& b : BUTTONS) b.draw();

        d.setFont(theme::FONT_BODY);
        d.setTextColor(theme::MUTED);
        for (uint8_t i = 0; i < INFO_LINES; i++)
        {
            d.setCursor(8, INFO_Y + i * INFO_DY);
            d.print(INFO_LABELS[i]);
            shown[i][0] = '\0';
        }

        lastDot    = {-1, -1};
        downButton = -1;
        lastInfoMs = 0;
        nextLine   = 0;
    }

    void TouchTest::drawInfoLine(uint8_t i)
    {
        char text[40];
        uint16_t color = theme::FG;
        if (i == 0)
        {
            touch::Raw r = touch::lastRaw();
            snprintf(text, sizeof(text), "x %4d   y %4d   z %4d", r.x, r.y, r.z);
        }
        else if (i == 1)
        {
            touch::Point p = touch::position();
            snprintf(text, sizeof(text), "x %4d   y %4d   %s", p.x, p.y, touch::isPressed() ? "pressed" : "");
        }
        else
        {
            uint32_t us = loopMaxUs();
            snprintf(text, sizeof(text), "%lu.%lu ms", us / 1000, (us % 1000) / 100);
            color = us > 5000 ? theme::WARN : theme::OK;
        }
        if (!strcmp(text, shown[i])) return;
        strcpy(shown[i], text);

        auto& d = tft();
        int16_t y = INFO_Y + i * INFO_DY;
        d.fillRect(VALUE_X, y - 2, theme::W - VALUE_X - 4, INFO_DY - 1, theme::BG);
        d.setFont(theme::FONT_BODY);
        d.setTextColor(color);
        d.setCursor(VALUE_X, y);
        d.print(text);
    }

    void TouchTest::update()
    {
        if (touch::isPressed() && downButton < 0)
        {
            touch::Point p = touch::position();
            bool moved     = abs(p.x - lastDot.x) > 1 || abs(p.y - lastDot.y) > 1;
            if (moved && p.y >= AREA_TOP && p.y <= AREA_BOTTOM)
            {
                tft().fillCircle(p.x, p.y, 2, theme::ACCENT);
                lastDot = p;
            }
        }

        if (millis() - lastInfoMs >= INFO_LINE_MS)
        {
            lastInfoMs = millis();
            drawInfoLine(nextLine);
            nextLine = (nextLine + 1) % INFO_LINES;
        }
    }

    void TouchTest::onTouch(const touch::Event& e)
    {
        if (e.type == touch::Type::Down)
        {
            downButton = buttonAt(e.pos);
            if (downButton >= 0) BUTTONS[downButton].draw(true);
        }
        else if (e.type == touch::Type::Up && downButton >= 0)
        {
            int8_t b   = downButton;
            downButton = -1;
            BUTTONS[b].draw();
            if (buttonAt(e.pos) != b) return; // slid off the button: cancel
            if (b == BTN_CLEAR) show(*this);
            else if (b == BTN_CALIBRATE) startCalibration();
        }
    }
}
