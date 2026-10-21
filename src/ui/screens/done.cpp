#include "ui/screens/done.h"
#include "config.h"
#include "ui/screens/idle.h"
#include "ui/screens/setup.h"
#include "ui/ui.h"
#include "winder.h"

namespace ui::screens
{
    Done done;

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
        char a[16], b[16], line[48];
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
        printCentered(headline, 0, theme::W, 36, theme::FONT_LARGE, color);

        // Whole turns, plus the fraction when the run ended between turns
        if (n % STEPS_PER_REV)
            snprintf(line, sizeof(line), "turns  %.3f / %s", (double)n / STEPS_PER_REV,
                     fmtThousands(target / STEPS_PER_REV, b, sizeof(b)));
        else
            snprintf(line, sizeof(line), "turns  %s / %s", fmtThousands(n / STEPS_PER_REV, a, sizeof(a)),
                     fmtThousands(target / STEPS_PER_REV, b, sizeof(b)));
        printAt(line, 16, 80, theme::FONT_VALUE, theme::FG);

        snprintf(line, sizeof(line), "steps  %s", fmtThousands(n, a, sizeof(a)));
        printAt(line, 16, 106, theme::FONT_BODY, theme::MUTED);

        int16_t off;
        winder::PulseCheck pc = winder::pulseCheck(&off);
        uint16_t pcColor      = theme::OK;
        switch (pc)
        {
            case winder::PulseCheck::Ok: snprintf(line, sizeof(line), "driver pulse check  OK"); break;
            case winder::PulseCheck::Mismatch:
                snprintf(line, sizeof(line), "driver pulse check  MISMATCH (%+d)", off);
                pcColor = theme::ERR;
                break;
            default:
                snprintf(line, sizeof(line), "driver pulse check  unavailable");
                pcColor = theme::WARN;
                break;
        }
        printAt(line, 16, 128, theme::FONT_BODY, pcColor);

        snprintf(line, sizeof(line), "winding time  %s", fmtDuration(winder::windingMs(), a, sizeof(a)));
        printAt(line, 16, 150, theme::FONT_BODY, theme::MUTED);

        group.reset();
        group.drawAll();
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
