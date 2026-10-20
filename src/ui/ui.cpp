#include "ui/ui.h"
#include "config.h"
#include "ui/screen.h"
#include "ui/screens/calibrate.h"
#include "ui/screens/touch_test.h"
#include "ui/theme.h"
#include "ui/touch.h"

namespace ui
{
    namespace
    {
        ILI9341_t3 display(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
        Screen* current = nullptr;

        constexpr uint32_t BOOT_HOLD_MS = 3000;
        bool bootHoldArmed              = false;
        uint32_t bootMs                 = 0;

        uint32_t loopMax       = 0;
        uint32_t loopMaxShown  = 0;
        uint32_t loopWindowMs  = 0;
    }

    ILI9341_t3& tft()
    {
        return display;
    }

    void begin()
    {
        display.begin();
        display.setRotation(DISPLAY_ROTATION);
        display.fillScreen(theme::BG);
        touch::begin();

        bootMs        = millis();
        bootHoldArmed = touch::calibrated() && touch::touchedNow();
        if (!touch::calibrated()) startCalibration();
        else show(screens::touchTest);
    }

    void update()
    {
        touch::update();

        // Holding the screen for 3 s right after boot forces a recalibration
        if (bootHoldArmed)
        {
            if (!touch::touchedNow()) bootHoldArmed = false;
            else if (millis() - bootMs >= BOOT_HOLD_MS)
            {
                bootHoldArmed = false;
                startCalibration();
            }
        }

        touch::Event e;
        while (current && touch::pollEvent(&e)) current->onTouch(e);
        if (current) current->update();
    }

    void show(Screen& s)
    {
        display.fillScreen(theme::BG);
        current = &s;
        s.enter();
    }

    void startCalibration()
    {
        show(screens::calibrate);
    }

    void noteLoopTime(uint32_t us)
    {
        if (us > loopMax) loopMax = us;
        if (millis() - loopWindowMs >= 1000)
        {
            loopWindowMs = millis();
            loopMaxShown = loopMax;
            loopMax      = 0;
        }
    }

    uint32_t loopMaxUs()
    {
        return loopMaxShown;
    }
}
