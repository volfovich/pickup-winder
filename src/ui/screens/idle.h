#pragma once

#include "ui/screen.h"
#include "ui/widgets.h"
#include "app.h"
#include "winder.h"

namespace ui::screens
{
    // Home screen: turns, direction, RPM and the bobbin to wind; START,
    // SETUP, GUIDE.
    class Idle : public Screen
    {
     public:
        Idle();
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void drawValues();
        void followPositioning();
        bool guideWarning();
        void setGuideLabel();

        enum : uint8_t { BTN_START, BTN_SETUP, BTN_GUIDE, BTN_COUNT };
        Button buttons[BTN_COUNT];
        ButtonGroup group;
        winder::Settings shown{};
        app::TraverseSettings shownTrav{};
        uint32_t lastCheckMs = 0;
        uint32_t msgUntilMs  = 0;
        bool shownPositioning = false;
        bool shownGuideWarn   = false;
    };

    extern Idle idle;
}
