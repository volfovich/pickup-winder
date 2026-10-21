#include "ui/screens/paused.h"
#include "app.h"
#include "config.h"
#include "winder.h"

namespace ui::screens
{
    Paused paused;

    namespace
    {
        constexpr int16_t MSG_Y          = 124;
        constexpr uint32_t CONFIRM_MS    = 3000;
        const char* const FINISH_LABEL   = "FINISH";
        const char* const CONFIRM_LABEL  = "TAP AGAIN";
    }

    Paused::Paused()
        : buttons{
              {8, 148, 148, 86, "RESUME", theme::GO},
              {164, 148, 148, 86, FINISH_LABEL},
          },
          group(buttons, BTN_COUNT)
    {
    }

    void Paused::enter()
    {
        char left[16], text[48];
        drawStatusBar("PAUSED");
        drawTurnCounter(winder::stepsDone() / STEPS_PER_REV, winder::targetSteps() / STEPS_PER_REV, theme::WARN);
        drawProgressBar(winder::stepsDone(), winder::targetSteps(), theme::WARN);

        uint32_t leftTurns = (winder::targetSteps() - winder::stepsDone() + STEPS_PER_REV - 1) / STEPS_PER_REV;
        snprintf(text, sizeof(text), "%.2f turns done, %s to go", winder::turnsDone(),
                 fmtThousands(leftTurns, left, sizeof(left)));
        drawMessage(text, theme::MUTED);

        confirmUntilMs           = 0;
        buttons[BTN_FINISH].label = FINISH_LABEL;
        group.reset();
        group.drawAll();
    }

    void Paused::drawMessage(const char* text, uint16_t color)
    {
        tft().fillRect(0, MSG_Y - 2, theme::W, 18, theme::BG);
        printCentered(text, 0, theme::W, MSG_Y, theme::FONT_BODY, color);
    }

    void Paused::update()
    {
        // FINISH confirmation times out
        if (confirmUntilMs && millis() > confirmUntilMs)
        {
            confirmUntilMs            = 0;
            buttons[BTN_FINISH].label = FINISH_LABEL;
            buttons[BTN_FINISH].color = theme::BUTTON;
            buttons[BTN_FINISH].draw();
        }
    }

    void Paused::onTouch(const touch::Event& e)
    {
        switch (group.handle(e))
        {
            case BTN_RESUME:
                // On success the screen manager switches to Running
                if (const char* err = app::resume()) drawMessage(err, theme::ERR);
                break;
            case BTN_FINISH:
                if (!confirmUntilMs)
                {
                    confirmUntilMs            = millis() + CONFIRM_MS;
                    buttons[BTN_FINISH].label = CONFIRM_LABEL;
                    buttons[BTN_FINISH].color = theme::WARN;
                    buttons[BTN_FINISH].draw();
                }
                else if (const char* err = app::finish())
                {
                    drawMessage(err, theme::ERR);
                }
                break;
            default: break;
        }
    }
}
