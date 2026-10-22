#pragma once

#include "ui/screen.h"
#include "ui/widgets.h"
#include "app.h"
#include "winder.h"

namespace ui::screens
{
    // Home screen: turns, direction, RPM and the bobbin to wind; START,
    // SETUP, PRESETS, GUIDE. The title is the loaded preset's name (" *"
    // once the settings differ from it), else IDLE.
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
        void drawTitle();

        enum : uint8_t { BTN_START, BTN_SETUP, BTN_PRESETS, BTN_GUIDE, BTN_COUNT };
        Button buttons[BTN_COUNT];
        ButtonGroup group;
        winder::Settings shown{};
        app::TraverseSettings shownTrav{};
        uint32_t lastCheckMs = 0;
        uint32_t msgUntilMs  = 0;
        bool shownPositioning = false;
        bool shownGuideWarn   = false;
        int8_t shownPreset    = -1;
        bool shownModified    = false;
    };

    extern Idle idle;
}
