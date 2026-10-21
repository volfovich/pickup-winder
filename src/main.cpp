#include <Arduino.h>
#include "app.h"
#include "config.h"
#include "tmc_driver.h"
#include "traverse.h"
#include "ui/ui.h"
#include "winder.h"

// USB serial console. All settings and actions go through app::, the same
// layer the touch UI uses.

namespace
{
    char line[64];
    size_t lineLen = 0;

    uint32_t lastProgressMs = 0;
    bool wasMoving          = false;

    void printHelp()
    {
        Serial.printf(
            "Setup (only while idle):\n"
            "  turns N      number of turns (1..%lu)\n"
            "  rpm N        max speed (%lu..%lu)\n"
            "  accel N      ramp up/down rate in RPM/s\n", MAX_TURNS, MIN_RPM, MAX_RPM);
        Serial.println(
            "  dir cw|ccw   winding direction\n"
            "  current N    spindle run current, mA RMS (heat grows with its square)\n"
            "  start        start winding (with the traverse: goes to the start position first)\n"
            "  release      disable both drivers (motors free)\n"
            "  init         re-run TMC2209 configuration\n"
            "Traverse (only while idle; lengths in mm):\n"
            "  traverse on|off   use the wire guide for winding\n"
            "  home              find the home switch (motor end = 0 mm)\n"
            "  jog MM / goto MM  move the guide (e.g. jog -0.5, goto 12)\n"
            "  awg 42|43|44      wire size\n"
            "  insulation single|heavy  wire insulation build\n"
            "  flange1 MM|here   carriage position where the wire meets flange 1 (nearer home)\n"
            "  flange2 MM|here   same for flange 2; sets the inner width\n"
            "  width MM          bobbin inner width (moves flange 2)\n"
            "  margin MM         kept clear inside both flanges\n"
            "  layer away|toward first layer direction\n"
            "  endstop           home switch state\n"
            "While winding / paused:\n"
            "  stop         ramped stop -> paused (count stays exact; also stops the traverse)\n"
            "  estop        immediate stop -> paused\n"
            "  resume       wind the remaining turns\n"
            "  finish       end a paused run early\n"
            "Any time:\n"
            "  status       settings and progress\n"
            "  drv          TMC2209 registers and status\n"
            "  calibrate    touch screen calibration\n"
            "  help\n"
            "Settings are saved and restored at power-up.");
    }

    void printSettings()
    {
        const winder::Settings& s = app::settings();
        uint32_t accelSec         = (s.rpm * 10 + s.accelRpmPerS / 2) / s.accelRpmPerS;
        Serial.printf("turns %lu  rpm %lu (actual %.1f)  accel %lu RPM/s (0->max in %lu.%lu s)  dir %s  current %u mA\n",
                      s.turns, s.rpm, winder::actualRpm(s.rpm), s.accelRpmPerS, accelSec / 10, accelSec % 10,
                      s.dir == winder::Dir::CW ? "cw" : "ccw", app::spindleCurrent());
    }

    // "12.345" for a value in um
    const char* fmtMm(int32_t um, char* buf, size_t size)
    {
        snprintf(buf, size, "%s%ld.%03ld", um < 0 ? "-" : "", labs(um) / 1000, labs(um) % 1000);
        return buf;
    }

    // "12.3" for a setting in um (kept in 0.1 mm steps)
    const char* fmtTenth(uint32_t um, char* buf, size_t size)
    {
        uint32_t tenths = (um + 50) / 100;
        snprintf(buf, size, "%lu.%lu", tenths / 10, tenths % 10);
        return buf;
    }

    void printTraverseSettings()
    {
        const app::TraverseSettings& t = app::traverseSettings();
        app::Winding w = app::winding();
        char p[16], a[16], b[16], c[16], s[16];
        Serial.printf("traverse %s  %u AWG %s (%s)  flanges %s - %s (inner width %s)  margin %s  first layer %s",
                      t.on ? "ON" : "off", t.awg, t.heavy ? "heavy" : "single", fmtMm(w.pitchUm, p, sizeof(p)),
                      fmtTenth(t.flangeUm, c, sizeof(c)), fmtTenth(t.flangeUm + t.widthUm, s, sizeof(s)),
                      fmtTenth(t.widthUm, a, sizeof(a)), fmtTenth(t.marginUm, b, sizeof(b)), t.firstAway ? "away" : "toward");
        if (w.turnsPerLayer)
            Serial.printf("\n  winds %s mm from %s: %lu turns/layer, %lu layers", fmtMm(w.spanUm, s, sizeof(s)),
                          fmtMm(w.startUm, a, sizeof(a)), w.turnsPerLayer, w.layers);
        Serial.println();
        if (const char* err = app::checkTraverse()) Serial.printf("  WARNING: %s\n", err);
    }

