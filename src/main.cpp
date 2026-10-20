#include <Arduino.h>
#include "config.h"
#include "tmc_driver.h"
#include "ui/ui.h"
#include "winder.h"

namespace
{
    winder::Settings settings{DEFAULT_TURNS, DEFAULT_RPM, DEFAULT_ACCEL_RPM_PER_S, winder::Dir::CW};

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

    uint32_t lastProgressMs  = 0;
    uint32_t lastPollMs      = 0;
    uint8_t commFailures     = 0;
    bool otWarned            = false;

    constexpr uint8_t COMM_FAILURES_TO_ABORT = 3;

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
            "  help");
    }

    void printSettings()
    {
        uint32_t accelSec = (settings.rpm * 10 + settings.accelRpmPerS / 2) / settings.accelRpmPerS;
        Serial.printf("turns %lu  rpm %lu (actual %.1f)  accel %lu RPM/s (0->max in %lu.%lu s)  dir %s\n",
                      settings.turns, settings.rpm, winder::actualRpm(settings.rpm), settings.accelRpmPerS,
                      accelSec / 10, accelSec % 10, settings.dir == winder::Dir::CW ? "cw" : "ccw");
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

    bool rejectWhileRunning()
    {
        if (!winder::isRunning()) return false;
        Serial.println("ERR: not allowed while winding — use 'stop' or 'estop' first");
        return true;
    }

    void handleCommand(char* cmd)
    {
        char* arg = strchr(cmd, ' ');
        if (arg)
        {
            *arg++ = '\0';
            while (*arg == ' ') arg++;
        }
        uint32_t v;

        if (!strcmp(cmd, "stop"))
        {
            if (winder::isRunning()) Serial.println("stopping (ramp down)...");
            winder::stop();
        }
        else if (!strcmp(cmd, "estop"))
        {
            winder::estop();
        }
        else if (!strcmp(cmd, "status"))
        {
            printStatus();
        }
        else if (!strcmp(cmd, "calibrate"))
        {
            ui::startCalibration();
            Serial.println("touch calibration started on the screen");
        }
        else if (!strcmp(cmd, "drv"))
        {
            tmc::printStatus();
        }
        else if (!strcmp(cmd, "help"))
        {
            printHelp();
        }
        else if (!strcmp(cmd, "turns"))
        {
            if (rejectWhileRunning()) return;
            if (!parseUint(arg, v) || v < 1 || v > MAX_TURNS)
                Serial.printf("ERR: turns must be 1..%lu\n", MAX_TURNS);
            else
                settings.turns = v;
            printSettings();
        }
        else if (!strcmp(cmd, "rpm"))
        {
            if (rejectWhileRunning()) return;
            if (!parseUint(arg, v)) v = 0;
            if (winder::rpmSupported(v)) settings.rpm = v;
            printSettings();
        }
        else if (!strcmp(cmd, "accel"))
        {
            if (rejectWhileRunning()) return;
            if (!parseUint(arg, v)) v = 0;
            if (winder::accelSupported(v)) settings.accelRpmPerS = v;
            printSettings();
        }
        else if (!strcmp(cmd, "dir"))
        {
            if (rejectWhileRunning()) return;
            if (arg && !strcmp(arg, "cw")) settings.dir = winder::Dir::CW;
            else if (arg && !strcmp(arg, "ccw")) settings.dir = winder::Dir::CCW;
            else Serial.println("ERR: dir cw|ccw");
            printSettings();
        }
        else if (!strcmp(cmd, "start"))
        {
            if (rejectWhileRunning()) return;
            printSettings();
            if (winder::start(settings))
            {
                Serial.println("winding...");
                Serial.printf("step timer runs %lu rpm as %.2f rpm\n", settings.rpm, winder::actualRpm(settings.rpm));
                lastProgressMs = millis();
                samples[0]     = {0, micros()};
                sampleCount    = 1;
                lastPollMs     = millis();
                commFailures    = 0;
                otWarned        = false;
            }
        }
        else if (!strcmp(cmd, "release"))
        {
            if (rejectWhileRunning()) return;
            winder::enableDriver(false);
            Serial.println("driver disabled");
        }
        else if (!strcmp(cmd, "init"))
        {
            if (rejectWhileRunning()) return;
            tmc::begin();
        }
        else if (*cmd)
        {
            Serial.printf("ERR: unknown command '%s' (try 'help')\n", cmd);
        }
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

    // Polls the driver while winding; aborts the run on a fault that makes
    // the step count unreliable.
    void watchDriver()
    {
        if (!winder::isRunning() || millis() - lastPollMs < FAULT_POLL_MS) return;
        lastPollMs = millis();

        bool otpw        = false;
        tmc::Fault fault = tmc::poll(&otpw);

        if (fault == tmc::Fault::NoComm)
        {
            if (++commFailures < COMM_FAILURES_TO_ABORT) return; // ride out a single noisy read
        }
        else
        {
            commFailures = 0;
        }

        if (fault != tmc::Fault::None)
        {
            winder::abortOnFault(tmc::faultName(fault));
            return;
        }
        if (otpw && !otWarned)
        {
            otWarned = true;
            Serial.println("WARNING: driver over-temperature pre-warning (otpw)");
        }
    }

    void printProgress()
    {
        if (!winder::isRunning() || millis() - lastProgressMs < PROGRESS_INTERVAL_MS) return;
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

    Serial.println("\nPickup winder — step 1");
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
    watchDriver();
    printProgress();
    ui::update();

    if (winder::update())
    {
        winder::printSummary();
        if (winder::lastOutcome() == winder::Outcome::Faulted)
        {
            winder::enableDriver(false);
            Serial.println("driver disabled after fault — check hardware, then 'init'");
        }
    }

    ui::noteLoopTime(micros() - t0);
}
