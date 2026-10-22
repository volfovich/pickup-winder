#include "tmc_driver.h"
#include "config.h"
#include <TMCStepper.h>

namespace tmc
{
    namespace
    {
        TMC2209Stepper spindleChip(&TMC_SERIAL, R_SENSE, DRIVER_ADDR);
        TMC2209Stepper traverseChip(&TMC_SERIAL, TRAVERSE_R_SENSE, TRAVERSE_DRIVER_ADDR);
        bool busStarted = false;

        constexpr uint8_t TMC2209_VERSION = 0x21;

        TMC2209Stepper& chipFor(const Config& c)
        {
            return c.address == TRAVERSE_DRIVER_ADDR ? traverseChip : spindleChip;
        }

        bool check(const Config& c, bool ok, const char* what)
        {
            if (!ok) Serial.printf("TMC2209 %s: verify FAILED: %s\n", c.name, what);
            return ok;
        }

        // RMS current for a current scale (CS 0..31), as TMCStepper computes it
        uint16_t csToMa(uint8_t cs, bool vsense, float rSense)
        {
            return (cs + 1) / 32.0f * (vsense ? 0.180f : 0.325f) / (rSense + 0.02f) / 1.41421f * 1000;
        }
    }

    Driver spindle({"spindle", DRIVER_ADDR, R_SENSE, RMS_CURRENT_MA, SPINDLE_HOLD_PCT, MICROSTEPS, false, 0});
    Driver traverse({"traverse", TRAVERSE_DRIVER_ADDR, TRAVERSE_R_SENSE, TRAVERSE_RMS_CURRENT_MA, TRAVERSE_HOLD_PCT,
                     TRAVERSE_MICROSTEPS, TRAVERSE_STEALTHCHOP,
                     TRAVERSE_STEALTH_BELOW_UM_S * TRAVERSE_STEPS_PER_MM / 1000});

    Driver::Driver(const Config& c) : cfg(c), runMa(c.currentMa) {}

    // Sets IRUN / IHOLD (and vsense) for runMa; checks the driver counted the writes
    bool Driver::writeCurrent()
    {
        TMC2209Stepper& driver = chipFor(cfg);
        uint8_t before         = driver.IFCNT();
        driver.rms_current(runMa, cfg.holdPct / 100.0f);
        uint8_t after = driver.IFCNT();
        return !driver.CRCerror && after != before;
    }

    bool Driver::setCurrent(uint16_t mA)
    {
        runMa = mA;
        if (!configured) return true;
        bool ok = writeCurrent();
        Serial.printf("TMC2209 %s: run current %u mA RMS, hold %u%% %s\n", cfg.name, runMa, cfg.holdPct,
                      ok ? "(written)" : "- WRITE FAILED");
        return ok;
    }