    void printTraverseState()
    {
        char a[16];
        Serial.printf("traverse driver %s, %s, %s, endstop %s\n",
                      tmc::traverse.ready() ? "ready" : "not found (address 1)",
                      traverse::driverEnabled() ? "enabled" : "disabled",
                      traverse::homed() ? "homed" : "NOT HOMED", traverse::endstopTriggered() ? "PRESSED" : "open");
        Serial.printf("  guide at %s mm%s\n", fmtMm(lroundf(traverse::positionMm() * 1000), a, sizeof(a)),
                      traverse::homed() ? "" : " (relative, not homed)");
    }

    void printStatus()
    {
        printSettings();
        printTraverseSettings();
        Serial.printf("driver %s, spindle: %s\n", tmc::spindle.ready() ? "ready" : "NOT CONFIGURED",
                      winder::driverEnabled() ? "holding" : "motor free");
        printTraverseState();
        if (app::positioning()) Serial.println("MOVING TO THE START POSITION");
        if (winder::inRun())
        {
            Serial.printf("%s  turns %.2f / %lu  steps %lu / %lu\n", winder::isMoving() ? "RUNNING" : "PAUSED",
                          winder::turnsDone(), winder::targetSteps() / STEPS_PER_REV, winder::stepsDone(),
                          winder::targetSteps());
            if (app::traverseRun())
            {
                const traverse_math::Pattern& p = app::runPattern();
                uint32_t layers = (winder::targetSteps() / STEPS_PER_REV + p.turnsPerLayer - 1) / p.turnsPerLayer;
                Serial.printf("  layer %lu / %lu  guide max lag %ld steps\n",
                              traverse_math::layerOf(p, winder::stepsDone()) + 1, layers, traverse::maxFollowError());
            }
        }
        else if (winder::lastOutcome() != winder::Outcome::None)
            winder::printSummary();
    }

    // "12.5", "-0.071", "3" (mm) -> um
    bool parseMm(const char* arg, int32_t& um)
    {
        if (!arg || !*arg) return false;
        bool neg = *arg == '-';
        if (*arg == '-' || *arg == '+') arg++;
        int32_t whole = 0, frac = 0, scale = 1000;
        bool digits = false;
        for (; isdigit(*arg); arg++, digits = true)
        {
            whole = whole * 10 + (*arg - '0');
            if (whole > 1000) return false;
        }
        if (*arg == '.')
            for (arg++; isdigit(*arg); arg++, digits = true)
                if (scale > 1) frac += (*arg - '0') * (scale /= 10);
        if (*arg || !digits) return false;
        um = (neg ? -1 : 1) * (whole * 1000 + frac);
        return true;
    }

    bool parseUint(const char* arg, uint32_t& out)
    {
        if (!arg || !*arg) return false;
        char* end;
        unsigned long v = strtoul(arg, &end, 10);
        if (*end != '\0') return false;
        out = v;
        return true;
    }

    // Prints an action's error, if any. Returns true on success.
    bool report(const char* err)
    {
        if (err) Serial.printf("ERR: %s\n", err);
        return !err;
    }

    // Runs a setter with the parsed number (0 if it didn't parse, which every
    // setter rejects with its range), then shows the settings.
    void setNumber(const char* (*setter)(uint32_t), const char* arg)
    {
        uint32_t v;
        if (!parseUint(arg, v)) v = 0;
        if (report(setter(v)) || !winder::inRun()) printSettings();
    }

    // Runs a traverse setter with a length in mm, then shows the settings
    void setLength(const char* (*setter)(uint32_t), const char* arg)
    {
        int32_t um;
        if (!parseMm(arg, um) || um < 0) Serial.println("ERR: give a length in mm, e.g. 12.5");
        else report(setter(um));
        printTraverseSettings();
    }

