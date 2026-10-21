#include "ui/screens/idle.h"
#include "app.h"
#include "ui/screens/setup.h"
#include "ui/ui.h"

namespace ui::screens
{
    Idle idle;

    namespace
    {
        constexpr uint32_t CHECK_MS = 250; // pick up changes made from serial
        constexpr int16_t VALUE_X   = 96;
        constexpr int16_t MSG_Y     = 113;
        constexpr uint32_t MSG_MS   = 5000;
    }

    Idle::Idle()
        : buttons{
              {8, 128, 304, 56, "START", theme::GO},
              {8, 192, 148, 42, "SETUP"},
              {164, 192, 148, 42, "DRIVER", theme::BUTTON, false}, // UI-5
          },
          group(buttons, BTN_COUNT)
    {
    }

    void Idle::enter()
    {
        drawStatusBar("IDLE");
        shown = app::settings();
        drawValues();
        group.reset();
        group.drawAll();
    }

    void Idle::drawValues()
    {
        static char text[32];
        auto& d = tft();
        d.fillRect(0, theme::STATUS_H, theme::W, 124 - theme::STATUS_H, theme::BG);

        printAt("Turns", 12, 40, theme::FONT_LABEL, theme::MUTED);
        printAt(fmtThousands(shown.turns, text, sizeof(text)), VALUE_X, 32, theme::FONT_LARGE, theme::FG);

        printAt("RPM", 12, 72, theme::FONT_LABEL, theme::MUTED);
        snprintf(text, sizeof(text), "%lu", shown.rpm);
        printAt(text, VALUE_X, 70, theme::FONT_VALUE, theme::FG);
        int16_t x = VALUE_X + textWidth(text, theme::FONT_VALUE) + 10;
        snprintf(text, sizeof(text), "(actual %.1f)", winder::actualRpm(shown.rpm));
        printAt(text, x, 73, theme::FONT_BODY, theme::MUTED);

        printAt("Accel", 12, 94, theme::FONT_LABEL, theme::MUTED);
        snprintf(text, sizeof(text), "%lu RPM/s", shown.accelRpmPerS);
        printAt(text, VALUE_X, 92, theme::FONT_VALUE, theme::FG);
        printAt("Dir", 232, 94, theme::FONT_LABEL, theme::MUTED);
        printAt(shown.dir == winder::Dir::CW ? "CW" : "CCW", 268, 92, theme::FONT_VALUE, theme::FG);
    }

    void Idle::update()
    {
        if (msgUntilMs && millis() > msgUntilMs)
        {
            msgUntilMs = 0;
            tft().fillRect(0, MSG_Y - 1, theme::W, 14, theme::BG);
        }
        if (millis() - lastCheckMs < CHECK_MS) return;
        lastCheckMs = millis();
        const winder::Settings& s = app::settings();
        if (s != shown)
        {
            shown = s;
            drawValues();
        }
    }

    void Idle::onTouch(const touch::Event& e)
    {
        switch (group.handle(e))
        {
            case BTN_START:
                // On success the screen manager switches to Running
                if (const char* err = app::start())
                {
                    tft().fillRect(0, MSG_Y - 1, theme::W, 14, theme::BG);
                    printCentered(err, 0, theme::W, MSG_Y, theme::FONT_SMALL, theme::ERR);
                    msgUntilMs = millis() + MSG_MS;
                }
                break;
            case BTN_SETUP: show(setup); break;
            default: break;
        }
    }
}
