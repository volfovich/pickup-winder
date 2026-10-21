#include "ui/screens/splash.h"
#include "config.h"
#include "tmc_driver.h"
#include "ui/screens/idle.h"
#include "ui/ui.h"

namespace ui::screens
{
    Splash splash;

    namespace
    {
        constexpr uint32_t FILL_MS   = 1500; // the coil winds up in this time
        constexpr uint32_t SPLASH_MS = 2500;

        // Pickup bobbin, front view: two flanges, six pole pieces through
        // them, and the coil window between the flanges.
        constexpr int16_t FLANGE_X = 64, FLANGE_W = 192, FLANGE_H = 8;
        constexpr int16_t TOP_Y = 32, COIL_Y = TOP_Y + FLANGE_H, COIL_H = 56, BOTTOM_Y = COIL_Y + COIL_H;
        constexpr int16_t COIL_X = 80, COIL_W = 160;
        constexpr int16_t POLE_W = 8, POLE_DX = 26, POLE_TOP = 24, POLE_BOTTOM = BOTTOM_Y + FLANGE_H + 6;
        constexpr uint8_t ROWS   = COIL_H / 2; // wire layers, 2 px each

        constexpr uint16_t FLANGE = theme::rgb(55, 55, 60);
        constexpr uint16_t POLE   = theme::rgb(190, 190, 200);

        constexpr int16_t TITLE_Y   = 122;
        constexpr int16_t VERSION_Y = 158;
        constexpr int16_t FOOTER_Y  = 186;
        constexpr int16_t STATUS_Y  = 210, STATUS_H = 18;

        enum : int8_t { STATUS_NONE, STATUS_HOLD, STATUS_READY, STATUS_NOT_READY };

        void drawBobbin()
        {
            auto& d = tft();
            for (uint8_t i = 0; i < 6; i++)
            {
                int16_t x = 160 + (2 * i - 5) * POLE_DX / 2 - POLE_W / 2;
                d.fillRect(x, POLE_TOP, POLE_W, POLE_BOTTOM - POLE_TOP, POLE);
            }
            d.fillRect(FLANGE_X, TOP_Y, FLANGE_W, FLANGE_H, FLANGE);
            d.fillRect(FLANGE_X, BOTTOM_Y, FLANGE_W, FLANGE_H, FLANGE);
        }

        // Layer i of the coil, laid from the bottom flange up
        void drawRow(uint8_t i)
        {
            int16_t y = BOTTOM_Y - 2 * (i + 1);
            tft().fillRect(COIL_X, y, COIL_W, 1, theme::COPPER);
            tft().fillRect(COIL_X, y + 1, COIL_W, 1, theme::COPPER_DK);
        }
    }

    void Splash::enter()
    {
        startMs     = millis();
        rowsDrawn   = 0;
        shownStatus = -1;
        drawBobbin();

        // "Pickup Winder " in white, "NG" in copper, centred together
        auto& d          = tft();
        const auto& f    = theme::FONT_ENTRY;
        int16_t x        = (theme::W - textWidth(FIRMWARE_NAME, f)) / 2;
        const char* name = FIRMWARE_NAME;
        const char* ng   = strrchr(name, ' ');
        char first[24];
        strlcpy(first, name, min<size_t>(ng - name + 2, sizeof(first)));
        printAt(first, x, TITLE_Y, f, theme::FG);
        printAt(ng + 1, d.getCursorX(), TITLE_Y, f, theme::COPPER);

        printCentered("version " FIRMWARE_VERSION, 0, theme::W, VERSION_Y, theme::FONT_LABEL, theme::ACCENT);
        printCentered("Teensy 4.0  -  TMC2209  -  NEMA 17", 0, theme::W, FOOTER_Y, theme::FONT_SMALL, theme::MUTED);
    }

    void Splash::drawStatus()
    {
        int8_t s = STATUS_NONE;
        if (touch::touchedNow() && touch::calibrated()) s = STATUS_HOLD;
        else if (ui::booted()) s = tmc::ready() ? STATUS_READY : STATUS_NOT_READY;
        if (s == shownStatus) return;
        shownStatus = s;

        switch (s)
        {
            case STATUS_HOLD: drawMessage("keep holding to recalibrate touch", STATUS_Y, STATUS_H, theme::WARN); break;
            case STATUS_READY: drawMessage("driver ready", STATUS_Y, STATUS_H, theme::OK); break;
            case STATUS_NOT_READY: drawMessage("driver not ready - check 24 V", STATUS_Y, STATUS_H, theme::WARN); break;
            default: drawMessage(nullptr, STATUS_Y, STATUS_H, theme::BG); break;
        }
    }

    void Splash::update()
    {
        uint32_t elapsed = millis() - startMs;
        uint8_t rows     = min<uint32_t>(ROWS, elapsed * ROWS / FILL_MS);
        while (rowsDrawn < rows) drawRow(rowsDrawn++);
        drawStatus();

        // Holding the screen at boot leads to recalibration (ui.cpp), so wait
        if (ui::booted() && elapsed >= SPLASH_MS && !touch::touchedNow()) leave();
    }

    void Splash::onTouch(const touch::Event& e)
    {
        if (e.type == touch::Type::Up && ui::booted()) leave();
    }

    void Splash::leave()
    {
        if (touch::calibrated()) show(idle);
        else startCalibration();
    }
}
