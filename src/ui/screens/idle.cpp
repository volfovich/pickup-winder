#include "ui/screens/idle.h"
#include "app.h"
#include "traverse.h"
#include "ui/screens/guide.h"
#include "ui/screens/presets.h"
#include "ui/screens/setup.h"
#include "ui/ui.h"

namespace ui::screens
{
    Idle idle;

    namespace
    {
        constexpr uint32_t CHECK_MS = 250; // pick up changes made from serial
        constexpr int16_t VALUE_X   = 96;
        constexpr int16_t MSG_Y     = 112; // between the settings and START
        constexpr int16_t MSG_H     = 16;
        constexpr uint32_t MSG_MS   = 5000;
    }

    Idle::Idle()
        : buttons{
              {8, 128, 304, 56, "START", theme::GO},
              {8, 190, 96, 46, "SETUP"},
              {112, 190, 96, 46, "PRESETS"},
              {216, 190, 96, 46, "GUIDE"},
          },
          group(buttons, BTN_COUNT)
    {
    }

    // The loaded preset's name, " *" when changed since; IDLE without one
    void Idle::drawTitle()
    {
        static char title[app::PRESET_NAME_MAX + 3];
        shownPreset         = app::activePreset();
        shownModified       = app::presetModified();
        const app::Preset* p = shownPreset >= 0 ? app::preset(shownPreset) : nullptr;
        if (p) snprintf(title, sizeof(title), "%s%s", p->name, shownModified ? " *" : "");
        else strlcpy(title, "IDLE", sizeof(title));
        drawStatusBar(title);
    }

    void Idle::enter()
    {
        drawTitle();
        shown            = app::settings();
        shownTrav        = app::traverseSettings();
        shownPositioning = false;
        shownGuideWarn   = guideWarning();
        setGuideLabel();
        msgUntilMs       = 0;
        buttons[BTN_START].label = "START";
        buttons[BTN_START].color = theme::GO;
        drawValues();
        group.reset();
        group.drawAll();
    }

    void Idle::drawValues()
    {
        static char text[48];
        auto& d = tft();
        d.fillRect(0, theme::STATUS_H, theme::W, MSG_Y - theme::STATUS_H, theme::BG);

        printAt("Turns", 12, 40, theme::FONT_LABEL, theme::MUTED);
        printAt(fmtThousands(shown.turns, text, sizeof(text)), VALUE_X, 32, theme::FONT_LARGE, theme::FG);
        printAt("Dir", 222, 40, theme::FONT_LABEL, theme::MUTED);
        printAt(shown.dir == winder::Dir::CW ? "CW" : "CCW", 254, 38, theme::FONT_VALUE, theme::FG);

        printAt("RPM", 12, 72, theme::FONT_LABEL, theme::MUTED);
        snprintf(text, sizeof(text), "%lu", shown.rpm);
        printAt(text, VALUE_X, 70, theme::FONT_VALUE, theme::FG);
        int16_t x = VALUE_X + textWidth(text, theme::FONT_VALUE) + 10;
        snprintf(text, sizeof(text), "(actual %.1f)", winder::actualRpm(shown.rpm));
        printAt(text, x, 73, theme::FONT_BODY, theme::MUTED);

        // The bobbin to wind (with the guide), else guided by hand
        printAt("Bobbin", 12, 94, theme::FONT_LABEL, theme::MUTED);
        if (shownTrav.on)
        {
            app::Winding w = app::winding();
            // The font is fitted to the line with ", scatter", so the size
            // stays the same when scatter is switched on or off
            snprintf(text, sizeof(text), "%u AWG%s, %lu layers, scatter", shownTrav.awg, shownTrav.heavy ? " H" : "",
                     w.layers);
            const ILI9341_t3_font_t& f = fitFont(text, theme::W - VALUE_X - 4, theme::FONT_VALUE);
            if (!shownTrav.scatter) *strrchr(text, ',') = '\0';
            printAt(text, VALUE_X, 92 + (theme::FONT_VALUE.cap_height - f.cap_height), f, theme::FG);
        }
        else printAt("guide off - by hand", VALUE_X, 95, theme::FONT_BODY, theme::MUTED);
    }

    // The guide is switched on but not homed: START would refuse
    bool Idle::guideWarning()
    {
        return app::traverseSettings().on && !traverse::homed();
    }

    void Idle::setGuideLabel()
    {
        buttons[BTN_GUIDE].label = shownGuideWarn ? "HOME\nGUIDE!" : "GUIDE";
        buttons[BTN_GUIDE].color = shownGuideWarn ? theme::WARN : theme::BUTTON;
    }

    // With the traverse on, START first moves the guide to the start
    // position; meanwhile START becomes CANCEL. On success the screen
    // manager switches to Running.
    void Idle::followPositioning()
    {
        bool now = app::positioning();
        if (now == shownPositioning) return;
        shownPositioning           = now;
        buttons[BTN_START].label = now ? "CANCEL" : "START";
        buttons[BTN_START].color = now ? theme::WARN : theme::GO;
        buttons[BTN_START].draw();
        if (now) drawMessage("moving the guide to the start position...", MSG_Y, MSG_H, theme::MUTED);
        else drawMessage(app::startError(), MSG_Y, MSG_H, theme::ERR);
        msgUntilMs = now ? 0 : millis() + MSG_MS;
    }

    void Idle::update()
    {
        followPositioning();
        if (guideWarning() != shownGuideWarn && group.pressed() != BTN_GUIDE)
        {
            shownGuideWarn = !shownGuideWarn;
            setGuideLabel();
            buttons[BTN_GUIDE].draw();
        }
        if (msgUntilMs && millis() > msgUntilMs)
        {
            msgUntilMs = 0;
            drawMessage(nullptr, MSG_Y, MSG_H, theme::BG);
        }
        if (millis() - lastCheckMs < CHECK_MS) return;
        lastCheckMs = millis();
        const winder::Settings& s = app::settings();
        if (s != shown || app::traverseSettings() != shownTrav)
        {
            shown     = s;
            shownTrav = app::traverseSettings();
            drawValues();
        }
        if (app::activePreset() != shownPreset || app::presetModified() != shownModified) drawTitle();
    }

    void Idle::onTouch(const touch::Event& e)
    {
        switch (group.handle(e))
        {
            case BTN_START:
                if (app::positioning()) app::stop();
                else if (const char* err = app::start())
                {
                    drawMessage(err, MSG_Y, MSG_H, theme::ERR);
                    msgUntilMs = millis() + MSG_MS;
                }
                break;
            case BTN_SETUP: show(setup); break;
            case BTN_PRESETS: show(presets); break;
            case BTN_GUIDE: show(guide); break;
            default: break;
        }
    }
}
