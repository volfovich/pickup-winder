#include "ui/screens/bobbin.h"
#include "config.h"
#include "ui/screens/guide.h"
#include "ui/screens/keypad.h"
#include "ui/screens/scatter.h"
#include "ui/screens/setup.h"
#include "ui/ui.h"
#include "winder.h"

namespace ui::screens
{
    Bobbin bobbin;

    namespace
    {
        constexpr uint32_t CHECK_MS  = 250;
        constexpr int16_t SUMMARY_Y  = 172, SUMMARY_H = 20;

        // "12.3" for um
        void formatMm(uint32_t um, char* buf, size_t size)
        {
            uint32_t tenths = (um + 50) / 100;
            snprintf(buf, size, "%lu.%lu", tenths / 10, tenths % 10);
        }

        // The next size in WIRE_SIZES, round the list
        uint8_t nextAwg(uint8_t awg)
        {
            constexpr uint8_t n = sizeof(WIRE_SIZES) / sizeof(WIRE_SIZES[0]);
            for (uint8_t i = 0; i < n; i++)
                if (WIRE_SIZES[i].awg == awg) return WIRE_SIZES[(i + 1) % n].awg;
            return WIRE_SIZES[0].awg;
        }

        bool locked()
        {
            return winder::inRun() || app::positioning();
        }
    }

    Bobbin::Bobbin()
        : buttons{
              {8, 30, 148, 32, "Guide"},
              {164, 30, 148, 32, "First"},
              {8, 66, 148, 32, "AWG"},
              {164, 66, 148, 32, "Insul."},
              {8, 102, 148, 32, "Margin"},
              {164, 102, 148, 32, "Inner w."},
              {8, 138, 304, 32, "Flanges (rail mm)"},
              {8, 196, 96, 40, "BACK"},
              {112, 196, 96, 40, "SCATTER"},
              {216, 196, 96, 40, "GUIDE"},
          },
          group(buttons, BTN_COUNT)
    {
        for (uint8_t i = 0; i < FIELDS; i++) buttons[i].value = values[i];
    }

    void Bobbin::load()
    {
        shown       = app::traverseSettings();
        shownRpm    = app::settings().rpm;
        shownTurns  = app::settings().turns;
        shownLocked = locked();
        snprintf(values[F_ON], sizeof(values[0]), "%s", shown.on ? "ON" : "off");
        snprintf(values[F_FIRST], sizeof(values[0]), "%s", shown.firstAway ? "away" : "toward");
        snprintf(values[F_AWG], sizeof(values[0]), "%u", shown.awg);
        snprintf(values[F_INSUL], sizeof(values[0]), "%s", shown.heavy ? "heavy" : "single");
        formatMm(shown.widthUm, values[F_WIDTH], sizeof(values[0]));
        formatMm(shown.marginUm, values[F_MARGIN], sizeof(values[0]));
        char f2[16];
        formatMm(shown.flangeUm + shown.widthUm, f2, sizeof(f2));
        formatMm(shown.flangeUm, values[F_FLANGES], sizeof(values[0]));
        strlcat(values[F_FLANGES], " - ", sizeof(values[0]));
        strlcat(values[F_FLANGES], f2, sizeof(values[0]));
        for (uint8_t i = 0; i < FIELDS; i++) buttons[i].enabled = !shownLocked;
    }

    // Turns per layer and layers for the set turns, or why it doesn't fit
    void Bobbin::drawSummary()
    {
        if (const char* err = app::checkTraverse())
        {
            drawMessage(err, SUMMARY_Y, SUMMARY_H, theme::ERR);
            return;
        }
        char text[64];
        app::Winding w = app::winding();
        if (shown.scatter)
            snprintf(text, sizeof(text), "%lu.%03lu mm wire, scatter: %lu layers", w.pitchUm / 1000,
                     w.pitchUm % 1000, w.layers);
        else
            snprintf(text, sizeof(text), "%lu.%03lu mm wire: %lu turns/layer, %lu layers", w.pitchUm / 1000,
                     w.pitchUm % 1000, w.turnsPerLayer, w.layers);
        drawMessage(text, SUMMARY_Y, SUMMARY_H, theme::MUTED);
    }

    void Bobbin::enter()
    {
        drawStatusBar("BOBBIN");
        load();
        drawSummary();
        group.reset();
        group.drawAll();
    }

    void Bobbin::update()
    {
        if (millis() - lastCheckMs < CHECK_MS || group.pressed() >= 0) return;
        lastCheckMs = millis();
        if (app::traverseSettings() != shown || app::settings().rpm != shownRpm ||
            app::settings().turns != shownTurns || locked() != shownLocked)
        {
            load();
            for (uint8_t i = 0; i < FIELDS; i++) buttons[i].draw();
            drawSummary();
        }
    }

    void Bobbin::onTouch(const touch::Event& e)
    {
        const app::TraverseSettings& t = app::traverseSettings();
        switch (group.handle(e))
        {
            case F_ON: app::setTraverseOn(!t.on); break;
            case F_FIRST: app::setFirstAway(!t.firstAway); break;
            case F_AWG: app::setAwg(nextAwg(t.awg)); break;
            case F_INSUL: app::setHeavy(!t.heavy); break;
            case F_WIDTH:
                keypad.openMm("Inner width", MIN_WIDTH_UM, MAX_WIDTH_UM, t.widthUm, app::setWidth, *this, 1);
                return;
            case F_MARGIN:
                keypad.openMm("Margin", 0, MAX_MARGIN_UM, t.marginUm, app::setMargin, *this, 1);
                return;
            case F_FLANGES: show(guide); return; // taught there, by jogging to each flange
            case BTN_BACK: show(setup); return;
            case BTN_SCATTER: show(scatter); return;
            case BTN_GUIDE: show(guide); return;
            default: return;
        }
        // A toggle changed: show it at once
        load();
        for (uint8_t i = 0; i < FIELDS; i++) buttons[i].draw();
        drawSummary();
    }
}
