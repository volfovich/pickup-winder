#pragma once

#include "ui/screen.h"

namespace ui::screens
{
    // UI-1 bring-up screen: shows raw and calibrated touch values and the
    // main-loop timing, and draws a dot wherever the screen is touched.
    class TouchTest : public Screen
    {
     public:
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void drawInfoLine(uint8_t i);

        static constexpr uint8_t INFO_LINES = 3;
        char shown[INFO_LINES][40];
        uint8_t nextLine    = 0;
        uint32_t lastInfoMs = 0;
        touch::Point lastDot{-1, -1};
        int8_t downButton   = -1;
    };

    extern TouchTest touchTest;
}
