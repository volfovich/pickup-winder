#pragma once

#include "ui/screen.h"
#include "ui/widgets.h"
#include "winder.h"

namespace ui::screens
{
    // Home screen: current settings, START, SETUP, DRIVER.
    class Idle : public Screen
    {
     public:
        Idle();
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void drawValues();

        enum : uint8_t { BTN_START, BTN_SETUP, BTN_DRIVER, BTN_COUNT };
        Button buttons[BTN_COUNT];
        ButtonGroup group;
        winder::Settings shown{};
        uint32_t lastCheckMs = 0;
    };

    extern Idle idle;
}