    void handleCommand(char* cmd)
    {
        char* arg = strchr(cmd, ' ');
        if (arg)
        {
            *arg++ = '\0';
            while (*arg == ' ') arg++;
        }

        if (!strcmp(cmd, "stop"))
        {
            if (winder::isMoving()) Serial.println("stopping (ramp down)...");
            app::stop();
        }
        else if (!strcmp(cmd, "estop")) app::estop();
        else if (!strcmp(cmd, "resume")) report(app::resume());
        else if (!strcmp(cmd, "finish")) report(app::finish());
        else if (!strcmp(cmd, "status")) printStatus();
        else if (!strcmp(cmd, "drv"))
        {
            tmc::spindle.printStatus();
            tmc::traverse.printStatus();
        }
        else if (!strcmp(cmd, "help")) printHelp();
        else if (!strcmp(cmd, "calibrate"))
        {
            ui::startCalibration();
            Serial.println("touch calibration started on the screen");
        }
        else if (!strcmp(cmd, "turns")) setNumber(app::setTurns, arg);
        else if (!strcmp(cmd, "rpm")) setNumber(app::setRpm, arg);
        else if (!strcmp(cmd, "accel")) setNumber(app::setAccel, arg);
        else if (!strcmp(cmd, "current")) setNumber(app::setCurrent, arg);
        else if (!strcmp(cmd, "dir"))
        {
            if (arg && !strcmp(arg, "cw")) report(app::setDir(winder::Dir::CW));
            else if (arg && !strcmp(arg, "ccw")) report(app::setDir(winder::Dir::CCW));
            else Serial.println("ERR: dir cw|ccw");
            printSettings();
        }
        else if (!strcmp(cmd, "start"))
        {
            if (!winder::inRun()) printSettings();
            report(app::start()); // progress output starts in printProgress()
        }
        else if (!strcmp(cmd, "release"))
        {
            if (report(app::release())) Serial.println("driver disabled");
        }
        else if (!strcmp(cmd, "init")) report(app::reinitDriver());
        else if (!strcmp(cmd, "traverse"))
        {
            if (arg && !strcmp(arg, "on")) report(app::setTraverseOn(true));
            else if (arg && !strcmp(arg, "off")) report(app::setTraverseOn(false));
            else Serial.println("ERR: traverse on|off");
            printTraverseSettings();
        }
        else if (!strcmp(cmd, "awg"))
        {
            uint32_t v;
            report(parseUint(arg, v) ? app::setAwg(v) : "awg 42|43|44");
            printTraverseSettings();
        }
        else if (!strcmp(cmd, "insulation"))
        {
            if (arg && !strcmp(arg, "single")) report(app::setHeavy(false));
            else if (arg && !strcmp(arg, "heavy")) report(app::setHeavy(true));
            else Serial.println("ERR: insulation single|heavy");
            printTraverseSettings();
        }
        else if (!strcmp(cmd, "width")) setLength(app::setWidth, arg);
        else if (!strcmp(cmd, "margin")) setLength(app::setMargin, arg);
        else if (!strcmp(cmd, "flange1") || !strcmp(cmd, "flange2"))
        {
            bool one = cmd[6] == '1';
            if (arg && !strcmp(arg, "here"))
            {
                report(one ? app::setFlange1Here() : app::setFlange2Here());
                printTraverseSettings();
            }
            else setLength(one ? app::setFlange1 : app::setFlange2, arg);
        }
        else if (!strcmp(cmd, "layer"))
        {
            if (arg && !strcmp(arg, "away")) report(app::setFirstAway(true));
            else if (arg && !strcmp(arg, "toward")) report(app::setFirstAway(false));
            else Serial.println("ERR: layer away|toward");
            printTraverseSettings();
        }
        else if (!strcmp(cmd, "home"))
        {
            if (report(app::home())) Serial.println("homing...");
        }
        else if (!strcmp(cmd, "jog") || !strcmp(cmd, "goto"))
        {
            int32_t um;
            if (!parseMm(arg, um)) Serial.printf("ERR: %s MM, e.g. %s\n", cmd, *cmd == 'j' ? "jog -0.5" : "goto 12");
            else report(*cmd == 'j' ? app::jog(um) : app::moveTo(um));
        }
        else if (!strcmp(cmd, "endstop"))
        {
            Serial.printf("endstop pin %u reads %s -> %s\n", PIN_ENDSTOP, digitalRead(PIN_ENDSTOP) ? "HIGH" : "LOW",
                          traverse::endstopTriggered() ? "PRESSED" : "open");
        }
        else if (*cmd) Serial.printf("ERR: unknown command '%s' (try 'help')\n", cmd);
    }

