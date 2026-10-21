#include "ui/ui.h"
#include "config.h"
#include "ui/screen.h"
#include "ui/screens/calibrate.h"
#include "ui/screens/done.h"
#include "ui/screens/fault.h"
#include "ui/screens/paused.h"
#include "ui/screens/running.h"
#include "ui/screens/splash.h"
#include "ui/theme.h"
#include "ui/touch.h"
#include "ui/widgets.h"
#include "winder.h"

namespace ui
{
    namespace
    {
        // Screens draw into a RAM frame buffer (fast, flicker-free); a changed
        // frame is sent to the display by DMA in the background. A full
        // transfer takes ~41 ms at 30 MHz SPI, so frames are capped at 10/s,
        // which leaves the shared SPI bus free for touch most of the time.
        ILI9341_t3n display(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
        bool dirty                     = false;
        uint32_t lastFrameMs           = 0;
        constexpr uint32_t MIN_FRAME_MS = 100;

        Screen* current = nullptr;

        constexpr uint32_t BOOT_HOLD_MS = 3000;
        bool bootHoldArmed              = false;
        uint32_t bootMs                 = 0;
        bool bootDone                   = false;

        uint32_t loopMax      = 0;
        uint32_t loopMaxShown = 0;
        uint32_t loopWindowMs = 0;

        // The run state drives the screen: whatever started, paused or ended a
        // run (this screen, the serial console, a fault), the matching screen
        // comes up — so STOP is always on screen while the motor turns.
        winder::State shownRunState = winder::State::Idle;

        void followRunState()
        {
            winder::State s = winder::state();
            if (s == shownRunState) return;
            shownRunState = s;
            switch (s)
            {
                case winder::State::Running: show(screens::running); break;
                case winder::State::Paused: show(screens::paused); break;
                case winder::State::Done:
                    show(winder::lastOutcome() == winder::Outcome::Faulted ? (Screen&)screens::fault
                                                                           : (Screen&)screens::done);
                    break;
                case winder::State::Idle: break;
            }
        }

        bool busBusy()
        {
            return display.asyncUpdateActive();
        }

        void pushFrame()
        {
            if (!dirty || busBusy() || millis() - lastFrameMs < MIN_FRAME_MS) return;
            dirty       = false;
            lastFrameMs = millis();
            display.updateScreenAsync();
        }
    }

    ILI9341_t3n& tft()
    {
        dirty = true;
        return display;
    }

    void begin()
    {
        display.begin();
        display.setRotation(DISPLAY_ROTATION);
        if (!display.useFrameBuffer(true)) Serial.println("UI: frame buffer allocation FAILED");
        display.setTextWrap(false); // too-wide text is clipped, never wrapped onto what is below
        touch::begin(busBusy);

        bootMs        = millis();
        bootHoldArmed = touch::calibrated() && touch::touchedNow();
        show(screens::splash);
        display.updateScreen(); // first frame synchronously
        dirty = false;
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
        followRunState();
        if (current) current->update();
        refreshStatusBar();
        pushFrame();
    }

    void markBooted()
    {
        bootDone = true;
    }

    bool booted()
    {
        return bootDone;
    }

    void show(Screen& s)
    {
        tft().fillScreen(theme::BG);
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
