#include "app.h"
#include "config.h"
#include "tmc_driver.h"
#include <EEPROM.h>

namespace app
{
    namespace
    {
        winder::Settings current{DEFAULT_TURNS, DEFAULT_RPM, DEFAULT_ACCEL_RPM_PER_S, winder::Dir::CW};
        constexpr TraverseSettings TRAV_DEFAULTS{false,         DEFAULT_AWG,       false, DEFAULT_WIDTH_UM,
                                                 DEFAULT_MARGIN_UM, DEFAULT_FLANGE_UM, true};
        TraverseSettings trav = TRAV_DEFAULTS;

        // Settings in EEPROM, versioned and checksummed
        struct Stored
        {
            uint32_t magic;
            uint16_t version;
            winder::Settings s;
            TraverseSettings t;
            uint16_t currentMa; // spindle run current
            uint32_t check;
        };
        struct StoredV2 // guide set by wire / width / offset in um
        {
            uint32_t magic;
            uint16_t version;
            winder::Settings s;
            struct
            {
                bool on;
                uint32_t wireUm, widthUm, offsetUm;
                bool firstAway;
            } t;
            uint16_t currentMa;
            uint32_t check;
        };
        struct StoredV1 // before the traverse
        {
            uint32_t magic;
            uint16_t version;
            winder::Settings s;
            uint32_t check;
        };
        constexpr uint32_t SETTINGS_MAGIC   = 0x57494E44; // "WIND"
        constexpr uint16_t SETTINGS_VERSION = 3;

        // Driver fault watch while winding: spindle and traverse take turns
        uint32_t lastPollMs  = 0;
        uint32_t pollCount   = 0;
        uint8_t commFailures[2] = {0, 0};
        bool otWarned        = false;

        // Spindle release after a run (paused runs keep holding)
        bool idleReleaseArmed = false;
        uint32_t idleSinceMs  = 0;
        constexpr uint8_t COMM_FAILURES_TO_ABORT = 3;

        // The run with the traverse
        traverse_math::Pattern pattern{};
        bool positioningNow     = false;
        const char* startErr    = nullptr;
        bool travRun            = false;
        bool travMscntValid     = false;
        uint16_t travMscntStart = 0, travMscntEnd = 0;
        bool travMscntPending   = false; // end read waits for the driver to settle
        bool travPulseDone      = false; // the end read happened this pass
        uint32_t travEndMs      = 0;

        // With intpol the driver spreads each step over the previous step
        // interval, so MSCNT read right after a step can be between two
        // microsteps. Both reads wait for the guide to settle: after the move
        // to the start position, and after the run (whose last guide steps
        // are sparse, one per ~14 spindle steps).
        constexpr uint32_t MSCNT_SETTLE_MS = 300;
        bool atStart       = false; // positioning: the guide is at the start, settling
        uint32_t atStartMs = 0;
        int32_t travStartPos    = 0, travEndPos = 0;
        traverse::Event travEvent = traverse::Event::None;

        constexpr const char* BUSY = "not allowed during a run";

        template <typename T>
        uint32_t checksum(const T& d)
        {
            // FNV-1a over everything before `check`
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&d);
            uint32_t h       = 2166136261u;
            for (size_t i = 0; i < offsetof(T, check); i++) h = (h ^ p[i]) * 16777619u;
            return h;
        }

        // Saved before the RPM range was narrowed: bring it into range rather
        // than discard every setting
        const winder::Settings& clampRpm(winder::Settings& s)
        {
            s.rpm = constrain(s.rpm, MIN_RPM, MAX_RPM);
            return s;
        }

        bool valid(const winder::Settings& s)
        {
            return !winder::checkTurns(s.turns) && !winder::checkRpm(s.rpm) && !winder::checkAccel(s.accelRpmPerS) &&
                   (s.dir == winder::Dir::CW || s.dir == winder::Dir::CCW);
        }

        const WireSize* wireSize(uint32_t awg)
        {
            for (const WireSize& w : WIRE_SIZES)
                if (w.awg == awg) return &w;
            return nullptr;
        }

        bool valid(const TraverseSettings& t)
        {
            return wireSize(t.awg) && t.widthUm >= MIN_WIDTH_UM && t.widthUm <= MAX_WIDTH_UM &&
                   t.marginUm <= MAX_MARGIN_UM && t.flangeUm <= TRAVERSE_TRAVEL_UM;
        }

