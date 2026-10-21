#include "ui/screens/guide.h"
#include "app.h"
#include "config.h"
#include "tmc_driver.h"
#include "traverse.h"
#include "ui/screens/bobbin.h"
#include "ui/screens/idle.h"
#include "ui/ui.h"
#include "winder.h"

namespace ui::screens
{
    Guide guide;

    namespace
    {
        constexpr uint32_t REFRESH_MS = 100;
        constexpr uint32_t MSG_MS     = 5000;
        constexpr int16_t POS_Y       = 32;
        constexpr int16_t STATE_Y     = 68, STATE_H = 18;
        constexpr int16_t JOG_Y = 90, JOG_W = 47, JOG_DX = 51, JOG_H = 44;
        constexpr int32_t JOG_UM[] = {-10000, -1000, -100, 100, 1000, 10000};

        constexpr Button jog(uint8_t i, const char* label)
        {
            return Button{(int16_t)(8 + i * JOG_DX), JOG_Y, JOG_W, JOG_H, label};
        }

        // Carriage position of flange 1 or 2 (um from home)
        int32_t flangeUm(uint8_t n)
        {
            const app::TraverseSettings& t = app::traverseSettings();
            return t.flangeUm + (n == 2 ? t.widthUm : 0);
        }

        // "5.0" for um
        const char* tenths(uint32_t um, char* buf, size_t size)
        {
            uint32_t v = (um + 50) / 100;
            snprintf(buf, size, "%lu.%lu", v / 10, v % 10);
            return buf;
        }
    }

    Guide::Guide()
        : buttons{
              jog(0, "-10"), jog(1, "-1"), jog(2, "-0.1"), jog(3, "+0.1"), jog(4, "+1"), jog(5, "+10"),
              {8, 142, 98, 44, "HOME"},
              {111, 142, 98, 44, "SET\nFLANGE 1"},
              {214, 142, 98, 44, "SET\nFLANGE 2"},
              {8, 194, 96, 42, "BOBBIN"},
              {108, 194, 112, 42, "GO TO\nFLANGE 1"},
              {224, 194, 88, 42, "BACK"},
          },
          group(buttons, BTN_COUNT)
    {
    }

    void Guide::enter()
    {
        drawStatusBar("GUIDE");
        printAt("Position", 8, POS_Y + 10, theme::FONT_LABEL, theme::MUTED);
        shownPos[0] = shownState[0] = '\0';
        shownEnabled                = 0xFFFF; // force the first button update
        shownBusy                   = false;
        msgUntilMs                  = 0;
        gotoFlange                  = 1;
        buttons[BTN_GOTO].label     = "GO TO\nFLANGE 1";
        group.reset();
        refresh(true);
    }

    void Guide::message(const char* text, uint16_t color)
    {
        drawMessage(text, STATE_Y, STATE_H, color);
        msgUntilMs     = millis() + MSG_MS;
        shownState[0] = '\0';
    }

