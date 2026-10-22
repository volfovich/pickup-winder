#include "ui/screens/presets.h"
#include "ui/screens/idle.h"
#include "ui/screens/keyboard.h"
#include "ui/ui.h"
#include "winder.h"

namespace ui::screens
{
    Presets presets;

    namespace
    {
        constexpr uint32_t CHECK_MS   = 250; // pick up changes made from serial
        constexpr uint32_t CONFIRM_MS = 3000;
        constexpr uint32_t MESSAGE_MS = 4000;
        constexpr uint8_t ROWS        = app::PRESET_SLOTS / 2;
        constexpr int16_t SLOT_Y = 28, SLOT_DY = 30, SLOT_H = 27, SLOT_W = 150;
        constexpr int16_t INFO_Y = 178, INFO_H = 18;

        const char* const SAVE_LABEL   = "SAVE";
        const char* const DELETE_LABEL = "DELETE";
        const char* const CONFIRM_LABEL = "TAP\nAGAIN";

        // The slot the keyboard saves into, and the message for after it
        uint8_t saveSlot = 0;
        char savedMessage[40];

        const char* applySave(const char* name)
        {
            if (const char* err = app::savePreset(saveSlot, name)) return err;
            snprintf(savedMessage, sizeof(savedMessage), "saved %s", app::preset(saveSlot)->name);
            return nullptr;
        }

        bool locked()
        {
            return winder::inRun() || app::positioning();
        }

        // "1  Strat neck" or "6  - empty -"
        void slotLabel(uint8_t slot, char* buf, size_t size)
        {
            const app::Preset* p = app::preset(slot);
            snprintf(buf, size, "%u  %s", slot + 1, p ? p->name : "- empty -");
        }

        constexpr Button slotButton(uint8_t slot)
        {
            return Button{(int16_t)(slot < ROWS ? 6 : 164), (int16_t)(SLOT_Y + (slot % ROWS) * SLOT_DY), SLOT_W,
                          SLOT_H, ""};
        }
    }

    Presets::Presets()
        : buttons{
              slotButton(0), slotButton(1), slotButton(2), slotButton(3), slotButton(4),
              slotButton(5), slotButton(6), slotButton(7), slotButton(8), slotButton(9),
              {6, 198, 74, 38, "BACK"},
              {84, 198, 74, 38, "LOAD", theme::GO},
              {162, 198, 74, 38, SAVE_LABEL},
              {240, 198, 74, 38, DELETE_LABEL},
          },
          group(buttons, BTN_COUNT)
    {
        static_assert(app::PRESET_SLOTS == 10, "the slot buttons above are for 10 slots");
        for (uint8_t i = 0; i < app::PRESET_SLOTS; i++) buttons[i].label = labels[i];
    }

    // Selected: highlighted; the active (loaded) preset: green
    void Presets::load()
    {
        shownLocked = locked();
        shownActive = app::activePreset();
        for (uint8_t i = 0; i < app::PRESET_SLOTS; i++)
        {
            slotLabel(i, labels[i], sizeof(labels[i]));
            buttons[i].color = i == selected ? theme::BUTTON_HI : i == shownActive ? theme::GO : theme::BUTTON;
        }
        bool used                  = app::preset(selected) != nullptr;
        buttons[BTN_LOAD].enabled   = used && !shownLocked;
        buttons[BTN_SAVE].enabled   = !shownLocked;
        buttons[BTN_DELETE].enabled = used && !shownLocked;
    }

    // What the selected slot holds, or a message (for MESSAGE_MS)
    void Presets::drawInfo(const char* message, uint16_t color)
    {
        char text[72];
        messageUntil = message ? millis() + MESSAGE_MS : 0;
        if (!message)
        {
            const app::Preset* p = app::preset(selected);
            if (!p) message = "empty - SAVE stores the current settings";
            else
            {
                uint32_t tenths = (p->t.widthUm + 50) / 100;
                if (p->t.on)
                    snprintf(text, sizeof(text), "%lu turns, %lu rpm, %u AWG%s, %lu.%lu mm%s%s", p->s.turns, p->s.rpm,
                             p->t.awg, p->t.heavy ? " H" : "", tenths / 10, tenths % 10,
                             p->t.scatter ? ", scatter" : "",
                             selected == app::activePreset() && app::presetModified() ? " (changed)" : "");
                else
                    snprintf(text, sizeof(text), "%lu turns, %lu rpm, guide off", p->s.turns, p->s.rpm);
                message = text;
            }
        }
        drawMessage(message, INFO_Y, INFO_H, color);
    }