    bool Driver::begin(bool quiet)
    {
        TMC2209Stepper& driver = chipFor(cfg);
        configured             = false;
        if (!busStarted)
        {
            TMC_SERIAL.begin(TMC_BAUD);
            delay(10);
            busStarted = true;
        }

        // Communication check — the driver only answers when VM (motor power) is on
        uint8_t ver = driver.version();
        if (driver.CRCerror || ver != TMC2209_VERSION)
        {
            if (!quiet)
                Serial.printf("TMC2209 %s (address %u): no response (version=0x%02X). Check 24V motor power and UART "
                              "wiring.\n",
                              cfg.name, cfg.address, ver);
            return false;
        }

        uint8_t ifcntBefore = driver.IFCNT();

        driver.begin();                  // pdn_disable(true), mstep_reg_select(true)
        driver.I_scale_analog(false);    // current from register, not VREF
        bool stealth = cfg.usesStealthChop();
        driver.en_spreadCycle(!stealth);
        driver.pwm_autoscale(stealth); // stealthChop tunes its own current regulation
        driver.pwm_autograd(stealth);
        // Hybrid: stealthChop while TSTEP (12 MHz clocks per 1/256 microstep)
        // is above TPWMTHRS, i.e. below the step rate; 0 = no switching
        uint32_t tpwmthrs = 0;
        if (!cfg.stealthChop && cfg.stealthBelowStepsPerS)
            tpwmthrs = min<uint32_t>(12000000ul / (cfg.stealthBelowStepsPerS * (256 / cfg.microsteps)), 0xFFFFFu);
        driver.TPWMTHRS(tpwmthrs); // write-only register: not verified below

        // Chopper: Trinamic's recommended spreadCycle starting point
        driver.toff(4);
        driver.blank_time(24);
        driver.hstrt(5);
        driver.hend(0);
        driver.intpol(true);             // interpolate to 256 microsteps
        driver.dedge(false);             // real pulses: step on rising edge only
        driver.microsteps(cfg.microsteps);

        writeCurrent();
        driver.iholddelay(10);
        driver.TPOWERDOWN(20);

        driver.GSTAT(0b111); // clear reset/drv_err/uv_cp so poll() sees new events only

        // Read back and verify — refuse to run on a misconfigured driver
        uint8_t ifcntAfter = driver.IFCNT();
        bool ok            = true;
        ok &= check(cfg, !driver.CRCerror, "UART CRC error reading IFCNT");
        ok &= check(cfg, ifcntAfter != ifcntBefore, "IFCNT did not change (writes not received)");
        ok &= check(cfg, driver.en_spreadCycle() == !stealth, "GCONF.en_spreadcycle wrong");
        ok &= check(cfg, driver.pdn_disable(), "GCONF.pdn_disable != 1");
        ok &= check(cfg, driver.mstep_reg_select(), "GCONF.mstep_reg_select != 1");
        ok &= check(cfg, !driver.I_scale_analog(), "GCONF.I_scale_analog != 0");
        ok &= check(cfg, driver.microsteps() == cfg.microsteps, "CHOPCONF.mres != configured microsteps");
        ok &= check(cfg, driver.intpol(), "CHOPCONF.intpol != 1");
        ok &= check(cfg, !driver.dedge(), "CHOPCONF.dedge != 0");
        ok &= check(cfg, driver.toff() == 4, "CHOPCONF.toff != 4");
        ok &= check(cfg, !driver.CRCerror, "UART CRC error on readback");

        configured = ok;
        Serial.printf("TMC2209 %s: %s (%s, %u microsteps, %u mA RMS)\n", cfg.name,
                      ok ? "configured and verified" : "CONFIGURATION FAILED — motion disabled",
                      cfg.modeName(), cfg.microsteps, runMa);
        return ok;
    }

    Fault Driver::poll(bool* otWarning)
    {
        TMC2209Stepper& driver = chipFor(cfg);
        uint8_t g = driver.GSTAT();
        if (driver.CRCerror) return Fault::NoComm;

        GSTAT_t gs{0};
        gs.sr = g;
        if (gs.reset) return Fault::DriverReset;
        if (gs.uv_cp) return Fault::Undervoltage;

        uint32_t s = driver.DRV_STATUS();
        if (driver.CRCerror) return Fault::NoComm;

        TMC2208_n::DRV_STATUS_t st{0};
        st.sr = s;
        if (otWarning) *otWarning = st.otpw;
        if (st.ot) return Fault::OverTemp;
        if (st.s2ga || st.s2gb) return Fault::ShortToGround;
        if (st.s2vsa || st.s2vsb) return Fault::ShortToSupply;
        if (gs.drv_err) return Fault::DriverError;
        return Fault::None;
    }

    bool Driver::readMscnt(uint16_t* out)
    {
        TMC2209Stepper& driver = chipFor(cfg);
        uint16_t v = driver.MSCNT();
        if (driver.CRCerror) return false;
        *out = v & 0x3FF;
        return true;
    }

    const char* faultName(Fault f)
    {
        switch (f)
        {
            case Fault::None: return "none";
            case Fault::NoComm: return "UART not responding (motor power lost?)";
            case Fault::OverTemp: return "driver over-temperature";
            case Fault::ShortToGround: return "short to ground";
            case Fault::ShortToSupply: return "short to supply";
            case Fault::DriverReset: return "driver was reset (motor power dip?)";
            case Fault::Undervoltage: return "driver charge pump undervoltage";
            case Fault::DriverError: return "driver shut down (drv_err)";
        }
        return "?";
    }

