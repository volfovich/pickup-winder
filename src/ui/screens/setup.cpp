#include "ui/screens/setup.h"
#include "app.h"
#include "config.h"
#include "ui/screens/idle.h"
#include "ui/screens/keypad.h"
#include "ui/screens/touch_test.h"
#include "ui/ui.h"

namespace ui::screens
{
    Setup setup;

    namespace
    {
        constexpr uint32_t CHECK_MS = 250;
    }

    Setup::Setup()
        : buttons{
              {8, 32, 304, 34, "Turns"},
              {8, 70, 304, 34, "RPM"},
              {8, 108, 304, 34, "Accel (RPM/s)"},
              {8, 146, 304, 34, "Direction"},
              {8, 188, 148, 46, "BACK"},
              {164, 188, 148, 46, "TOUCH TEST"},
          },
          group(buttons, BTN_COUNT)
    {
        for (uint8_t i = 0; i < 4; i++) buttons[i].value = values[i];
    }

    void Setup::load()
    {
        shown       = app::settings();
        shownLocked = winder::isRunning();
        snprintf(values[F_TURNS], sizeof(values[0]), "%lu", shown.turns);
        snprintf(values[F_RPM], sizeof(values[0]), "%lu", shown.rpm);
        snprintf(values[F_ACCEL], sizeof(values[0]), "%lu", shown.accelRpmPerS);
        snprintf(values[F_DIR], sizeof(values[0]), "%s", shown.dir == winder::Dir::CW ? "CW" : "CCW");
        for (uint8_t i = 0; i < 4; i++) buttons[i].enabled = !shownLocked;
    }

    void Setup::enter()
    {
        drawStatusBar("SETUP");
        load();
        group.reset();
        group.drawAll();
    }

    void Setup::update()
    {
        if (millis() - lastCheckMs < CHECK_MS || group.pressed() >= 0) return;
        lastCheckMs = millis();
        const winder::Settings& s = app::settings();
        if (s != shown || winder::isRunning() != shownLocked)
        {
            load();
            for (uint8_t i = 0; i < 4; i++) buttons[i].draw();
        }
    }

    void Setup::onTouch(const touch::Event& e)
    {
        const winder::Settings& s = app::settings();
        switch (group.handle(e))
        {
            case F_TURNS: keypad.open("Turns", 1, MAX_TURNS, s.turns, app::setTurns, *this); break;
            case F_RPM: keypad.open("RPM", MIN_RPM, MAX_RPM, s.rpm, app::setRpm, *this); break;
            case F_ACCEL:
                keypad.open("Accel RPM/s", MIN_ACCEL_RPM_PER_S, MAX_ACCEL_RPM_PER_S, s.accelRpmPerS, app::setAccel, *this);
                break;
            case F_DIR:
                app::setDir(s.dir == winder::Dir::CW ? winder::Dir::CCW : winder::Dir::CW);
                load();
                buttons[F_DIR].draw();
                break;
            case BTN_BACK: show(idle); break;
            case BTN_TOUCH: show(touchTest); break;
            default: break;
        }
    }
}
