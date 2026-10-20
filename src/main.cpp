#include <Arduino.h>
#include "app.h"
#include "config.h"
#include "tmc_driver.h"
#include "ui/ui.h"
#include "winder.h"

// USB serial console. All settings and actions go through app::, the same
// layer the touch UI uses.

namespace
{
    char line[64];
    size_t lineLen = 0;

    // Speed is measured over the last RPM_WINDOW progress intervals (1 s) with
    // microsecond timing, so one step more or less in the window is 0.02 rpm.
    constexpr uint8_t RPM_WINDOW = 4;
    struct Sample
    {
        uint32_t steps;
        uint32_t us;
    };
    Sample samples[RPM_WINDOW + 1];
    uint8_t sampleCount = 0;

    uint32_t lastProgressMs = 0;
    bool wasRunning         = false;

    void printHelp()
    {
        Serial.printf(
            "Setup (only while idle):\n"
            "  turns N      number of turns (1..%lu)\n"
            "  rpm N        max speed (%lu..%lu)\n"
            "  accel N      ramp up/down rate in RPM/s\n", MAX_TURNS, MIN_RPM, MAX_RPM);
        Serial.println(
            "  dir cw|ccw   winding direction\n"
            "  start        start winding\n"
            "  release      disable the driver (motor free)\n"
            "  init         re-run TMC2209 configuration\n"
            "Any time:\n"
            "  stop         ramped stop (count stays exact)\n"
            "  estop        immediate stop\n"
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
        Serial.printf("turns %lu  rpm %lu (actual %.1f)  accel %lu RPM/s (0->max in %lu.%lu s)  dir %s\n", s.turns,
                      s.rpm, winder::actualRpm(s.rpm), s.accelRpmPerS, accelSec / 10, accelSec % 10,
                      s.dir == winder::Dir::CW ? "cw" : "ccw");
    }

    void printStatus()
    {
        printSettings();
        Serial.printf("driver %s, %s\n", tmc::ready() ? "ready" : "NOT CONFIGURED",
                      winder::driverEnabled() ? "enabled" : "disabled");
        if (winder::isRunning())
            Serial.printf("RUNNING  turns %.2f / %lu  steps %lu / %lu\n", winder::turnsDone(),
                          winder::targetSteps() / STEPS_PER_REV, winder::stepsDone(), winder::targetSteps());
        else if (winder::lastOutcome() != winder::Outcome::None)
            winder::printSummary();
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
        if (report(setter(v)) || !winder::isRunning()) printSettings();
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
            if (winder::isRunning()) Serial.println("stopping (ramp down)...");
            app::stop();
        }
        else if (!strcmp(cmd, "estop")) app::estop();
        else if (!strcmp(cmd, "status")) printStatus();
        else if (!strcmp(cmd, "drv")) tmc::printStatus();
        else if (!strcmp(cmd, "help")) printHelp();
        else if (!strcmp(cmd, "calibrate"))
        {
            ui::startCalibration();
            Serial.println("touch calibration started on the screen");
        }
        else if (!strcmp(cmd, "turns")) setNumber(app::setTurns, arg);
        else if (!strcmp(cmd, "rpm")) setNumber(app::setRpm, arg);
        else if (!strcmp(cmd, "accel")) setNumber(app::setAccel, arg);
        else if (!strcmp(cmd, "dir"))
        {
            if (arg && !strcmp(arg, "cw")) report(app::setDir(winder::Dir::CW));
            else if (arg && !strcmp(arg, "ccw")) report(app::setDir(winder::Dir::CCW));
            else Serial.println("ERR: dir cw|ccw");
            printSettings();
        }
        else if (!strcmp(cmd, "start"))
        {
            if (winder::isRunning())
            {
                report("already running");
                return;
            }
            printSettings();
            report(app::start()); // progress output starts in printProgress()
        }
        else if (!strcmp(cmd, "release"))
        {
            if (report(app::release())) Serial.println("driver disabled");
        }
        else if (!strcmp(cmd, "init")) report(app::reinitDriver());
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
        bool running = winder::isRunning();
        if (running && !wasRunning)
        {
            const winder::Settings& s = app::settings();
            Serial.println("winding...");
            Serial.printf("step timer runs %lu rpm as %.2f rpm\n", s.rpm, winder::actualRpm(s.rpm));
            lastProgressMs = millis();
            samples[0]     = {0, micros()};
            sampleCount    = 1;
        }
        wasRunning = running;

        if (!running || millis() - lastProgressMs < PROGRESS_INTERVAL_MS) return;
        lastProgressMs = millis();

        Sample now{winder::stepsDone(), micros()};
        if (sampleCount == RPM_WINDOW + 1)
        {
            memmove(samples, samples + 1, RPM_WINDOW * sizeof(Sample));
            sampleCount--;
        }
        samples[sampleCount++] = now;

        const Sample& first = samples[0];
        double rpm = (double)(now.steps - first.steps) / STEPS_PER_REV * 60e6 / (now.us - first.us);
        Serial.printf("turns %9.2f / %lu   rpm %7.2f\n", (double)now.steps / STEPS_PER_REV,
                      winder::targetSteps() / STEPS_PER_REV, rpm);
    }
}

void setup()
{
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 3000) {}

    Serial.println("\nPickup winder");
    app::begin();
    winder::begin();
    tmc::begin();
    ui::begin();
    printSettings();
    Serial.println("type 'help' for commands");
}

void loop()
{
    uint32_t t0 = micros();

    readSerial();
    if (app::update())
    {
        winder::printSummary();
        if (winder::lastOutcome() == winder::Outcome::Faulted)
            Serial.println("driver disabled after fault — check hardware, then 'init'");
    }
    printProgress();
    ui::update();

    ui::noteLoopTime(micros() - t0);
}
