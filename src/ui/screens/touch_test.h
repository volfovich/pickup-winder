#pragma once

#include "ui/screen.h"
#include "ui/widgets.h"

namespace ui::screens
{
    // Touch check screen: shows raw and calibrated touch values and the
    // main-loop timing, and draws a dot wherever the screen is touched.
    class TouchTest : public Screen
    {
     public:
        TouchTest();
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void drawInfoLine(uint8_t i);

        enum : uint8_t { BTN_CLEAR, BTN_CALIBRATE, BTN_BACK, BTN_COUNT };
        Button buttons[BTN_COUNT];
        ButtonGroup group;

        static constexpr uint8_t INFO_LINES = 3;
        char shown[INFO_LINES][40];
        uint8_t nextLine    = 0;
        uint32_t lastInfoMs = 0;
        touch::Point lastDot{-1, -1};
    };

    extern TouchTest touchTest;
}