    bool Driver::readStatus(Status* s, uint8_t part)
    {
        TMC2209Stepper& driver = chipFor(cfg);
        switch (part)
        {
            case 0:
            {
                TMC2209_n::IOIN_t r{0};
                r.sr = s->ioin = driver.IOIN();
                s->version     = r.version;
                s->enPinOff    = r.enn;
                break;
            }
            case 1:
            {
                TMC2208_n::GCONF_t r{0};
                r.sr = s->gconf = driver.GCONF();
                s->spreadCycle  = r.en_spreadcycle;
                s->pdnDisable   = r.pdn_disable;
                s->mstepReg     = r.mstep_reg_select;
                s->iScaleAnalog = r.i_scale_analog;
                break;
            }
            case 2:
            {
                TMC2208_n::CHOPCONF_t r{0};
                r.sr = s->chopconf = driver.CHOPCONF();
                s->microsteps      = 256 >> r.mres;
                s->intpol          = r.intpol;
                s->dedge           = r.dedge;
                s->toff            = r.toff;
                s->hstrt           = r.hstrt;
                s->hend            = r.hend;
                s->tblClocks       = 16 + 8 * r.tbl;
                s->runMa           = csToMa(driver.irun(), r.vsense, cfg.rSense);
                s->holdMa          = csToMa(driver.ihold(), r.vsense, cfg.rSense);
                break;
            }
            case 3:
            {
                TMC2208_n::DRV_STATUS_t r{0};
                r.sr = s->drvStatus = driver.DRV_STATUS();
                TMC2208_n::CHOPCONF_t c{0};
                c.sr          = s->chopconf; // part 2 comes first
                s->csActual   = r.cs_actual;
                s->currentMa  = csToMa(r.cs_actual, c.vsense, cfg.rSense);
                s->standstill = r.stst;
                s->otpw       = r.otpw;
                s->ot         = r.ot;
                s->s2ga       = r.s2ga;
                s->s2gb       = r.s2gb;
                s->s2vsa      = r.s2vsa;
                s->s2vsb      = r.s2vsb;
                s->ola        = r.ola;
                s->olb        = r.olb;
                s->tempOver   = r.t157 ? 157 : r.t150 ? 150 : r.t143 ? 143 : r.t120 ? 120 : 0;
                break;
            }
            case 4:
            {
                GSTAT_t r{0};
                r.sr = s->gstat = driver.GSTAT();
                s->reset        = r.reset;
                s->drvErr       = r.drv_err;
                s->uvCp         = r.uv_cp;
                break;
            }
            case 5: s->mscnt = driver.MSCNT() & 0x3FF; break;
            default: return false;
        }
        return !driver.CRCerror;
    }

    void Driver::printStatus()
    {
        Status s{};
        for (uint8_t part = 0; part < STATUS_READS; part++)
        {
            if (!readStatus(&s, part))
            {
                Serial.printf("TMC2209 %s (address %u): no response (check 24V motor power and UART wiring)\n",
                              cfg.name, cfg.address);
                return;
            }
        }

        Serial.printf("TMC2209 %s (address %u) version 0x%02X  configured=%s  EN pin %s\n", cfg.name, cfg.address,
                      s.version, configured ? "yes" : "NO", s.enPinOff ? "off (motor free)" : "on");
        Serial.printf("  GCONF    0x%08lX  spreadCycle=%d pdn_disable=%d mstep_reg=%d I_scale_analog=%d\n", s.gconf,
                      s.spreadCycle, s.pdnDisable, s.mstepReg, s.iScaleAnalog);
        Serial.printf("  CHOPCONF 0x%08lX  microsteps=%u intpol=%d dedge=%d toff=%u hstrt=%u hend=%u tbl=%u\n",
                      s.chopconf, s.microsteps, s.intpol, s.dedge, s.toff, s.hstrt, s.hend, s.tblClocks);
        Serial.printf("  current  %u mA RMS run, %u mA hold (set)  now %u mA (cs_actual=%u)\n", s.runMa, s.holdMa,
                      s.currentMa, s.csActual);
        Serial.printf("  DRV_STATUS 0x%08lX  ot=%d otpw=%d s2ga=%d s2gb=%d s2vsa=%d s2vsb=%d ola=%d olb=%d stst=%d\n",
                      s.drvStatus, s.ot, s.otpw, s.s2ga, s.s2gb, s.s2vsa, s.s2vsb, s.ola, s.olb, s.standstill);
        if (s.tempOver) Serial.printf("  temperature above %u C\n", s.tempOver);
        else Serial.println("  temperature below 120 C");
        Serial.printf("  GSTAT reset=%d drv_err=%d uv_cp=%d   MSCNT %u\n", s.reset, s.drvErr, s.uvCp, s.mscnt);
        Serial.println("  UART ok");
    }
}
