#pragma once

#include "ui/screen.h"
#include "ui/widgets.h"

namespace ui::screens
{
    // A run aborted by a driver fault: what happened, where, and a way to
    // re-initialise the driver. The driver is already disabled.
    class Fault : public Screen
    {
     public:
        Fault();
        void enter() override;
        void onTouch(const touch::Event& e) override;

     private:

        enum : uint8_t { BTN_REINIT, BTN_OK, BTN_COUNT };
        Button buttons[BTN_COUNT];
        ButtonGroup group;
    };

    extern Fault fault;
}
