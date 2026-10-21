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
        constexpr int16_t MSG_Y = 154; // two small lines above the buttons
        constexpr int16_t MSG_H = 32;
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
        printAt("Driver disabled. Fix the cause, then re-init.", 12, y + 22, theme::FONT_SMALL, theme::MUTED);

        group.reset();
        group.drawAll();
    }

    void Fault::onTouch(const touch::Event& e)
    {
        switch (group.handle(e))
        {
            case BTN_REINIT:
                drawMessage("configuring driver...", MSG_Y, MSG_H, theme::MUTED);
                if (const char* err = app::reinitDriver()) drawMessage(err, MSG_Y, MSG_H, theme::ERR);
                else drawMessage("driver configured and verified", MSG_Y, MSG_H, theme::OK);
                break;
            case BTN_OK: show(idle); break;
            default: break;
        }
    }
}