        bool validCurrent(uint16_t mA)
        {
            return mA >= MIN_CURRENT_MA && mA <= MAX_CURRENT_MA;
        }

        uint32_t roundTenth(uint32_t um)
        {
            return (um + 50) / 100 * 100;
        }

        void save()
        {
            Stored d;
            memset(&d, 0, sizeof(d)); // padding too, for a stable checksum
            d.magic   = SETTINGS_MAGIC;
            d.version = SETTINGS_VERSION;
            d.s       = current;
            d.t         = trav;
            d.currentMa = tmc::spindle.current();
            d.check     = checksum(d);
            EEPROM.put(EEPROM_ADDR_SETTINGS, d);
        }

        bool makeRunPattern()
        {
            Winding w = winding();
            return w.turnsPerLayer && traverse_math::makePattern(&pattern, w.pitchUm, w.spanUm, w.startUm,
                                                                 trav.firstAway, TRAVERSE_STEPS_PER_MM, STEPS_PER_REV);
        }

        void watchDriver()
        {
            if (!winder::isMoving() || millis() - lastPollMs < FAULT_POLL_MS) return;
            lastPollMs = millis();

            if (travRun && traverse::limitHit())
            {
                winder::abortOnFault("traverse reached a soft limit");
                return;
            }

            // One driver per poll: each UART read blocks the loop ~5 ms
            uint8_t i         = travRun && (pollCount++ & 1) ? 1 : 0;
            tmc::Driver& drv  = i ? tmc::traverse : tmc::spindle;
            bool otpw         = false;
            tmc::Fault fault  = drv.poll(&otpw);

            if (fault == tmc::Fault::NoComm)
            {
                if (++commFailures[i] < COMM_FAILURES_TO_ABORT) return; // ride out a single noisy read
            }
            else
            {
                commFailures[i] = 0;
            }

            if (fault != tmc::Fault::None)
            {
                static char reason[64];
                if (i) snprintf(reason, sizeof(reason), "traverse: %s", tmc::faultName(fault));
                winder::abortOnFault(i ? reason : tmc::faultName(fault));
                return;
            }
            if (otpw && !otWarned)
            {
                otWarned = true;
                Serial.printf("WARNING: %s driver over-temperature pre-warning (otpw)\n", drv.config().name);
            }
        }

        void resetWatch()
        {
            lastPollMs      = millis();
            commFailures[0] = commFailures[1] = 0;
        }

        const char* startSpindle()
        {
            if (const char* err = winder::start(current)) return err;
            resetWatch();
            otWarned = false;
            return nullptr;
        }

        // Positioning done: start the spindle with the guide following
        void startWithTraverse()
        {
            positioningNow = false;
            if (traverse::positionSteps() != traverse_math::position(pattern, 0))
            {
                startErr = traverse::lastError() ? traverse::lastError() : "traverse did not reach the start";
                Serial.printf("ERR: %s\n", startErr);
                return;
            }
            travMscntValid = tmc::traverse.readMscnt(&travMscntStart);
            travStartPos   = traverse::positionSteps();
            if ((startErr = startSpindle()))
            {
                Serial.printf("ERR: %s\n", startErr);
                return;
            }
            traverse::follow(pattern); // the spindle has made at most a step by now
            travRun = true;
        }

        void endTraverseRun()
        {
            traverse::stopFollow();
            travEndPos = traverse::positionSteps();
            if (winder::lastOutcome() == winder::Outcome::Faulted) travMscntValid = false;
            travMscntPending = travMscntValid;
            travEndMs        = millis();
        }

