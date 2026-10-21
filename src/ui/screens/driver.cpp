#include "ui/screens/driver.h"
#include "app.h"
#include "ui/screens/diagnostics.h"
#include "ui/ui.h"
#include "winder.h"

namespace ui::screens
{
    Driver driver;

    namespace
    {
        constexpr uint32_t REFRESH_MS = 500;
        constexpr int16_t LINE_Y      = 32;
        constexpr int16_t LINE_DY     = 17;
        constexpr int16_t VALUE_X     = 76;
        constexpr int16_t VALUE_W     = theme::W - VALUE_X - 4;
        const char* const LABELS[]    = {"Driver", "Mode", "Chopper", "Current", "Now", "Motor", "Temp", "Faults", "MSCNT"};

        // Appends ", item" (or "item" first) to a fault list
        void append(char* list, size_t size, const char* item)
        {
            if (*list) strlcat(list, ", ", size);
            strlcat(list, item, size);
        }
    }

    Driver::Driver()
        : buttons{
              {8, 188, 180, 46, "RE-INIT DRIVER"},
              {196, 188, 116, 46, "BACK"},
          },
          group(buttons, BTN_COUNT)
    {
    }

    void Driver::open(tmc::Driver& d)
    {
        drv = &d;
        show(*this);
    }

    void Driver::enter()
    {
        drawStatusBar("DRIVER");
        for (uint8_t i = 0; i < LINES; i++)
        {
            printAt(LABELS[i], 8, LINE_Y + i * LINE_DY, theme::FONT_BODY, theme::MUTED);
            shown[i][0] = '\0';
        }
        drawLine(0, "reading...", theme::MUTED);
        group.reset();
        group.drawAll();
        part       = 0;
        refreshNow = true;
    }

    void Driver::drawLine(uint8_t i, const char* text, uint16_t color)
    {
        if (!strcmp(text, shown[i]) && color == shownColor[i]) return;
        strlcpy(shown[i], text, sizeof(shown[i]));
        shownColor[i] = color;

        int16_t y = LINE_Y + i * LINE_DY;
        tft().fillRect(VALUE_X, y - 2, theme::W - VALUE_X, LINE_DY, theme::BG);
        printAt(text, VALUE_X, y, fitFont(text, VALUE_W, theme::FONT_BODY), color);
    }

    void Driver::drawStatus(bool comm)
    {
        char t[48];
        const tmc::Status& s = reading;

        if (!comm)
        {
            drawLine(0, drv == &tmc::spindle ? "spindle  NO RESPONSE - 24 V off?" : "guide  NO RESPONSE - 24 V off?",
                     theme::ERR);
            for (uint8_t i = 1; i < LINES; i++) drawLine(i, "", theme::FG);
            return;
        }

        snprintf(t, sizeof(t), "%s  v0x%02X  %s", drv == &tmc::spindle ? "spindle" : "guide", s.version,
                 drv->ready() ? "configured" : "NOT CONFIGURED");
        drawLine(0, t, drv->ready() ? theme::OK : theme::WARN);

        snprintf(t, sizeof(t), "%s, %u microsteps", s.spreadCycle ? "spreadCycle" : "stealthChop", s.microsteps);
        drawLine(1, t, s.spreadCycle == !drv->config().stealthChop ? theme::FG : theme::ERR); // red: not as configured

        snprintf(t, sizeof(t), "toff %u  hstrt %u  hend %u  tbl %u", s.toff, s.hstrt, s.hend, s.tblClocks);
        drawLine(2, t, theme::FG);

        snprintf(t, sizeof(t), "%u run / %u hold mA", s.runMa, s.holdMa);
        drawLine(3, t, theme::FG);

        snprintf(t, sizeof(t), "%u mA (CS %u) %s", s.currentMa, s.csActual, s.standstill ? "standstill" : "moving");
        drawLine(4, t, theme::FG);

        drawLine(5, s.enPinOff ? "free (driver off)" : "enabled (holding)", s.enPinOff ? theme::MUTED : theme::FG);

        if (s.ot) drawLine(6, "OVER-TEMP - shut down", theme::ERR);
        else if (s.otpw) drawLine(6, "pre-warning (hot)", theme::WARN);
        else if (s.tempOver)
        {
            snprintf(t, sizeof(t), "above %u C", s.tempOver);
            drawLine(6, t, theme::WARN);
        }
        else drawLine(6, "below 120 C", theme::OK);

        // Open load (ola/olb) is left out: the datasheet says it is often
        // flagged falsely at standstill, and this screen only shows idle.
        t[0] = '\0';
        if (s.s2ga || s.s2gb) append(t, sizeof(t), "short GND");
        if (s.s2vsa || s.s2vsb) append(t, sizeof(t), "short VS");
        if (s.reset) append(t, sizeof(t), "reset - re-init");
        if (s.uvCp) append(t, sizeof(t), "undervoltage");
        if (s.drvErr) append(t, sizeof(t), "drv_err");
        drawLine(7, *t ? t : "none", *t ? theme::ERR : theme::OK);

        snprintf(t, sizeof(t), "%u  (UART ok)", s.mscnt);
        drawLine(8, t, theme::FG);
    }

    void Driver::update()
    {
        // The fault watch owns the UART while the motor turns
        if (winder::isMoving()) return;

        if (part == 0)
        {
            if (!refreshNow && millis() - cycleStartMs < REFRESH_MS) return;
            refreshNow   = false;
            cycleStartMs = millis();
        }
        if (!drv->readStatus(&reading, part))
        {
            part = 0;
            drawStatus(false);
            return;
        }
        if (++part == tmc::STATUS_READS)
        {
            part = 0;
            drawStatus(true);
        }
    }

    void Driver::onTouch(const touch::Event& e)
    {
        switch (group.handle(e))
        {
            case BTN_REINIT:
                app::reinitDriver(); // the next read shows the result
                part       = 0;
                refreshNow = true;
                break;
            case BTN_BACK: show(diagnostics); break;
            default: break;
        }
    }
}
