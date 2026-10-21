#include "tmc_driver.h"
#include "config.h"
#include <TMCStepper.h>

namespace tmc
{
    namespace
    {
        TMC2209Stepper driver(&TMC_SERIAL, R_SENSE, DRIVER_ADDR);
        bool configured = false;

        constexpr uint8_t TMC2209_VERSION = 0x21;

        bool check(bool ok, const char* what)
        {
            if (!ok) Serial.printf("TMC2209: verify FAILED: %s\n", what);
            return ok;
        }

        // RMS current for a current scale (CS 0..31), as TMCStepper computes it
        uint16_t csToMa(uint8_t cs, bool vsense)
        {
            return (cs + 1) / 32.0f * (vsense ? 0.180f : 0.325f) / (R_SENSE + 0.02f) / 1.41421f * 1000;
        }
    }

    bool begin()
    {
        configured = false;
        TMC_SERIAL.begin(TMC_BAUD);
        delay(10);

        // Communication check — the driver only answers when VM (motor power) is on
        uint8_t ver = driver.version();
        if (driver.CRCerror || ver != TMC2209_VERSION)
        {
            Serial.printf("TMC2209: no response (version=0x%02X). Check 24V motor power and UART wiring.\n", ver);
            return false;
        }

        uint8_t ifcntBefore = driver.IFCNT();

        driver.begin();                  // pdn_disable(true), mstep_reg_select(true)
        driver.I_scale_analog(false);    // current from register, not VREF
        driver.en_spreadCycle(true);     // spreadCycle, not stealthChop
        driver.pwm_autoscale(false);

        // Chopper: Trinamic's recommended spreadCycle starting point
        driver.toff(4);
        driver.blank_time(24);
        driver.hstrt(5);
        driver.hend(0);
        driver.intpol(true);             // interpolate to 256 microsteps
        driver.dedge(false);             // TeensyStep4 makes real pulses: step on rising edge only
        driver.microsteps(MICROSTEPS);

        driver.rms_current(RMS_CURRENT_MA);
        driver.iholddelay(10);
        driver.TPOWERDOWN(20);

        driver.GSTAT(0b111); // clear reset/drv_err/uv_cp so poll() sees new events only

        // Read back and verify — refuse to run on a misconfigured driver
        uint8_t ifcntAfter = driver.IFCNT();
        bool ok            = true;
        ok &= check(!driver.CRCerror, "UART CRC error reading IFCNT");
        ok &= check(ifcntAfter != ifcntBefore, "IFCNT did not change (writes not received)");
        ok &= check(driver.en_spreadCycle(), "GCONF.en_spreadcycle != 1");
        ok &= check(driver.pdn_disable(), "GCONF.pdn_disable != 1");
        ok &= check(driver.mstep_reg_select(), "GCONF.mstep_reg_select != 1");
        ok &= check(!driver.I_scale_analog(), "GCONF.I_scale_analog != 0");
        ok &= check(driver.microsteps() == MICROSTEPS, "CHOPCONF.mres != configured microsteps");
        ok &= check(driver.intpol(), "CHOPCONF.intpol != 1");
        ok &= check(!driver.dedge(), "CHOPCONF.dedge != 0");
        ok &= check(driver.toff() == 4, "CHOPCONF.toff != 4");
        ok &= check(!driver.CRCerror, "UART CRC error on readback");

        configured = ok;
        Serial.printf("TMC2209: %s (spreadCycle, %u microsteps, %u mA RMS)\n",
                      ok ? "configured and verified" : "CONFIGURATION FAILED — motion disabled",
                      (unsigned)MICROSTEPS, (unsigned)RMS_CURRENT_MA);
        return ok;
    }

    bool ready()
    {
        return configured;
    }

    Fault poll(bool* otWarning)
    {
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

    bool readMscnt(uint16_t* out)
    {
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

    bool readStatus(Status* s, uint8_t part)
    {
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
                s->runMa           = csToMa(driver.irun(), r.vsense);
                s->holdMa          = csToMa(driver.ihold(), r.vsense);
                break;
            }
            case 3:
            {
                TMC2208_n::DRV_STATUS_t r{0};
                r.sr = s->drvStatus = driver.DRV_STATUS();
                TMC2208_n::CHOPCONF_t c{0};
                c.sr          = s->chopconf; // part 2 comes first
                s->csActual   = r.cs_actual;
                s->currentMa  = csToMa(r.cs_actual, c.vsense);
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

    void printStatus()
    {
        Status s{};
        for (uint8_t part = 0; part < STATUS_READS; part++)
        {
            if (!readStatus(&s, part))
            {
                Serial.println("TMC2209: no response (check 24V motor power and UART wiring)");
                return;
            }
        }

        Serial.printf("TMC2209 version 0x%02X  configured=%s  EN pin %s\n", s.version, configured ? "yes" : "NO",
                      s.enPinOff ? "off (motor free)" : "on");
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
