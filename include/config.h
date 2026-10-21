#pragma once

#include <stdint.h>

#define FIRMWARE_NAME    "Pickup Winder NG"
#define FIRMWARE_VERSION "1.0"

// ---------------------------------------------------------------------------
// Pins (Teensy 4.0)
// ---------------------------------------------------------------------------
// TMC2209 single-wire UART: Teensy TX1 (pin 1) --[1k]--> PDN_UART <-- Teensy RX1 (pin 0)
#define TMC_SERIAL Serial1
constexpr uint32_t TMC_BAUD = 115200;

constexpr uint8_t PIN_STEP = 2;
constexpr uint8_t PIN_DIR  = 3;
constexpr uint8_t PIN_EN   = 4; // active LOW

// Optional self-test: jumper PIN_STEP to this pin to count the actual STEP
// pulses with an independent interrupt. Comment out to disable.
// #define STEP_LOOPBACK_PIN 5

// Traverse (wire guide): 28T0601-50 slide, its own TMC2209 on the shared UART
constexpr uint8_t PIN_TRAV_STEP = 6;
constexpr uint8_t PIN_TRAV_DIR  = 7;
constexpr uint8_t PIN_TRAV_EN   = 22; // active LOW

// 3-wire endstop module at the home (motor) end: VCC to 3.3 V (not 5 V,
// Teensy pins are not 5 V tolerant), GND, SIG to this pin. Check the level
// with the `endstop` command, pressing the switch by hand.
constexpr uint8_t PIN_ENDSTOP             = 21;
constexpr uint8_t ENDSTOP_TRIGGERED_LEVEL = 1; // 1 = SIG high when pressed

// Display (ILI9341) + touch (HR2046 / XPT2046) on the shared SPI bus:
// MOSI 11, MISO 12, SCK 13. Backlight LED and VCC to 3.3 V.
constexpr uint8_t PIN_TFT_CS    = 10;
constexpr uint8_t PIN_TFT_DC    = 9;
constexpr uint8_t PIN_TFT_RST   = 8;
constexpr uint8_t PIN_TOUCH_CS  = 14;
constexpr uint8_t PIN_TOUCH_IRQ = 15;

// Landscape with the module's pin header on the left. If the picture is
// upside down, use 1 instead (and recalibrate touch).
constexpr uint8_t DISPLAY_ROTATION = 3;

// ---------------------------------------------------------------------------
// EEPROM layout (Teensy 4.0: 1080 bytes)
// ---------------------------------------------------------------------------
constexpr int EEPROM_ADDR_TOUCH_CAL = 0;  // 64 bytes reserved
constexpr int EEPROM_ADDR_SETTINGS  = 64; // settings (UI-2)

// ---------------------------------------------------------------------------
// TMC2209 driver
// ---------------------------------------------------------------------------
constexpr uint8_t  DRIVER_ADDR    = 0;     // MS1 = MS2 = GND
constexpr float    R_SENSE        = 0.11f; // ohms — check your module (BTT/FYSETC: 0.11)
// Motor is rated 2.8 A/phase; the TMC2209 is the limit (~1.77 A RMS max with
// 0.11 ohm sense resistors). 1500 mA needs a heatsink and airflow on the driver.
// This is the default; the `current` command changes it (saved in EEPROM).
// Heat in driver and motor grows with the square of the current.
constexpr uint16_t RMS_CURRENT_MA = 1500;  // motor run current (RMS)
constexpr uint16_t MIN_CURRENT_MA = 400;
constexpr uint16_t MAX_CURRENT_MA = 1700;
// Standstill current, % of the run current: the spindle needs little
// holding (just against wire tension while paused)
constexpr uint8_t  SPINDLE_HOLD_PCT = 25;
// After a run ends, the spindle driver switches off (motor free, no heat)
// after this long. Paused runs keep holding.
constexpr uint32_t IDLE_RELEASE_MS = 120000;

// Traverse driver: MS1 = 3.3 V, MS2 = GND sets UART address 1
constexpr uint8_t  TRAVERSE_DRIVER_ADDR    = 1;
constexpr float    TRAVERSE_R_SENSE        = 0.11f;
constexpr uint16_t TRAVERSE_RMS_CURRENT_MA = 400; // motor rated 0.6 A/phase
constexpr uint8_t  TRAVERSE_HOLD_PCT       = 50;
// spreadCycle by default: the traverse reverses at full speed at every
// layer end, and stealthChop's current regulation copes worse with sudden
// speed changes. stealthChop is quieter; true to try it.
constexpr bool     TRAVERSE_STEALTHCHOP    = false;

