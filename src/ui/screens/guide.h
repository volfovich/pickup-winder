#pragma once

#include "ui/screen.h"
#include "ui/widgets.h"

namespace ui::screens
{
    // Wire guide: live position, jog buttons, HOME (STOP while moving), and
    // teaching the bobbin: jog until the wire meets a flange's inner face,
    // SET FLANGE 1 / 2. GO TO FLANGE moves to each in turn, to check them.
    class Guide : public Screen
    {
     public:
        Guide();
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void refresh(bool force); // position, state line, button states
        void message(const char* text, uint16_t color);

        enum : uint8_t {
            J_M10, J_M1, J_M01, J_P01, J_P1, J_P10, // jogs, toward home (-) / away (+)
            BTN_HOME, BTN_SET1, BTN_SET2, BTN_BOBBIN, BTN_GOTO, BTN_BACK, BTN_COUNT
        };
        Button buttons[BTN_COUNT];
        ButtonGroup group;

        char shownPos[16]     = "";
        char shownState[48]   = "";
        uint16_t shownEnabled = 0; // bit per button
        bool shownBusy        = false;
        uint32_t lastMs       = 0;
        uint32_t msgUntilMs   = 0;
        uint8_t gotoFlange    = 1; // GO TO's next target
    };

    extern Guide guide;
}
