#pragma once

#include "app.h"
#include "ui/screen.h"
#include "ui/widgets.h"

namespace ui::screens
{
    // Presets, from Idle → PRESETS: the 10 slots (tap to select), what the
    // selected one holds, and LOAD / SAVE (the current settings, named on
    // the keyboard) / DELETE. Overwriting a slot and deleting one need a
    // second tap. Locked while a run is in progress.
    class Presets : public Screen
    {
     public:
        Presets();
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void load(); // slot labels, selection, lock state
        void drawInfo(const char* message = nullptr, uint16_t color = theme::MUTED);
        void redraw();
        void cancelConfirm();
        bool confirm(uint8_t button); // first tap arms, second (within 3 s) returns true

        enum : uint8_t { BTN_BACK = app::PRESET_SLOTS, BTN_LOAD, BTN_SAVE, BTN_DELETE, BTN_COUNT };
        Button buttons[BTN_COUNT];
        ButtonGroup group;
        char labels[app::PRESET_SLOTS][24];
        uint8_t selected      = 0;
        int8_t confirming     = -1; // button waiting for its second tap
        uint32_t confirmUntil = 0;
        uint32_t messageUntil = 0;
        bool shownLocked      = false;
        int8_t shownActive    = -2;
        uint32_t lastCheckMs  = 0;
    };

    extern Presets presets;
}
