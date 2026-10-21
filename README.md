# Pickup Winder NG

Guitar pickup coil winder firmware for a Teensy 4.0. A NEMA 17 spindle, driven by a TMC2209, stops on the **exact** preset turn count: up to 11,000 turns at up to 1500 RPM. It is run from a **2.8" touch screen** (no hardware buttons) or the USB serial console. A **wire guide** on a small linear slide lays the wire in even layers.

Firmware **v1.0** (`FIRMWARE_NAME` / `FIRMWARE_VERSION` in `include/config.h`).

## Status

| Part | State |
|---|---|
| Spindle with exact turn counting | **Done, tested on hardware.** 1000- and 3000-turn runs at 1500 RPM with exact counts and a clean driver pulse check. |
| Touch UI (UI-1 … UI-6) | **Done.** UI-1 … UI-5 tested on hardware; the UI-6 guide screens can only be fully tested once the guide is wired. See [UI.md](UI.md). |
| Pause / resume | **Done:** STOP pauses with the count exact; RESUME winds the rest. |
| Heat | Hold current 25 %, spindle released 2 min after a run, run current adjustable (`current N`). |
| Wire guide (traverse) | **Firmware written and host-tested; hardware not wired yet.** Off by default (`traverse off`), so the machine winds exactly as before. See [TRAVERSE.md](TRAVERSE.md). |

Roadmap and ideas: [TBD.md](TBD.md).

## Hardware

| Part | Notes |
|---|---|
| Teensy 4.0 | 600 MHz, PlatformIO `teensy40` |
| Spindle: NEMA 17 + TMC2209 | 1.8°, rated 2.8 A/phase; driver R_SENSE 0.11 Ω (BTT/FYSETC-style module), heatsink |
| Display | 2.8" ILI9341 SPI TFT with HR2046 (XPT2046-compatible) resistive touch |
| Wire guide *(not wired yet)* | 28T0601-50 slide (NEMA 11, 1 mm lead, 50 mm), second TMC2209 at UART address 1, 3-wire endstop at the motor end. Details: [TRAVERSE.md](TRAVERSE.md) |
| 24 V supply | motor power (VM) for both drivers; its switch is the emergency off |

### Wiring

Spindle driver:

