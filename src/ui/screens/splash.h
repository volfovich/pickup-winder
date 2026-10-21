#pragma once

#include "ui/screen.h"
#include "ui/widgets.h"

namespace ui::screens
{
    // Boot screen: name, version and a pickup bobbin that winds itself,
    // then the driver state. Leaves for Idle (or touch calibration on first
    // boot) after a moment or on a tap.
    class Splash : public Screen
    {
     public:
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void leave();
        void drawStatus();

        uint32_t startMs    = 0;
        uint8_t rowsDrawn   = 0;
        int8_t shownStatus  = -1;
    };

    extern Splash splash;
}
