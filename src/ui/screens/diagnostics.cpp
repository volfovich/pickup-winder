#include "ui/screens/diagnostics.h"
#include "app.h"
#include "config.h"
#include "tmc_driver.h"
#include "traverse.h"
#include "ui/screens/driver.h"
#include "ui/screens/setup.h"
#include "ui/screens/touch_test.h"
#include "ui/ui.h"
#include "winder.h"

namespace ui::screens
{
    Diagnostics diagnostics;

    namespace
    {
        constexpr uint32_t REFRESH_MS = 500;
        constexpr int16_t LINE_Y      = 34;
        constexpr int16_t LINE_DY     = 22;
        constexpr int16_t VALUE_X     = 96;
        const char* const LABELS[]    = {"Firmware", "Loop max", "Spindle", "Guide", "Endstop", "Uptime"};
    }

    Diagnostics::Diagnostics()
        : buttons{
              {8, 188, 84, 46, "SPINDLE"},
              {96, 188, 70, 46, "GUIDE"},
              {170, 188, 72, 46, "TOUCH\nTEST"},
              {246, 188, 66, 46, "BACK"},
          },
          group(buttons, BTN_COUNT)
    {
    }

    void Diagnostics::enter()
    {
        drawStatusBar("DIAGNOSTICS");
        for (uint8_t i = 0; i < LINES; i++)
        {
            printAt(LABELS[i], 8, LINE_Y + i * LINE_DY, theme::FONT_BODY, theme::MUTED);
            shown[i][0] = '\0';
        }
        group.reset();
        group.drawAll();
        refresh();
    }

    void Diagnostics::drawLine(uint8_t i, const char* text, uint16_t color)
    {
        if (!strcmp(text, shown[i]) && color == shownColor[i]) return;
        strlcpy(shown[i], text, sizeof(shown[i]));
        shownColor[i] = color;

        int16_t y = LINE_Y + i * LINE_DY;
        tft().fillRect(VALUE_X, y - 2, theme::W - VALUE_X, LINE_DY - 2, theme::BG);
        printAt(text, VALUE_X, y, fitFont(text, theme::W - VALUE_X - 4, theme::FONT_BODY), color);
    }

    void Diagnostics::refresh()
    {
        char t[48];
        drawLine(0, FIRMWARE_NAME " v" FIRMWARE_VERSION, theme::FG);

        uint32_t us = loopMaxUs();
        snprintf(t, sizeof(t), "%lu.%lu ms", us / 1000, (us % 1000) / 100);
        drawLine(1, t, us > 5000 ? theme::WARN : theme::OK);

        if (tmc::spindle.ready())
        {
            snprintf(t, sizeof(t), "ready, %s, %u mA", winder::driverEnabled() ? "holding" : "motor free",
                     tmc::spindle.current());
            drawLine(2, t, theme::FG);
        }
        else drawLine(2, "NOT READY - check 24 V, re-init", theme::ERR);

        if (!tmc::traverse.ready()) drawLine(3, "driver not found (address 1)", theme::MUTED);
        else if (traverse::homed())
        {
            snprintf(t, sizeof(t), "ready, homed, at %.3f mm", traverse::positionMm());
            drawLine(3, t, theme::FG);
        }
        else drawLine(3, "ready, NOT HOMED", theme::WARN);

        drawLine(4, traverse::endstopTriggered() ? "PRESSED" : "open", theme::FG);

        char d[16];
        drawLine(5, fmtDuration(millis(), d, sizeof(d)), theme::FG);
    }

    void Diagnostics::update()
    {
        if (millis() - lastMs < REFRESH_MS) return;
        lastMs = millis();
        refresh();
    }

    void Diagnostics::onTouch(const touch::Event& e)
    {
        switch (group.handle(e))
        {
            case BTN_SPINDLE: driver.open(tmc::spindle); break;
            case BTN_GUIDE: driver.open(tmc::traverse); break;
            case BTN_TOUCH: show(touchTest); break;
            case BTN_BACK: show(setup); break;
            default: break;
        }
    }
}
