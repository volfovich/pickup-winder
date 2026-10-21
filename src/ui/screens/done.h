#pragma once

#include "ui/screen.h"
#include "ui/widgets.h"

namespace ui::screens
{
    // Result of a run that ended normally or early: turns, steps, the driver
    // pulse check and the winding time.
    class Done : public Screen
    {
     public:
        Done();
        void enter() override;
        void onTouch(const touch::Event& e) override;

     private:
        enum : uint8_t { BTN_OK, BTN_SETUP, BTN_COUNT };
        Button buttons[BTN_COUNT];
        ButtonGroup group;
    };

    extern Done done;
}
