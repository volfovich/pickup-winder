#include "ui/screens/touch_test.h"
#include "ui/screens/setup.h"
#include "ui/ui.h"

namespace ui::screens
{
    TouchTest touchTest;

    namespace
    {
        constexpr int16_t AREA_TOP    = 92; // dot drawing area
        constexpr int16_t AREA_BOTTOM = 184;

        // Info lines: labels drawn once, values redrawn one line per pass and
        // only when they change.
        constexpr uint32_t INFO_LINE_MS = 70; // all 3 lines refresh at ~5 Hz
        constexpr int16_t INFO_Y        = 34;
        constexpr int16_t INFO_DY       = 18;
        constexpr int16_t VALUE_X       = 82;
        const char* const INFO_LABELS[] = {"raw", "screen", "loop max"};

        // Accuracy markers: taps on these should land on them
        constexpr touch::Point MARKERS[] = {{20, 100}, {300, 100}, {160, 138}, {20, 176}, {300, 176}};
    }

    TouchTest::TouchTest()
        : buttons{
              {8, 192, 96, 42, "CLEAR"},
              {110, 192, 116, 42, "CALIBRATE"},
              {232, 192, 80, 42, "BACK"},
          },
          group(buttons, BTN_COUNT)
    {
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
        group.reset();
        group.drawAll();

        for (uint8_t i = 0; i < INFO_LINES; i++)
        {
            printAt(INFO_LABELS[i], 8, INFO_Y + i * INFO_DY, theme::FONT_BODY, theme::MUTED);
            shown[i][0] = '\0';
        }

        lastDot    = {-1, -1};
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

        int16_t y = INFO_Y + i * INFO_DY;
        tft().fillRect(VALUE_X, y - 2, theme::W - VALUE_X - 4, INFO_DY - 1, theme::BG);
        printAt(text, VALUE_X, y, theme::FONT_BODY, color);
    }

    void TouchTest::update()
    {
        if (touch::isPressed() && group.pressed() < 0)
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
        switch (group.handle(e))
        {
            case BTN_CLEAR: show(*this); break;
            case BTN_CALIBRATE: startCalibration(); break;
            case BTN_BACK: show(setup); break;
            default: break;
        }
    }
}
