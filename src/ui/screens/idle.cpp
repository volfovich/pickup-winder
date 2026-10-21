#include "ui/screens/idle.h"
#include "app.h"
#include "ui/screens/driver.h"
#include "ui/screens/setup.h"
#include "ui/ui.h"

namespace ui::screens
{
    Idle idle;

    namespace
    {
        constexpr uint32_t CHECK_MS = 250; // pick up changes made from serial
        constexpr int16_t VALUE_X   = 96;
        constexpr int16_t MSG_Y     = 112; // between the settings and START
        constexpr int16_t MSG_H     = 16;
        constexpr uint32_t MSG_MS   = 5000;
    }

    Idle::Idle()
        : buttons{
              {8, 128, 304, 56, "START", theme::GO},
              {8, 192, 148, 42, "SETUP"},
              {164, 192, 148, 42, "DRIVER"},
          },
          group(buttons, BTN_COUNT)
    {
    }

    void Idle::enter()
    {
        drawStatusBar("IDLE");
        shown            = app::settings();
        shownPositioning = false;
        msgUntilMs       = 0;
        buttons[BTN_START].label = "START";
        buttons[BTN_START].color = theme::GO;
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

    // With the traverse on, START first moves the guide to the start
    // position; meanwhile START becomes CANCEL. On success the screen
    // manager switches to Running.
    void Idle::followPositioning()
    {
        bool now = app::positioning();
        if (now == shownPositioning) return;
        shownPositioning           = now;
        buttons[BTN_START].label = now ? "CANCEL" : "START";
        buttons[BTN_START].color = now ? theme::WARN : theme::GO;
        buttons[BTN_START].draw();
        if (now) drawMessage("moving the guide to the start position...", MSG_Y, MSG_H, theme::MUTED);
        else drawMessage(app::startError(), MSG_Y, MSG_H, theme::ERR);
        msgUntilMs = now ? 0 : millis() + MSG_MS;
    }

    void Idle::update()
    {
        followPositioning();
        if (msgUntilMs && millis() > msgUntilMs)
        {
            msgUntilMs = 0;
            drawMessage(nullptr, MSG_Y, MSG_H, theme::BG);
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
                if (app::positioning()) app::stop();
                else if (const char* err = app::start())
                {
                    drawMessage(err, MSG_Y, MSG_H, theme::ERR);
                    msgUntilMs = millis() + MSG_MS;
                }
                break;
            case BTN_SETUP: show(setup); break;
            case BTN_DRIVER: show(driver); break;
            default: break;
        }
    }
}