    void readSerial()
    {
        while (Serial.available())
        {
            char c = Serial.read();
            if (c == '\r' || c == '\n')
            {
                line[lineLen] = '\0';
                for (size_t i = 0; i < lineLen; i++) line[i] = tolower(line[i]);
                handleCommand(line);
                lineLen = 0;
            }
            else if (lineLen < sizeof(line) - 1)
            {
                line[lineLen++] = c;
            }
        }
    }

    // Progress lines for any run, whether started from serial or the screen.
    void printProgress()
    {
        bool moving = winder::isMoving();
        if (moving && !wasMoving)
        {
            const winder::Settings& s = winder::runSettings();
            Serial.println(winder::resumed() ? "resuming..." : "winding...");
            Serial.printf("step timer runs %lu rpm as %.2f rpm\n", s.rpm, winder::actualRpm(s.rpm));
            lastProgressMs = millis();
        }
        wasMoving = moving;

        if (!moving || millis() - lastProgressMs < PROGRESS_INTERVAL_MS) return;
        lastProgressMs = millis();
        Serial.printf("turns %9.2f / %lu   rpm %7.2f\n", winder::turnsDone(), winder::targetSteps() / STEPS_PER_REV,
                      winder::measuredRpm());
    }
}

namespace
{
    void printTraverseSummary()
    {
        int16_t off;
        switch (app::traversePulseCheck(&off))
        {
            case winder::PulseCheck::Ok: Serial.println("  traverse pulse check: OK"); break;
            case winder::PulseCheck::Mismatch:
                Serial.printf("  traverse pulse check: MISMATCH - driver saw %+d step(s) vs. sent (mod 64)\n", off);
                break;
            case winder::PulseCheck::Unavailable: Serial.println("  traverse pulse check: unavailable"); break;
            case winder::PulseCheck::Skipped: Serial.println("  traverse pulse check: skipped (fault)"); break;
        }
        Serial.printf("  traverse max lag %ld step(s), now at %.3f mm\n", traverse::maxFollowError(),
                      traverse::positionMm());
    }

    void printTraverseEvent()
    {
        switch (app::traverseEvent())
        {
            case traverse::Event::Homed:
                Serial.printf("traverse homed, parked at %.3f mm\n", traverse::positionMm());
                break;
            case traverse::Event::HomingFailed: Serial.printf("HOMING FAILED: %s\n", traverse::lastError()); break;
            case traverse::Event::MoveDone:
                if (traverse::lastError()) Serial.printf("traverse stopped: %s\n", traverse::lastError());
                else if (!app::positioning() && !winder::inRun())
                    Serial.printf("traverse at %.3f mm\n", traverse::positionMm());
                break;
            case traverse::Event::None: break;
        }
    }
}

void setup()
{
    Serial.begin(115200);
    winder::begin();   // driver EN pins to a defined (disabled) state first
    traverse::begin();
    ui::begin();     // splash screen, animated while we wait for USB serial

    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 3000) ui::update();

    Serial.println("\n" FIRMWARE_NAME " v" FIRMWARE_VERSION);
    app::begin();
    tmc::spindle.begin();
    if (!tmc::traverse.begin(true)) Serial.println("TMC2209 traverse (address 1): not found");
    printSettings();
    printTraverseSettings();
    Serial.println("type 'help' for commands");
    ui::markBooted();
}

void loop()
{
    uint32_t t0 = micros();

    readSerial();
    winder::Event e = app::update();
    if (e == winder::Event::Paused)
    {
        Serial.printf("PAUSED at %.2f / %lu turns — 'resume' or 'finish'\n", winder::turnsDone(),
                      winder::targetSteps() / STEPS_PER_REV);
    }
    else if (e == winder::Event::Ended)
    {
        winder::printSummary();
        if (app::traverseRun()) printTraverseSummary();
        if (winder::lastOutcome() == winder::Outcome::Faulted)
            Serial.println("driver disabled after fault — check hardware, then 'init'");
    }
    printTraverseEvent();
    printProgress();
    ui::update();

    ui::noteLoopTime(micros() - t0);
}
