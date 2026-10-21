#pragma once

#include "ui/screen.h"
#include "ui/widgets.h"
#include "winder.h"

namespace ui::screens
{
    // Settings: tap a value to edit it with the keypad; direction toggles.
    // Locked (greyed) while a run is in progress.
    class Setup : public Screen
    {
     public:
        Setup();
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void load(); // settings + lock state -> button values

        enum : uint8_t { F_TURNS, F_RPM, F_ACCEL, F_DIR, BTN_BACK, BTN_BOBBIN, BTN_DIAG, BTN_COUNT };
        Button buttons[BTN_COUNT];
        ButtonGroup group;
        char values[4][16];
        winder::Settings shown{};
        bool shownLocked     = false;
        uint32_t lastCheckMs = 0;
    };

    extern Setup setup;
}
