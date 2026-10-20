#pragma once

#include "ui/touch.h"

namespace ui
{
    // One full-screen page. The screen manager clears the display before
    // enter(); update() runs every loop pass and must return quickly (<5 ms).
    class Screen
    {
     public:
        virtual ~Screen() = default;
        virtual void enter() = 0;
        virtual void update() {}
        virtual void onTouch(const touch::Event&) {}
    };
}
