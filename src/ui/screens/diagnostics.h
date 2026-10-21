#pragma once

#include "ui/screen.h"
#include "ui/widgets.h"

namespace ui::screens
{
    // Diagnostics: firmware version, loop timing, both drivers, the home
    // switch and uptime, with the driver register screens and the touch test.
    class Diagnostics : public Screen
    {
     public:
        Diagnostics();
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void drawLine(uint8_t i, const char* text, uint16_t color);
        void refresh();

        enum : uint8_t { BTN_SPINDLE, BTN_GUIDE, BTN_TOUCH, BTN_BACK, BTN_COUNT };
        Button buttons[BTN_COUNT];
        ButtonGroup group;

        static constexpr uint8_t LINES = 6;
        char shown[LINES][48];
        uint16_t shownColor[LINES];
        uint32_t lastMs = 0;
    };

    extern Diagnostics diagnostics;
}