    void Guide::refresh(bool force)
    {
        char t[48];

        // Position, homed or not
        snprintf(t, sizeof(t), "%.3f mm", traverse::positionMm());
        if (force || strcmp(t, shownPos))
        {
            strlcpy(shownPos, t, sizeof(shownPos));
            tft().fillRect(96, POS_Y, theme::W - 96, 34, theme::BG);
            printAt(t, 96, POS_Y, theme::FONT_LARGE, traverse::homed() ? theme::FG : theme::WARN);
        }

        // State line (unless a message is showing)
        bool driverOk = tmc::traverse.ready();
        bool busy     = traverse::busy();
        bool inRun    = winder::inRun() || app::positioning() || traverse::following();
        if (!msgUntilMs)
        {
            uint16_t color = theme::MUTED;
            if (!driverOk)
            {
                snprintf(t, sizeof(t), "guide driver not found - check wiring, re-init");
                color = theme::ERR;
            }
            else if (inRun) snprintf(t, sizeof(t), "winding - guide follows the spindle");
            else
                snprintf(t, sizeof(t), "%s  -  endstop %s", traverse::homed() ? "homed" : "NOT HOMED",
                         traverse::endstopTriggered() ? "PRESSED" : "open");
            if (force || strcmp(t, shownState))
            {
                strlcpy(shownState, t, sizeof(shownState));
                drawMessage(t, STATE_Y, STATE_H, color);
            }
        }

        // Buttons: HOME turns into STOP while moving
        bool idle = driverOk && !busy && !inRun;
        bool taught = idle && traverse::homed();
        uint16_t enabled = 0;
        for (uint8_t i = J_M10; i <= J_P10; i++)
            if (idle) enabled |= 1 << i;
        if (busy || (driverOk && !inRun)) enabled |= 1 << BTN_HOME;
        if (taught) enabled |= (1 << BTN_SET1) | (1 << BTN_SET2);
        if (taught && flangeUm(2) <= (int32_t)TRAVERSE_TRAVEL_UM) enabled |= 1 << BTN_GOTO;
        enabled |= (1 << BTN_BOBBIN) | (1 << BTN_BACK);

        if (force || enabled != shownEnabled || busy != shownBusy)
        {
            shownEnabled                = enabled;
            shownBusy                   = busy;
            buttons[BTN_HOME].label = busy ? "STOP" : "HOME";
            buttons[BTN_HOME].color = busy ? theme::WARN : theme::BUTTON;
            for (uint8_t i = 0; i < BTN_COUNT; i++)
            {
                bool on = enabled & (1 << i);
                if (on == buttons[i].enabled && !force && i != BTN_HOME) continue;
                buttons[i].enabled = on;
                if (group.pressed() != i) buttons[i].draw();
            }
        }
    }

    void Guide::update()
    {
        switch (app::traverseEvent())
        {
            case traverse::Event::Homed: message("homed", theme::OK); break;
            case traverse::Event::HomingFailed: message(traverse::lastError(), theme::ERR); break;
            case traverse::Event::MoveDone:
                if (traverse::lastError()) message(traverse::lastError(), theme::ERR);
                break;
            case traverse::Event::None: break;
        }
        if (msgUntilMs && millis() > msgUntilMs) msgUntilMs = 0;
        if (millis() - lastMs < REFRESH_MS) return;
        lastMs = millis();
        refresh(false);
    }

    void Guide::onTouch(const touch::Event& e)
    {
        int8_t b = group.handle(e);
        if (b < 0) return;

        const char* err = nullptr;
        char t[64], a[12], w[12];
        if (b <= J_P10) err = app::jog(JOG_UM[b]);
        else if (b == BTN_HOME)
        {
            if (traverse::busy()) traverse::stop();
            else err = app::home();
        }
        else if (b == BTN_SET1 || b == BTN_SET2)
        {
            err = b == BTN_SET1 ? app::setFlange1Here() : app::setFlange2Here();
            if (!err)
            {
                const app::TraverseSettings& s = app::traverseSettings();
                if (b == BTN_SET1)
                    snprintf(t, sizeof(t), "flange 1 set at %s mm (flange 2 at %s)", tenths(s.flangeUm, a, sizeof(a)),
                             tenths(flangeUm(2), w, sizeof(w)));
                else
                    snprintf(t, sizeof(t), "flange 2 set at %s mm - inner width %s", tenths(flangeUm(2), a, sizeof(a)),
                             tenths(s.widthUm, w, sizeof(w)));
                message(t, theme::OK);
            }
        }
        else if (b == BTN_GOTO)
        {
            err = app::moveTo(flangeUm(gotoFlange));
            if (!err)
            {
                gotoFlange              = gotoFlange == 1 ? 2 : 1;
                buttons[BTN_GOTO].label = gotoFlange == 1 ? "GO TO\nFLANGE 1" : "GO TO\nFLANGE 2";
            }
        }
        else if (b == BTN_BOBBIN)
        {
            show(bobbin);
            return;
        }
        else if (b == BTN_BACK)
        {
            show(idle);
            return;
        }
        if (err) message(err, theme::ERR);
        refresh(false);
    }
}
