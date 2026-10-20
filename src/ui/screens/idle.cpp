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
    }

    Idle::Idle()
        : buttons{
              // START stays disabled until UI-4 adds the Running screen with its STOP button
              {8, 128, 304, 56, "START", theme::GO, false},
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

        printAt("Turns", 12, 42, theme::FONT_LABEL, theme::MUTED);
        snprintf(text, sizeof(text), "%lu", shown.turns);
        printAt(text, VALUE_X, 34, theme::FONT_LARGE, theme::FG);

        printAt("RPM", 12, 78, theme::FONT_LABEL, theme::MUTED);
        snprintf(text, sizeof(text), "%lu", shown.rpm);
        printAt(text, VALUE_X, 76, theme::FONT_VALUE, theme::FG);
        int16_t x = VALUE_X + textWidth(text, theme::FONT_VALUE) + 10;
        snprintf(text, sizeof(text), "(actual %.1f)", winder::actualRpm(shown.rpm));
        printAt(text, x, 79, theme::FONT_BODY, theme::MUTED);

        printAt("Accel", 12, 102, theme::FONT_LABEL, theme::MUTED);
        snprintf(text, sizeof(text), "%lu RPM/s", shown.accelRpmPerS);
        printAt(text, VALUE_X, 100, theme::FONT_VALUE, theme::FG);
        printAt("Dir", 232, 102, theme::FONT_LABEL, theme::MUTED);
        printAt(shown.dir == winder::Dir::CW ? "CW" : "CCW", 268, 100, theme::FONT_VALUE, theme::FG);
    }

    void Idle::update()
    {
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
            case BTN_SETUP: show(setup); break;
            default: break;
        }
    }
}
