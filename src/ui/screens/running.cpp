#include "ui/screens/running.h"
#include "app.h"
#include "config.h"
#include "winder.h"

namespace ui::screens
{
    Running running;

    namespace
    {
        constexpr uint32_t FAST_MS = 100; // counter + progress
        constexpr uint32_t INFO_MS = 500; // speed + times
        constexpr int16_t INFO_Y   = 124;
        const Button STOP_BUTTON{8, 146, 304, 88, "STOP", theme::ERR};
    }

    void Running::enter()
    {
        drawStatusBar("WINDING");
        shownTurns    = UINT32_MAX;
        shownStopping = winder::stopping();
        lastFastMs = lastInfoMs = 0;
        drawStop();
        update();
    }

    void Running::drawStop()
    {
        Button b = STOP_BUTTON;
        if (shownStopping)
        {
            b.color = theme::WARN;
            b.label = "STOPPING...\nTAP AGAIN TO STOP NOW";
        }
        b.draw();
    }

    void Running::drawInfo()
    {
        char rpm[24], elapsed[16], eta[24], t[12];
        const winder::Settings& s = winder::runSettings();

        snprintf(rpm, sizeof(rpm), "%.1f rpm", winder::measuredRpm());
        snprintf(elapsed, sizeof(elapsed), "%s", fmtDuration(winder::windingMs(), t, sizeof(t)));

        // Time left at the timer's actual cruise speed (ramps are short)
        uint32_t left    = winder::targetSteps() - winder::stepsDone();
        uint32_t leftMs  = (uint32_t)(left / (winder::actualRpm(s.rpm) * STEPS_PER_REV / 60.0f) * 1000.0f);
        snprintf(eta, sizeof(eta), "left %s", fmtDuration(leftMs, t, sizeof(t)));

        tft().fillRect(0, INFO_Y - 2, theme::W, 18, theme::BG);
        printAt(rpm, 8, INFO_Y, theme::FONT_BODY, theme::FG);
        printCentered(elapsed, 0, theme::W, INFO_Y, theme::FONT_BODY, theme::MUTED);
        printRight(eta, theme::W - 8, INFO_Y, theme::FONT_BODY, theme::MUTED);
    }

    void Running::update()
    {
        uint32_t now = millis();
        if (now - lastFastMs >= FAST_MS)
        {
            lastFastMs     = now;
            uint32_t turns = winder::stepsDone() / STEPS_PER_REV;
            if (turns != shownTurns)
            {
                shownTurns = turns;
                drawTurnCounter(turns, winder::targetSteps() / STEPS_PER_REV, theme::FG);
            }
            drawProgressBar(winder::stepsDone(), winder::targetSteps(), theme::ACCENT);
        }
        if (now - lastInfoMs >= INFO_MS)
        {
            lastInfoMs = now;
            drawInfo();
        }
        if (winder::stopping() != shownStopping)
        {
            shownStopping = winder::stopping();
            drawStop();
        }
    }

    void Running::onTouch(const touch::Event& e)
    {
        if (e.type != touch::Type::Down || !STOP_BUTTON.contains(e.pos)) return;
        if (winder::stopping()) app::estop();
        else app::stop();
        shownStopping = true;
        drawStop();
    }
}