    void Presets::redraw()
    {
        load();
        group.drawAll();
        drawInfo();
    }

    void Presets::cancelConfirm()
    {
        confirming                = -1;
        confirmUntil              = 0;
        buttons[BTN_SAVE].label   = SAVE_LABEL;
        buttons[BTN_DELETE].label = DELETE_LABEL;
        buttons[BTN_SAVE].color = buttons[BTN_DELETE].color = theme::BUTTON;
    }

    bool Presets::confirm(uint8_t button)
    {
        if (confirming == button) return true;
        cancelConfirm();
        confirming            = button;
        confirmUntil          = millis() + CONFIRM_MS;
        buttons[button].label = CONFIRM_LABEL;
        buttons[button].color = theme::WARN;
        buttons[BTN_SAVE].draw();
        buttons[BTN_DELETE].draw();
        return false;
    }

    void Presets::enter()
    {
        drawStatusBar("PRESETS");
        cancelConfirm();
        group.reset();
        load();
        group.drawAll();
        if (savedMessage[0])
        {
            drawInfo(savedMessage, theme::OK);
            savedMessage[0] = '\0';
        }
        else drawInfo();
    }

    void Presets::update()
    {
        if (confirmUntil && millis() > confirmUntil)
        {
            cancelConfirm();
            buttons[BTN_SAVE].draw();
            buttons[BTN_DELETE].draw();
        }
        if (messageUntil && millis() > messageUntil) drawInfo();

        if (millis() - lastCheckMs < CHECK_MS || group.pressed() >= 0) return;
        lastCheckMs = millis();
        // A run started, or presets changed from serial
        bool changed = locked() != shownLocked || app::activePreset() != shownActive;
        char label[sizeof(labels[0])];
        for (uint8_t i = 0; i < app::PRESET_SLOTS && !changed; i++)
        {
            slotLabel(i, label, sizeof(label));
            changed = strcmp(label, labels[i]) != 0;
        }
        if (changed) redraw();
    }

    void Presets::onTouch(const touch::Event& e)
    {
        int8_t b = group.handle(e);
        if (b < 0) return;
        if (b != confirming) cancelConfirm();

        if (b < app::PRESET_SLOTS)
        {
            selected = b;
            redraw();
            return;
        }
        switch (b)
        {
            case BTN_BACK: show(idle); return;
            case BTN_LOAD:
                if (const char* err = app::loadPreset(selected)) drawInfo(err, theme::ERR);
                else
                {
                    char text[40];
                    snprintf(text, sizeof(text), "loaded %s", app::preset(selected)->name);
                    load();
                    group.drawAll();
                    drawInfo(text, theme::OK);
                }
                return;
            case BTN_SAVE:
            {
                // Overwriting a preset needs a second tap; the keyboard shows its name
                const app::Preset* p = app::preset(selected);
                if (p && !confirm(BTN_SAVE)) return;
                cancelConfirm();
                saveSlot = selected;
                keyboard.open("Preset name", p ? p->name : "", app::PRESET_NAME_MAX, applySave, *this);
                return;
            }
            case BTN_DELETE:
            {
                if (!confirm(BTN_DELETE)) return;
                char text[40];
                snprintf(text, sizeof(text), "deleted %s", app::preset(selected)->name);
                const char* err = app::deletePreset(selected);
                cancelConfirm();
                load();
                group.drawAll();
                drawInfo(err ? err : text, err ? theme::ERR : theme::OK);
                return;
            }
            default: return;
        }
    }
}
