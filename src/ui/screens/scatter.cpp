#include "ui/screens/scatter.h"
#include "config.h"
#include "ui/screens/bobbin.h"
#include "ui/screens/keypad.h"
#include "ui/ui.h"
#include "winder.h"

namespace ui::screens
{
    Scatter scatter;

    namespace
    {
        constexpr uint32_t CHECK_MS = 250;
        constexpr int16_t ABOUT_Y   = 112, ABOUT_LINE_H = 18;
        constexpr int16_t SUMMARY_Y = 172, SUMMARY_H = 20;

        bool locked()
        {
            return winder::inRun() || app::positioning();
        }
    }

    Scatter::Scatter()
        : buttons{
              {8, 30, 148, 32, "Scatter"},
              {164, 30, 148, 32, "Pitch +%"},
              {8, 66, 148, 32, "Ends mm"},
              {164, 66, 148, 32, "Seed"},
              {8, 196, 148, 40, "BACK"},
              {164, 196, 148, 40, "NEW SEED"},
          },
          group(buttons, BTN_COUNT)
    {
        for (uint8_t i = 0; i < FIELDS; i++) buttons[i].value = values[i];
    }

    void Scatter::load()
    {
        shown       = app::traverseSettings();
        shownTurns  = app::settings().turns;
        shownRpm    = app::settings().rpm;
        shownLocked = locked();
        uint32_t tenths = (shown.endScatterUm + 50) / 100;
        snprintf(values[F_ON], sizeof(values[0]), "%s", shown.scatter ? "ON" : "off");
        snprintf(values[F_PITCH], sizeof(values[0]), "%u", shown.pitchScatterPct);
        snprintf(values[F_ENDS], sizeof(values[0]), "%lu.%lu", tenths / 10, tenths % 10);
        snprintf(values[F_SEED], sizeof(values[0]), "%lu", shown.seed);
        for (uint8_t i = 0; i < BTN_COUNT; i++)
            if (i != BTN_BACK) buttons[i].enabled = !shownLocked;
    }

    // What the settings do, then the turns per layer and layers, or why
    // it won't fit
    void Scatter::drawSummary()
    {
        char text[96];
        app::Winding w = app::winding();
        uint32_t tenths = (shown.endScatterUm + 50) / 100;
        snprintf(text, sizeof(text), "Each layer: pitch %lu.%03lu - %lu.%03lu mm, turns up to %lu.%lu mm inside the margins.",
                 w.pitchUm / 1000, w.pitchUm % 1000, w.pitchUm * (100 + shown.pitchScatterPct) / 100 / 1000,
                 w.pitchUm * (100 + shown.pitchScatterPct) / 100 % 1000, tenths / 10, tenths % 10);
        tft().fillRect(0, ABOUT_Y, theme::W, SUMMARY_Y - ABOUT_Y, theme::BG);
        printWrapped(text, 12, ABOUT_Y, theme::W - 24, ABOUT_LINE_H, theme::FONT_BODY, theme::MUTED);

        if (!shown.scatter)
        {
            drawMessage("off - even layers", SUMMARY_Y, SUMMARY_H, theme::MUTED);
            return;
        }
        if (const char* err = app::checkTraverse())
        {
            drawMessage(err, SUMMARY_Y, SUMMARY_H, theme::ERR);
            return;
        }
        snprintf(text, sizeof(text), "%lu - %lu turns/layer, %lu layers", w.turnsPerLayer, w.maxTurnsPerLayer,
                 w.layers);
        drawMessage(text, SUMMARY_Y, SUMMARY_H, theme::FG);
    }

    void Scatter::redraw()
    {
        load();
        for (uint8_t i = 0; i < BTN_COUNT; i++) buttons[i].draw();
        drawSummary();
    }

    void Scatter::enter()
    {
        drawStatusBar("SCATTER");
        load();
        drawSummary();
        group.reset();
        group.drawAll();
    }

    void Scatter::update()
    {
        if (millis() - lastCheckMs < CHECK_MS || group.pressed() >= 0) return;
        lastCheckMs = millis();
        if (app::traverseSettings() != shown || app::settings().turns != shownTurns ||
            app::settings().rpm != shownRpm || locked() != shownLocked)
            redraw();
    }

    void Scatter::onTouch(const touch::Event& e)
    {
        const app::TraverseSettings& t = app::traverseSettings();
        switch (group.handle(e))
        {
            case F_ON: app::setScatter(!t.scatter); break;
            case F_PITCH:
                keypad.open("Pitch scatter %", 0, MAX_PITCH_SCATTER_PCT, t.pitchScatterPct, app::setPitchScatter,
                            *this);
                return;
            case F_ENDS:
                keypad.openMm("End scatter", 0, MAX_END_SCATTER_UM, t.endScatterUm, app::setEndScatter, *this, 1);
                return;
            case F_SEED: keypad.open("Seed", 0, MAX_SCATTER_SEED, t.seed, app::setSeed, *this); return;
            case BTN_NEW: app::newSeed(); break;
            case BTN_BACK: show(bobbin); return;
            default: return;
        }
        // A toggle or the seed changed: show it at once
        redraw();
    }
}
