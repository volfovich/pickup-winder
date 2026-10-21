#include "ui/screens/fault.h"
#include "app.h"
#include "config.h"
#include "ui/screens/idle.h"
#include "ui/ui.h"
#include "winder.h"

namespace ui::screens
{
    Fault fault;

    namespace
    {
        constexpr int16_t MSG_Y = 160;
    }

    Fault::Fault()
        : buttons{
              {8, 188, 180, 46, "RE-INIT DRIVER"},
              {196, 188, 116, 46, "OK"},
          },
          group(buttons, BTN_COUNT)
    {
    }

    void Fault::enter()
    {
        char a[16], line[48];
        auto& d = tft();
        d.fillRect(0, 0, theme::W, theme::STATUS_H, theme::ERR);
        printAt("FAULT", 8, 7, theme::FONT_TITLE, theme::FG);
        printRight("count unreliable", theme::W - 8, 7, theme::FONT_TITLE, theme::FG);

        const char* why = winder::faultReason() ? winder::faultReason() : "driver fault";
        uint8_t lines   = printWrapped(why, 12, 36, theme::W - 24, 22, theme::FONT_VALUE, theme::ERR);

        int16_t y = 40 + lines * 22;
        snprintf(line, sizeof(line), "stopped at %.2f / %s turns", winder::turnsDone(),
                 fmtThousands(winder::targetSteps() / STEPS_PER_REV, a, sizeof(a)));
        printAt(line, 12, y, theme::FONT_BODY, theme::FG);
        printAt("The driver is disabled. Fix the cause, then re-init.", 12, y + 22, theme::FONT_SMALL, theme::MUTED);

        group.reset();
        group.drawAll();
    }

    void Fault::drawMessage(const char* text, uint16_t color)
    {
        tft().fillRect(0, MSG_Y - 2, theme::W, 24, theme::BG);
        printWrapped(text, 12, MSG_Y, theme::W - 24, 13, theme::FONT_SMALL, color);
    }

    void Fault::onTouch(const touch::Event& e)
    {
        switch (group.handle(e))
        {
            case BTN_REINIT:
                drawMessage("configuring driver...", theme::MUTED);
                if (const char* err = app::reinitDriver()) drawMessage(err, theme::ERR);
                else drawMessage("driver configured and verified", theme::OK);
                break;
            case BTN_OK: show(idle); break;
            default: break;
        }
    }
}