// ---------------------------------------------------------------------------
// Motor / mechanics
// ---------------------------------------------------------------------------
constexpr uint32_t FULL_STEPS_PER_REV = 200; // 1.8 deg motor
constexpr uint32_t MICROSTEPS         = 16;  // driver interpolates to 256 internally
constexpr uint32_t STEPS_PER_REV      = FULL_STEPS_PER_REV * MICROSTEPS;

// Traverse: NEMA 11 (1.8 deg), 1 mm lead screw, 50 mm stroke, home (0 mm)
// at the motor end, positions grow away from the motor.
constexpr uint32_t TRAVERSE_MICROSTEPS   = 16;
constexpr uint32_t TRAVERSE_STEPS_PER_MM = 200 * TRAVERSE_MICROSTEPS / 1; // 3200: 0.31 um per step
constexpr uint32_t TRAVERSE_TRAVEL_UM    = 50000;
constexpr bool     TRAVERSE_DIR_INVERT   = false; // set true if `jog 1` moves toward the motor

// ---------------------------------------------------------------------------
// Limits and defaults
// ---------------------------------------------------------------------------
constexpr uint32_t MIN_RPM   = 10; // TeensyStep4's 16-bit timer can't go below ~72 steps/s
constexpr uint32_t MAX_RPM   = 1500;
constexpr uint32_t MAX_TURNS = 11000;

constexpr uint32_t MIN_ACCEL_RPM_PER_S = 10;
constexpr uint32_t MAX_ACCEL_RPM_PER_S = 2000;

constexpr uint32_t DEFAULT_TURNS           = 10;
constexpr uint32_t DEFAULT_RPM             = 300;
constexpr uint32_t DEFAULT_ACCEL_RPM_PER_S = 300; // conservative to avoid stalls

// Traverse step generator: a 40 kHz timer interrupt, at most one step per
// tick (12.5 mm/s at 3200 steps/mm)
constexpr uint32_t TRAVERSE_TICK_HZ       = 40000;
constexpr uint32_t TRAVERSE_MOVE_UM_S     = 10000; // jog / goto / go to start
constexpr uint32_t TRAVERSE_ACCEL_UM_S2   = 50000;
constexpr uint32_t HOMING_FAST_UM_S       = 5000;
constexpr uint32_t HOMING_SLOW_UM_S       = 500;
constexpr uint32_t HOMING_BACKOFF_UM      = 1000;
constexpr uint32_t HOMING_CLEAR_UM        = 500;  // park this far off the switch after homing
constexpr uint32_t HOMING_SEARCH_UM       = 52000;
// Fastest the wire guide may have to follow while winding (wire x RPM / 60)
constexpr uint32_t TRAVERSE_MAX_FOLLOW_UM_S = 6000;

// Wire: outer diameter incl. insulation = the guide's pitch per turn (um).
// Nominal values; adjust to your wire's datasheet.
struct WireSize
{
    uint8_t awg;
    uint16_t singleUm; // single build (plain enamel / poly)
    uint16_t heavyUm;  // heavy build (heavy formvar / heavy poly)
};
constexpr WireSize WIRE_SIZES[] = {{42, 71, 76}, {43, 63, 69}, {44, 56, 61}};
constexpr uint8_t DEFAULT_AWG = 42;
constexpr uint32_t MAX_WIRE_UM = 76; // largest in the table

// Bobbin (all in um, set in 0.1 mm steps)
constexpr uint32_t MIN_WIDTH_UM = 2000, MAX_WIDTH_UM = 48000; // inner width, flange to flange
constexpr uint32_t MAX_MARGIN_UM      = 5000; // kept clear inside each flange
constexpr uint32_t DEFAULT_WIDTH_UM   = 12000;
constexpr uint32_t DEFAULT_MARGIN_UM  = 1000;
constexpr uint32_t DEFAULT_FLANGE_UM  = 5000; // inner face of the near flange, from home

// TeensyStep4 counts position in int32_t
static_assert((uint64_t)MAX_TURNS * STEPS_PER_REV <= INT32_MAX, "MAX_TURNS * STEPS_PER_REV must fit in int32");
// TeensyStep4's TMR timer with 8 us pulses tops out around 118 kHz; keep well below
static_assert((uint64_t)MAX_RPM * STEPS_PER_REV / 60 <= 100000, "step rate too high for TeensyStep4");
// The traverse may move at most one step per spindle step (it does far less:
// 0.07 for 42 AWG), and its fastest move must fit one step per tick
static_assert(MAX_WIRE_UM * TRAVERSE_STEPS_PER_MM / 1000 < STEPS_PER_REV, "wire pitch too coarse");
static_assert((uint64_t)TRAVERSE_MOVE_UM_S * TRAVERSE_STEPS_PER_MM / 1000 < TRAVERSE_TICK_HZ, "traverse moves too fast");

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------
constexpr uint32_t PROGRESS_INTERVAL_MS = 250;
constexpr uint32_t FAULT_POLL_MS        = 100;
