#pragma once

#include "ui/screen.h"
#include "ui/widgets.h"

namespace ui::screens
{
    // A stopped run with turns left: RESUME winds the rest (count stays
    // exact), FINISH ends it early — FINISH asks for a second tap.
    class Paused : public Screen
    {
     public:
        Paused();
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void drawMessage(const char* text, uint16_t color);

        enum : uint8_t { BTN_RESUME, BTN_FINISH, BTN_COUNT };
        Button buttons[BTN_COUNT];
        ButtonGroup group;
        uint32_t confirmUntilMs = 0; // FINISH armed until then
    };

    extern Paused paused;
}