| Teensy pin | TMC2209 | Notes |
|---|---|---|
| 1 (TX1) | PDN_UART | through a **1 kΩ** resistor |
| 0 (RX1) | PDN_UART | direct (single-wire UART, shared with the guide's driver) |
| 2 | STEP | |
| 3 | DIR | |
| 4 | EN | active LOW; **10 kΩ pull-up to 3.3 V recommended** (see below) |
| GND | MS1, MS2 | UART address 0 |
| 5 *(optional)* | — | jumper to pin 2 for the STEP loopback self-test |

Display and touch (full table in the module's pin order: [UI.md](UI.md#2-hardware-and-wiring)): SCK 13, MOSI 11, MISO 12 shared by display and touch; TFT CS 10, DC 9, RESET 8; touch T_CS 14, T_IRQ 15; VCC and LED to 3.3 V.

Wire guide (when fitted): STEP 6, DIR 7, EN 22, endstop SIG 21, step by step in [TRAVERSE.md](TRAVERSE.md#wiring-step-by-step).

> The TMC2209s only answer on UART when the **24 V motor power is on**. If it was off at boot, power it up and type `init` (or RE-INIT on the Driver screen).

> **EN pull-up:** while the Teensy is unpowered, booting or resetting for an upload, the EN pins float. Many TMC2209 modules pull EN low on the board, so the driver switches on, at the current set by the module's VREF potentiometer. A 10 kΩ resistor from EN to 3.3 V keeps it off until the firmware takes over; check with a meter that EN reads ~3.3 V with the Teensy unplugged (use 4.7 kΩ if the module's own pull-down is strong).

## Using it

1. **Power up:** a splash screen, then the home screen. On first boot the touch screen is calibrated (tap 3 crosses + 1 check); hold the screen for 3 s at boot to recalibrate.
2. **Home screen:** turns, direction, RPM and the bobbin to wind. **START**, **SETUP**, **GUIDE**.
3. **Setup:** turns, RPM, accel, direction (keypad with range and validation). **BOBBIN** for the guide settings, **DIAGNOSTICS** for driver status and the touch test.
4. **Winding:** a big turn counter, progress, speed, time left (layer n / N with the guide) and a big **STOP**. One tap = ramped stop; a second tap while it slows = stop on the next step. Either way the run **pauses** with the count exact.
5. **Paused:** **RESUME** winds the remaining turns; **FINISH** (tap twice) ends the run early.
6. **Done:** COMPLETE / ENDED EARLY / COUNT MISMATCH, turns, driver pulse check(s), winding time.
7. With the **guide**: home it and teach the bobbin's flanges once on the Guide screen (see [TRAVERSE.md](TRAVERSE.md#how-the-bobbin-is-located)), set AWG and margin on the Bobbin screen, switch the guide on. START then moves the guide to the start position first.

Settings are saved in EEPROM and restored at power-up. They are locked while a run is in progress (running or paused). The serial console works alongside the screen; whatever starts, pauses or ends a run, the screen follows.

## Driver configuration

Set over UART at boot and **verified by reading every value back**: the motor will not start on a driver that failed verification.

| Setting | Spindle | Guide |
|---|---|---|
| UART address | 0 | 1 |
| Mode | **spreadCycle** | spreadCycle (`TRAVERSE_STEALTHCHOP` to change) |
| Microsteps | 16, with 256× interpolation | 16 (3200 steps/mm) |
| Run current | 1500 mA RMS default; `current N` changes it (saved) | 400 mA RMS |
| Hold current | 25 %; switched off 2 min after a run ends (paused runs keep holding) | 50 % |
| Chopper | toff 4, blank time 24, hstrt 5, hend 0 | same |
| Step input | rising edge only (`dedge = 0`) | same |

The status bar shows whether the spindle is **"ready - motor free"** or **"ready - holding"**. A free stepper still feels notchy by hand: that's its magnets, not current.

## How it works

- **Step generation:** [TeensyStep4](https://github.com/luni64/TeensyStep4) (pinned to a reviewed commit) makes the spindle's STEP pulses from a hardware timer (QuadTimer TMR4, NVIC priority 16), so USB, UART, display or the guide can't disturb stepping.
- **Exact count:** one turn = 200 × 16 = **3200 steps**; 11,000 turns = 35.2 M steps (the int32 position counter holds ~671,000 turns). A run moves exactly `turns × 3200` steps; after a pause, RESUME moves exactly the rest.
- **Ramps:** a trapezoidal profile planned in step counts. It ramps down so the slowest step is **exactly** the last one: no inertia overshoot at full speed. Short runs become a triangle and still end on the exact step.
- **Speed resolution:** the step timer ticks at 0.213 µs, so speeds are quantised: 1200 RPM runs at **1204.0**, 1500 RPM at **1489.7**. The screen shows the actual speed. This doesn't affect the count: the count is steps, not speed × time.
- **Wire guide:** its position is a fixed function of the spindle's step count, computed in a 40 kHz timer interrupt below the spindle's priority. It follows the spindle's ramps, pauses and stops by itself. Details: [TRAVERSE.md](TRAVERSE.md#how-it-works).
- **Display:** drawn into a RAM frame buffer and sent by DMA, at most 10 frames/s; the main loop stays at ~0.4 ms per pass. Details: [UI.md](UI.md).

## Safety and verification

- **End-of-run check:** steps taken vs. target → `OK` / `MISMATCH`.
- **Driver pulse check:** each driver's own microstep counter (`MSCNT`) is read before and after the run and must match the pulses sent. This catches pulses lost or added on the STEP wire. With the guide, both drivers are checked.
- **Fault watch** (every 100 ms while winding, the two drivers in turn): over-temperature, short to ground / supply, UART lost (3 failed reads in a row), driver reset, charge-pump undervoltage, driver error. A fault aborts the run as **"count unreliable"**, disables the driver and shows the Fault screen. START and RESUME also check the driver first.
- **Over-temperature pre-warning** (~120 °C chip): the status bar shows **"winding - HOT"** and the Done screen says so.
- **Touch-only safety:** STOP is always on screen while the motor turns, and reacts on touch-down. A frozen UI still can't over-wind: the step timer stops on the exact target by itself. The 24 V switch is the emergency off.
- **Optional loopback:** uncomment `STEP_LOOPBACK_PIN` in `include/config.h` and jumper pin 2 → 5 to count the real STEP pulses independently.

## Limits and defaults

| Setting | Range | Default |
|---|---|---|
| Turns | 1 – 11000 | 10 |
| RPM | 10 – 1500 | 300 |
| Accel (RPM/s) | 10 – 2000, **except 327 – 423** ¹ | 300 |
| Spindle current (mA RMS) | 400 – 1700 | 1500 |
| Wire | AWG 42 / 43 / 44, single or heavy insulation | 42 single (0.071 mm) |
| Bobbin inner width (mm) | 2.0 – 48.0 | 12.0 |
| Margin inside each flange (mm) | 0.0 – 5.0 | 1.0 |

¹ In that range TeensyStep4 computes an invalid timer period for the final step; these values are rejected.

## Build and use

Requires [PlatformIO](https://platformio.org/). Libraries (fetched automatically): TMCStepper, TeensyStep4 and ILI9341_t3n (both pinned); XPT2046_Touchscreen and EEPROM come with Teensyduino.

```sh
pio run                 # build
pio run -t upload       # build and flash the Teensy
pio device monitor      # serial console, 115200, local echo on
```

### Serial commands

| Command | When | Description |
|---|---|---|
| `turns N` | idle | number of turns |
| `rpm N` | idle | max speed (shows the actual timer speed) |
| `accel N` | idle | ramp up / down rate in RPM/s |
| `dir cw\|ccw` | idle | winding direction |
| `current N` | idle | spindle run current, mA RMS |
| `start` | idle | start winding (with the guide: goes to the start position first) |
| `stop` | winding | ramped stop → paused, count exact |
| `estop` | winding | stop at the next step → paused, count exact |
| `resume` | paused | wind the remaining turns |
| `finish` | paused | end the run early |
| `release` | idle | disable both drivers (motors free) |
| `init` | idle | re-run the TMC2209 configuration |
| `traverse on\|off` | idle | use the wire guide for winding |
| `home` | idle | find the home switch (motor end = 0 mm) |
| `jog MM` / `goto MM` | idle | move the guide, e.g. `jog -0.5`, `goto 12` (goto needs homing) |
| `awg 42\|43\|44` / `insulation single\|heavy` | idle | wire size and insulation (sets the pitch) |
| `flange1 MM\|here` / `flange2 MM\|here` | idle | taught carriage positions of the bobbin's flanges |
| `width MM` / `margin MM` | idle | inner width (moves flange 2), margin inside both flanges |
| `layer away\|toward` | idle | direction of the first layer |
| `endstop` | any time | home switch state (to check its polarity) |
| `status` | any time | settings, drivers, guide and progress |
| `drv` | any time | both TMC2209s: registers and status |
| `calibrate` | any time | touch screen calibration |
| `help` | any time | command list |

While winding, progress prints every 250 ms (turns, and RPM measured over the last second); a summary prints at the end.

## Heat

Driver and motor heat grow with the **square** of the current: 1000 mA makes about 45 % of the heat of 1500 mA. A pickup winder's spindle has very little load, so it rarely needs the full 1500 mA. Find what yours needs:

1. With a real bobbin, at the RPM and accel you actually use, run 200 turns with the pointer test below at `current 1000`, then `800`, then `600`.
2. Note the lowest current that never slips, and set **about 1.5× that** (not below what works at your highest RPM).
3. Do a long run and compare: driver heatsink and motor should end clearly cooler.

Also:
- **Heatsink:** many TMC2209 modules cool through a thermal pad on the **bottom** of the PCB; check where your module's maker wants the heatsink, and leave air around it.
- **Motor:** mounting it on a metal plate spreads its heat a lot.
- **Fan:** a 40 mm fan on the driver is the next step if it still runs hot.

## Testing for skipped steps

The firmware counts every step it sends and checks the driver received them, but an open-loop stepper can still **slip mechanically**. On a 1.8° motor a slip always loses a whole electrical cycle = **7.2°** of spindle rotation, so it's visible:

1. **Pointer test:** fix a thin pointer next to the spindle, mark the spindle exactly at it, run, and check the mark ends exactly at the pointer.
2. **Margin test:** at your target RPM, raise `accel` on short runs (or lower `current`) until the mark is off, then run at about half of that.
3. **Load test:** repeat with a real bobbin, wire and your normal tension.
4. **Listen:** a slip sounds like a sharp clack or growl instead of a smooth whine.

## Project layout

```
platformio.ini         board, libraries (TMCStepper, TeensyStep4, ILI9341_t3n pinned), monitor settings
include/config.h       pins, drivers, mechanics, wire table, limits and defaults
src/main.cpp           serial console, progress output, boot sequence
src/app.*              settings (EEPROM), actions, fault watch, run flow; shared by serial and UI
src/tmc_driver.*       both TMC2209s (spindle 0, guide 1): setup, verification, status, faults
src/winder.*           exact-count winding on TeensyStep4, pause / resume, pulse check, summary
src/traverse.*         wire guide: 40 kHz step ISR, homing, jog, follows the spindle
src/traverse_math.h    guide position as a function of the spindle step count (host-testable)
src/ui/                display + touch: frame buffer, calibration, widgets, screen manager
src/ui/screens/        splash, idle, setup, keypad, bobbin, guide, running, paused, done,
                       fault, diagnostics, driver, touch test, calibrate
```

All tunables (pins, currents, microsteps, limits, defaults, wire sizes) are in `include/config.h`.

## Known limitations

- **Open loop:** a mechanical slip can't be detected automatically (StallGuard doesn't work in spreadCycle). A once-per-turn spindle sensor would add this (see [TBD.md](TBD.md)).
- **Wire guide not yet tested on hardware:** homing, jogging and following are verified by host simulation and serial checks only.
- **No temperature readout below 120 °C:** the TMC2209 only reports threshold flags; NTC sensors are on the [TBD.md](TBD.md) list.
- **UART reads block the loop ~5 ms each** (TMCStepper waits before and after the reply), so a STOP tap can wait up to ~5–10 ms during a run. Stepping is unaffected.
- **TeensyStep4 is experimental** (v0.0.3); it is pinned to a reviewed commit, and its edge cases are worked around in `src/winder.cpp`.
- **Speed is fixed per run:** RPM and turns are set before START and can't change mid-run (by design).
