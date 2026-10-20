#include "ui/screens/calibrate.h"
#include "ui/screens/idle.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace ui::screens
{
    Calibrate calibrate;

    namespace
    {
        // Three solve points spread over the screen, plus a check point
        // outside their triangle so the check also tests extrapolation.
        constexpr touch::Point TARGETS[4] = {{30, 30}, {290, 120}, {160, 210}, {30, 210}};
        constexpr int16_t CHECK_TOLERANCE_PX = 12;
        constexpr uint32_t DONE_SHOW_MS      = 1500;

        void drawCross(touch::Point p, uint16_t color)
        {
            auto& d = tft();
            d.drawFastHLine(p.x - 12, p.y, 25, color);
            d.drawFastVLine(p.x, p.y - 12, 25, color);
            d.drawCircle(p.x, p.y, 6, color);
        }

        void message(const char* line1, const char* line2, uint16_t color)
        {
            auto& d = tft();
            d.fillRect(50, 80, 220, 60, theme::BG);
            printCentered(line1, 50, 220, 88, theme::FONT_BODY, color);
            if (line2) printCentered(line2, 50, 220, 112, theme::FONT_BODY, theme::MUTED);
        }
    }

    void Calibrate::enter()
    {
        step        = 0;
        doneMs      = 0;
        waitRelease = touch::isPressed(); // e.g. entered by holding the screen at boot
        touch::reloadCalibration();
        drawStep();
    }

    void Calibrate::drawStep()
    {
        static char line[40];
        if (step > 0) drawCross(TARGETS[step - 1], theme::BG);
        drawCross(TARGETS[step], theme::ACCENT);
        snprintf(line, sizeof(line), "Tap the centre of the cross  %u/4", step + 1);
        message(step < 3 ? "TOUCH CALIBRATION" : "CHECK", line, theme::FG);
    }

    void Calibrate::restart(const char* why)
    {
        touch::reloadCalibration();
        drawCross(TARGETS[step], theme::BG);
        step = 0;
        drawStep();
        message(why, "Start again: tap the cross  1/4", theme::WARN);
    }

    void Calibrate::onTouch(const touch::Event& e)
    {
        if (e.type != touch::Type::Up || doneMs) return;
        if (waitRelease)
        {
            waitRelease = false;
            return;
        }

        if (step < 3)
        {
            raw[step] = e.raw;
            if (step == 2 && !touch::solveCalibration(raw, TARGETS))
            {
                restart("Touches too close together");
                return;
            }
            step++;
            drawStep();
            return;
        }

        // Check point: the new mapping must put this tap on the cross
        touch::Point p = touch::map(e.raw.x, e.raw.y);
        int16_t dx = p.x - TARGETS[3].x, dy = p.y - TARGETS[3].y;
        if (dx * dx + dy * dy > CHECK_TOLERANCE_PX * CHECK_TOLERANCE_PX)
        {
            static char why[40];
            snprintf(why, sizeof(why), "Check missed by %d px", (int)sqrtf(dx * dx + dy * dy));
            restart(why);
            return;
        }

        touch::saveCalibration();
        drawCross(TARGETS[3], theme::BG);
        message("Calibration saved", nullptr, theme::OK);
        doneMs = millis();
    }

    void Calibrate::update()
    {
        if (doneMs && millis() - doneMs >= DONE_SHOW_MS) show(idle);
    }
}
