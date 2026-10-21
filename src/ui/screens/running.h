#pragma once

#include "ui/screen.h"
#include "ui/widgets.h"

namespace ui::screens
{
    // Live run: turn counter, progress, speed and times, and a huge STOP.
    // STOP acts on touch-down: first tap = ramped stop, a second tap while
    // ramping down = stop now. Both pause the run (count stays exact).
    class Running : public Screen
    {
     public:
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void drawStop();
        void drawInfo();

        uint32_t shownTurns = UINT32_MAX;
        bool shownStopping  = false;
        uint32_t lastFastMs = 0;
        uint32_t lastInfoMs = 0;
    };

    extern Running running;
}
