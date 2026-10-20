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

    void printStatus()
    {
        uint8_t ver = driver.version();
        if (driver.CRCerror)
        {
            Serial.println("TMC2209: no response (check 24V motor power and UART wiring)");
            return;
        }
        uint32_t gconf    = driver.GCONF();
        uint32_t chopconf = driver.CHOPCONF();
        uint32_t drv      = driver.DRV_STATUS();
        TMC2208_n::DRV_STATUS_t st{0};
        st.sr = drv;

        Serial.printf("TMC2209 version 0x%02X  configured=%s\n", ver, configured ? "yes" : "NO");
        Serial.printf("  GCONF    0x%08lX  spreadCycle=%d pdn_disable=%d mstep_reg=%d I_scale_analog=%d\n",
                      gconf, driver.en_spreadCycle(), driver.pdn_disable(), driver.mstep_reg_select(), driver.I_scale_analog());
        Serial.printf("  CHOPCONF 0x%08lX  microsteps=%u intpol=%d dedge=%d toff=%u hstrt=%u hend=%u\n",
                      chopconf, driver.microsteps(), driver.intpol(), driver.dedge(), driver.toff(), driver.hstrt(), driver.hend());
        Serial.printf("  current  %u mA RMS (set)  cs_actual=%u\n", driver.rms_current(), (unsigned)st.cs_actual);
        Serial.printf("  DRV_STATUS 0x%08lX  ot=%d otpw=%d s2ga=%d s2gb=%d s2vsa=%d s2vsb=%d ola=%d olb=%d stst=%d\n",
                      drv, st.ot, st.otpw, st.s2ga, st.s2gb, st.s2vsa, st.s2vsb, st.ola, st.olb, st.stst);
        GSTAT_t gs{0};
        gs.sr = driver.GSTAT();
        Serial.printf("  GSTAT reset=%d drv_err=%d uv_cp=%d   MSCNT %u\n", gs.reset, gs.drv_err, gs.uv_cp, driver.MSCNT());
        Serial.printf("  UART %s\n", driver.CRCerror ? "CRC ERROR" : "ok");
    }
}