        void readTraverseMscntEnd()
        {
            travPulseDone = false;
            if (!travMscntPending || millis() - travEndMs < MSCNT_SETTLE_MS) return;
            travMscntPending = false;
            travMscntValid   = tmc::traverse.readMscnt(&travMscntEnd);
            travPulseDone    = true;
        }
    }

    void begin()
    {
        Stored d;
        EEPROM.get(EEPROM_ADDR_SETTINGS, d);
        if (d.magic == SETTINGS_MAGIC && d.version == SETTINGS_VERSION && d.check == checksum(d) && valid(clampRpm(d.s)) &&
            valid(d.t) && validCurrent(d.currentMa))
        {
            current = d.s;
            trav    = d.t;
            tmc::spindle.setCurrent(d.currentMa); // before the driver is configured: used by begin()
            return;
        }

        StoredV2 v2;
        EEPROM.get(EEPROM_ADDR_SETTINGS, v2);
        if (v2.magic == SETTINGS_MAGIC && v2.version == 2 && v2.check == checksum(v2) && valid(clampRpm(v2.s)) &&
            validCurrent(v2.currentMa))
        {
            current = v2.s;
            tmc::spindle.setCurrent(v2.currentMa);
            save(); // as version 3: the guide is now set by AWG / margin / flange, back to defaults
            Serial.println("settings: upgraded (guide settings reset: AWG, margin, flange)");
            return;
        }

        StoredV1 v1;
        EEPROM.get(EEPROM_ADDR_SETTINGS, v1);
        if (v1.magic == SETTINGS_MAGIC && v1.version == 1 && v1.check == checksum(v1) && valid(clampRpm(v1.s)))
        {
            current = v1.s;
            save(); // as version 2, traverse settings at their defaults
            Serial.println("settings: upgraded (traverse settings added, traverse off)");
            return;
        }
        Serial.println("settings: none saved yet (or invalid) - using defaults");
    }

    winder::Event update()
    {
        travEvent = traverse::update();
        if (positioningNow && !traverse::busy())
        {
            if (!atStart)
            {
                atStart   = true;
                atStartMs = millis();
            }
            else if (millis() - atStartMs >= MSCNT_SETTLE_MS) startWithTraverse();
        }
        readTraverseMscntEnd();

        watchDriver();
        winder::Event e = winder::update();
        if (e == winder::Event::Ended)
        {
            idleReleaseArmed = true;
            idleSinceMs      = millis();
            if (travRun) endTraverseRun();
            if (winder::lastOutcome() == winder::Outcome::Faulted)
            {
                winder::enableDriver(false);
                const char* why = winder::faultReason();
                if (travRun && why && !strncmp(why, "traverse", 8)) traverse::enableDriver(false);
            }
        }

        // Nobody needs the spindle held long after a run: free it, no heat
        if (idleReleaseArmed && !winder::inRun() && !positioningNow && millis() - idleSinceMs >= IDLE_RELEASE_MS)
        {
            idleReleaseArmed = false;
            if (winder::driverEnabled())
            {
                winder::enableDriver(false);
                Serial.printf("spindle released after %lu min idle (motor free)\n", IDLE_RELEASE_MS / 60000);
            }
        }
        return e;
    }

    traverse::Event traverseEvent()
    {
        return travEvent;
    }

    const winder::Settings& settings()
    {
        return current;
    }

    const TraverseSettings& traverseSettings()
    {
        return trav;
    }

    const char* setTurns(uint32_t turns)
    {
        if (winder::inRun()) return BUSY;
        if (const char* err = winder::checkTurns(turns)) return err;
        current.turns = turns;
        save();
        return nullptr;
    }

    const char* setRpm(uint32_t rpm)
    {
        if (winder::inRun()) return BUSY;
        if (const char* err = winder::checkRpm(rpm)) return err;
        current.rpm = rpm;
        save();
        return nullptr;
    }

    const char* setAccel(uint32_t accelRpmPerS)
    {
        if (winder::inRun()) return BUSY;
        if (const char* err = winder::checkAccel(accelRpmPerS)) return err;
        current.accelRpmPerS = accelRpmPerS;
        save();
        return nullptr;
    }

    const char* setDir(winder::Dir dir)
    {
        if (winder::inRun()) return BUSY;
        current.dir = dir;
        save();
        return nullptr;
    }

    const char* setCurrent(uint32_t mA)
    {
        static char msg[40];
        if (winder::inRun() || positioningNow) return BUSY;
        if (mA < MIN_CURRENT_MA || mA > MAX_CURRENT_MA)
        {
            snprintf(msg, sizeof(msg), "current must be %u..%u mA", MIN_CURRENT_MA, MAX_CURRENT_MA);
            return msg;
        }
        if (!tmc::spindle.setCurrent(mA)) return "driver did not take it - check 24 V, then re-init";
        save();
        return nullptr;
    }

    uint16_t spindleCurrent()
    {
        return tmc::spindle.current();
    }

    bool driverHot()
    {
        return otWarned;
    }

    const char* setTraverseOn(bool on)
    {
        if (winder::inRun() || positioningNow) return BUSY;
        trav.on = on;
        save();
        return nullptr;
    }

    const char* setAwg(uint32_t awg)
    {
        if (winder::inRun() || positioningNow) return BUSY;
        if (!wireSize(awg)) return "AWG must be 42, 43 or 44";
        trav.awg = awg;
        save();
        return nullptr;
    }

    const char* setHeavy(bool heavy)
    {
        if (winder::inRun() || positioningNow) return BUSY;
        trav.heavy = heavy;
        save();
        return nullptr;
    }

    const char* setWidth(uint32_t um)
    {
        static char msg[40];
        if (winder::inRun() || positioningNow) return BUSY;
        um = roundTenth(um);
        if (um < MIN_WIDTH_UM || um > MAX_WIDTH_UM)
        {
            snprintf(msg, sizeof(msg), "width must be %lu..%lu mm", MIN_WIDTH_UM / 1000, MAX_WIDTH_UM / 1000);
            return msg;
        }
        trav.widthUm = um;
        save();
        return nullptr;
    }

    const char* setMargin(uint32_t um)
    {
        static char msg[40];
        if (winder::inRun() || positioningNow) return BUSY;
        um = roundTenth(um);
        if (um > MAX_MARGIN_UM)
        {
            snprintf(msg, sizeof(msg), "margin must be 0..%lu mm", MAX_MARGIN_UM / 1000);
            return msg;
        }
        trav.marginUm = um;
        save();
        return nullptr;
    }

    const char* setFlange1(uint32_t um)
    {
        if (winder::inRun() || positioningNow) return BUSY;
        um = roundTenth(um);
        if (um > TRAVERSE_TRAVEL_UM - MIN_WIDTH_UM) return "flange 1 must be 0..48 mm";
        // Flange 2 stays put, unless that leaves less than the minimum width
        uint32_t flange2 = trav.flangeUm + trav.widthUm;
        if (flange2 >= um + MIN_WIDTH_UM && flange2 - um <= MAX_WIDTH_UM) trav.widthUm = flange2 - um;
        trav.flangeUm = um;
        save();
        return nullptr;
    }

    const char* setFlange2(uint32_t um)
    {
        static char msg[48];
        if (winder::inRun() || positioningNow) return BUSY;
        um = roundTenth(um);
        if (um > TRAVERSE_TRAVEL_UM) return "flange 2 must be within 50 mm";
        if (um < trav.flangeUm + MIN_WIDTH_UM)
        {
            snprintf(msg, sizeof(msg), "flange 2: at least %lu mm past flange 1", MIN_WIDTH_UM / 1000);
            return msg;
        }
        trav.widthUm = um - trav.flangeUm;
        save();
        return nullptr;
    }

    namespace
    {
        const char* carriageUm(uint32_t* um)
        {
            if (winder::inRun() || positioningNow) return BUSY;
            if (!traverse::homed()) return "home the guide first";
            if (traverse::busy()) return "guide is moving";
            int32_t v = lroundf(traverse::positionMm() * 1000);
            *um       = v < 0 ? 0 : v;
            return nullptr;
        }
    }

    const char* setFlange1Here()
    {
        uint32_t um;
        if (const char* err = carriageUm(&um)) return err;
        return setFlange1(um);
    }

    const char* setFlange2Here()
    {
        uint32_t um;
        if (const char* err = carriageUm(&um)) return err;
        return setFlange2(um);
    }

    const char* setFirstAway(bool away)
    {
        if (winder::inRun() || positioningNow) return BUSY;
        trav.firstAway = away;
        save();
        return nullptr;
    }

    Winding winding()
    {
        Winding w{};
        const WireSize* size = wireSize(trav.awg);
        w.pitchUm            = size ? (trav.heavy ? size->heavyUm : size->singleUm) : MAX_WIRE_UM;
        w.startUm            = trav.flangeUm + trav.marginUm;
        uint32_t room        = trav.widthUm > 2 * trav.marginUm ? trav.widthUm - 2 * trav.marginUm : 0;
        w.turnsPerLayer      = room / w.pitchUm;
        w.spanUm             = w.turnsPerLayer * w.pitchUm;
        w.layers             = w.turnsPerLayer ? (current.turns + w.turnsPerLayer - 1) / w.turnsPerLayer : 0;
        return w;
    }

    const char* checkTraverse()
    {
        static char msg[48];
        Winding w = winding();
        if (!w.turnsPerLayer) return "margins leave no room to wind";
        if (trav.flangeUm + trav.widthUm > TRAVERSE_TRAVEL_UM) return "bobbin past 50 mm - check flange / width";
        float followUmS = w.pitchUm * winder::actualRpm(current.rpm) / 60.0f;
        if (followUmS > TRAVERSE_MAX_FOLLOW_UM_S)
        {
            snprintf(msg, sizeof(msg), "guide too slow for %lu rpm (max %lu mm/s)", current.rpm,
                     TRAVERSE_MAX_FOLLOW_UM_S / 1000);
            return msg;
        }
        return nullptr;
    }

    const char* start()
    {
        if (positioningNow) return "moving to the start position";
        startErr         = nullptr;
        travRun          = false;
        travMscntPending = false;
        if (!trav.on) return startSpindle();

        if (const char* err = winder::canStart(current)) return err;
        if (!tmc::traverse.ready()) return "guide driver not ready - check 24 V, re-init";
        if (!traverse::homed()) return "traverse not homed - home it first";
        if (const char* err = checkTraverse()) return err;
        tmc::Fault f = tmc::traverse.poll(nullptr);
        if (f != tmc::Fault::None)
        {
            static char reason[64];
            snprintf(reason, sizeof(reason), "traverse: %s", tmc::faultName(f));
            return reason;
        }
        if (!makeRunPattern()) return "margins leave no room to wind";
        if (const char* err = traverse::moveToSteps(traverse_math::position(pattern, 0))) return err;
        positioningNow = true;
        atStart        = false;
        return nullptr;
    }

    bool positioning()
    {
        return positioningNow;
    }

    const char* startError()
    {
        return startErr;
    }

    const char* resume()
    {
        if (travRun && !tmc::traverse.ready()) return "guide driver not ready - check 24 V, re-init";
        if (const char* err = winder::resume()) return err;
        resetWatch();
        return nullptr;
    }

    const char* finish()
    {
        return winder::finish();
    }

    void stop()
    {
        if (positioningNow)
        {
            positioningNow = false;
            startErr       = "start cancelled";
        }
        traverse::stop();
        winder::stop();
    }

    void estop()
    {
        positioningNow = false;
        traverse::stop();
        winder::estop();
    }

    const char* home()
    {
        if (winder::inRun() || positioningNow) return BUSY;
        return traverse::home();
    }

    const char* jog(int32_t um)
    {
        if (winder::inRun() || positioningNow) return BUSY;
        return traverse::jog(um);
    }

    const char* moveTo(int32_t um)
    {
        if (winder::inRun() || positioningNow) return BUSY;
        return traverse::moveTo(um);
    }

    const char* release()
    {
        if (winder::inRun() || positioningNow) return BUSY;
        winder::enableDriver(false);
        traverse::enableDriver(false);
        return nullptr;
    }

    const char* reinitDriver()
    {
        if (winder::inRun() || positioningNow) return BUSY;
        bool ok = tmc::spindle.begin();
        // The traverse is optional until switched on. Re-configuring it may
        // reset its microstep position, so it must be homed again.
        if (tmc::traverse.begin(!trav.on) || trav.on) traverse::enableDriver(false);
        if (!ok) return "driver setup failed - check 24 V and UART";
        if (trav.on && !tmc::traverse.ready()) return "traverse driver setup failed - check 24 V and UART";
        return nullptr;
    }

    bool traverseRun()
    {
        return travRun;
    }

    const traverse_math::Pattern& runPattern()
    {
        return pattern;
    }

    bool traversePulseChecked()
    {
        return travPulseDone;
    }

    void traverseMscnt(uint16_t* start, uint16_t* end, int32_t* netSteps)
    {
        *start    = travMscntStart;
        *end      = travMscntEnd;
        *netSteps = travEndPos - travStartPos;
    }

    winder::PulseCheck traversePulseCheck(int16_t* offsetSteps)
    {
        if (offsetSteps) *offsetSteps = 0;
        if (winder::lastOutcome() == winder::Outcome::Faulted) return winder::PulseCheck::Skipped;
        if (!travMscntValid) return winder::PulseCheck::Unavailable;
        if (travMscntPending) return winder::PulseCheck::Pending;
        return winder::checkMscnt(travMscntStart, travMscntEnd, (uint32_t)abs(travEndPos - travStartPos), offsetSteps);
    }
}
