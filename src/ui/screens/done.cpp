#include "ui/screens/done.h"
#include "app.h"
#include "config.h"
#include "ui/screens/idle.h"
#include "ui/screens/setup.h"
#include "ui/ui.h"
#include "winder.h"

namespace ui::screens
{
    Done done;

    namespace
    {
        constexpr int16_t SPINDLE_CHECK_Y = 106;
        constexpr int16_t GUIDE_CHECK_Y   = 128;

        void drawPulseCheck(const char* name, winder::PulseCheck pc, int16_t offset, int16_t y)
        {
            char line[48];
            uint16_t color = theme::OK;
            switch (pc)
            {
                case winder::PulseCheck::Ok: snprintf(line, sizeof(line), "%s pulse check  OK", name); break;
                case winder::PulseCheck::Mismatch:
                    snprintf(line, sizeof(line), "%s pulse check  MISMATCH (%+d)", name, offset);
                    color = theme::ERR;
                    break;
                case winder::PulseCheck::Pending:
                    snprintf(line, sizeof(line), "%s pulse check  ...", name);
                    color = theme::MUTED;
                    break;
                default:
                    snprintf(line, sizeof(line), "%s pulse check  unavailable", name);
                    color = theme::WARN;
                    break;
            }
            printAt(line, 16, y, theme::FONT_BODY, color);
        }
    }

    Done::Done()
        : buttons{
              {8, 188, 148, 46, "OK", theme::GO},
              {164, 188, 148, 46, "SETUP"},
          },
          group(buttons, BTN_COUNT)
    {
    }

    void Done::enter()
    {
        char a[16], b[16], line[80];
        drawStatusBar("DONE");

        uint32_t n      = winder::stepsDone();
        uint32_t target = winder::targetSteps();
        bool exact      = n == target;

        const char* headline = "COMPLETE";
        uint16_t color       = theme::OK;
        if (winder::lastOutcome() == winder::Outcome::EndedEarly)
        {
            headline = "ENDED EARLY";
            color    = theme::WARN;
        }
        else if (!exact)
        {
            headline = "COUNT MISMATCH";
            color    = theme::ERR;
        }
        printFit(headline, 8, theme::W - 16, 36, theme::FONT_LARGE, color);

        // Whole turns, plus the fraction when the run ended between turns
        if (n % STEPS_PER_REV)
            snprintf(line, sizeof(line), "turns  %.3f / %s", (double)n / STEPS_PER_REV,
                     fmtThousands(target / STEPS_PER_REV, b, sizeof(b)));
        else
            snprintf(line, sizeof(line), "turns  %s / %s", fmtThousands(n / STEPS_PER_REV, a, sizeof(a)),
                     fmtThousands(target / STEPS_PER_REV, b, sizeof(b)));
        printAt(line, 16, 80, theme::FONT_VALUE, theme::FG);

        // Driver pulse checks: did each driver receive every step sent?
        int16_t off;
        if (app::traverseRun())
        {
            // No room for the steps line: the turns above show the fraction
            drawPulseCheck("spindle", winder::pulseCheck(&off), off, SPINDLE_CHECK_Y);
            winder::PulseCheck guide = app::traversePulseCheck(&off);
            drawPulseCheck("guide", guide, off, GUIDE_CHECK_Y);
            guidePending = guide == winder::PulseCheck::Pending;
        }
        else
        {
            snprintf(line, sizeof(line), "steps  %s", fmtThousands(n, a, sizeof(a)));
            printAt(line, 16, SPINDLE_CHECK_Y, theme::FONT_BODY, theme::MUTED);
            drawPulseCheck("driver", winder::pulseCheck(&off), off, GUIDE_CHECK_Y);
            guidePending = false;
        }

        snprintf(line, sizeof(line), "winding time  %s", fmtDuration(winder::windingMs(), a, sizeof(a)));
        printAt(line, 16, 150, theme::FONT_BODY, theme::MUTED);
        if (app::driverHot()) printAt("driver got hot - lower the current", 16, 169, theme::FONT_BODY, theme::WARN);

        group.reset();
        group.drawAll();
    }

    void Done::update()
    {
        if (!guidePending) return;
        int16_t off;
        winder::PulseCheck guide = app::traversePulseCheck(&off);
        if (guide == winder::PulseCheck::Pending) return;
        guidePending = false;
        tft().fillRect(16, GUIDE_CHECK_Y - 2, theme::W - 16, 20, theme::BG);
        drawPulseCheck("guide", guide, off, GUIDE_CHECK_Y);
    }

    void Done::onTouch(const touch::Event& e)
    {
        switch (group.handle(e))
        {
            case BTN_OK: show(idle); break;
            case BTN_SETUP: show(setup); break;
            default: break;
        }
    }
}
