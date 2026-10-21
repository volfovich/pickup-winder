# Touch UI

The winder's front panel: a 2.8" ILI9341 display with HR2046 resistive touch. **Touch only: no hardware buttons.** The USB serial console works alongside it.

**Status: UI-1 to UI-6 done.** UI-1 … UI-5 are tested on hardware. The UI-6 guide screens work, but homing, flange teaching and layer display can only be fully tested once the guide is wired ([TRAVERSE.md](TRAVERSE.md)). Overall roadmap: [TBD.md](TBD.md). Firmware overview: [README.md](README.md).

## 1. Goals and constraints

- **Touch only**, landscape **320 × 240**, resistive touch (finger or fingernail, works with wire in hand).
- **Big targets:** at least ~48 px; STOP is far larger than anything else.
- **One brain:** the UI and the serial console drive the same `app` layer, so they can never disagree. Either can be used at any time.
- **Settings are locked during a run** (running or paused).
- **The UI must never affect the count.** Spindle steps come from a timer interrupt (TeensyStep4, TMR4, priority 16), guide steps from another (PIT, priority 32). The UI only runs in `loop()`.

## 2. Hardware and wiring

| Module pin | Teensy pin | Notes |
|---|---|---|
| T_IRQ | 15 | touch interrupt, low while touched |
| T_OUT | 12 (MISO) | shared SPI |
| T_DIN | 11 (MOSI) | shared SPI |
| T_CS | 14 | touch chip select |
| T_CLK | 13 (SCK) | shared SPI |
| SDO (MISO) | 12 | |
| LED | 3.3 V | backlight, direct (too much current for a pin) |
| SCK | 13 | |
| SDI (MOSI) | 11 | |
| D/C | 9 | |
| RESET | 8 | |
| CS | 10 | display chip select |
| GND | GND | |
| VCC | 3.3 V | |

Orientation: landscape with the module's **pin header on the left** (`DISPLAY_ROTATION` in `config.h`; touch calibration handles the touch axes).

**Libraries:**
- `ILI9341_t3n` (KurtE, pinned in `platformio.ini`): display driver with a **frame buffer and DMA**, plus its own Arial / Arial Bold fonts in 8–96 pt. The bundled `ILI9341_t3` is excluded with `lib_ignore`, because its font files have the same names.
- `XPT2046_Touchscreen` (bundled): works with the HR2046 (an XPT2046 clone).
- `EEPROM` (bundled): settings and touch calibration.

**SPI sharing:** the display runs at 30 MHz and touch at 2 MHz on the same bus. **Touch is not sampled while a frame is being sent by DMA** (`touch::begin()` takes a "bus busy" check). The UART to the TMC2209s is a separate peripheral and is unaffected.

## 3. Safety without hardware buttons

1. **STOP is always on screen while the motor turns**, as the biggest target (the bottom of the screen). Whatever starts a run (screen or serial), the Running screen comes up.
2. **STOP reacts on touch-down.** First tap = ramped stop; a second tap while it slows = stop on the next step. Either way the run **pauses** with the count exact.
3. **Touch is sampled on every loop pass** (~100 Hz) except while a frame is on the bus (≤ 41 ms, at most 10 times a second).
4. **A frozen UI can't over-wind.** TeensyStep4 stops on the exact target step by itself, without the main loop.
5. **Serial `stop` / `estop` keep working** in parallel.
6. **The 24 V power switch is the emergency off.** Keep it within reach.
7. **Recommended:** a **10 kΩ pull-up from each driver's EN pin to 3.3 V**. While the Teensy is unpowered, resets, boots or is being flashed, its pins float; the pull-up keeps the drivers off until the firmware takes over.

## 4. Architecture

```
src/
  app.h / app.cpp          settings (EEPROM), actions, fault watch, run flow: shared by serial and UI
  main.cpp                 setup / loop, serial console (calls app)
  ui/
    ui.h / ui.cpp          frame buffer, screen manager, follows the run state, boot hold, loop stats
    touch.h / touch.cpp    sampling, debounce, tap / long-press events, calibration maths
    affine.h               3-point affine calibration solve
    widgets.h / .cpp       Button + ButtonGroup, status bar, text fitting, messages, turn counter, progress bar
    theme.h                colours, fonts, sizes
    screens/               one class per screen:
      splash  idle  setup  keypad  bobbin  guide  running  paused  done  fault
      diagnostics  driver  touch_test  calibrate
```

### `app`: the shared layer
- **Settings:** turns, RPM, accel, direction, spindle current, and the guide settings (on/off, AWG, insulation, inner width, margin, flanges, first layer). **Saved in EEPROM** with a version number and checksum (version 3; older versions migrate). Invalid data falls back to the defaults in `config.h`.
- **Actions:** setters, `start` / `stop` / `estop` / `resume` / `finish`, `home` / `jog` / `moveTo`, `setFlange1Here` / `setFlange2Here`, `release`, `reinitDriver`.
- Each action returns **`nullptr` on success or a short error text**, e.g. `"rpm must be 10..1500"`. Serial prints it, the UI shows it.

### Screen manager
- **The run state drives the screen:** whatever starts, pauses or ends a run (a tap, serial, a fault), the matching screen comes up: Running, Paused, Done or Fault.
- Each screen has `enter()` (draw everything), `update()` (redraw what changed, every loop pass) and `onTouch()`.

### Rendering (frame buffer + DMA)
- **All drawing goes into a RAM frame buffer** (320×240×2 = 150 KB, allocated from RAM2 at boot). This takes microseconds, and nothing half-drawn is ever visible, so there's **no flicker**.
- **`ui::tft()` marks the frame dirty.** The screen manager sends a dirty frame to the display by **async DMA**, at most **10 frames/s**. A full transfer takes about 41 ms at 30 MHz SPI; a screen change is a RAM clear plus redraw (about 1 ms).
- **Only what changed is redrawn**, which keeps frames rare and the bus free for touch.
- **Fixed field boxes:** numbers are right-aligned in boxes of fixed width, so the layout never shifts.
- **Measured loop time:** 12.3 ms drawing directly (UI-1), 2.3 ms redrawing only changed boxes, **0.4 ms** with the frame buffer (UI-3). Budget: 5 ms per pass.
- Why the frame buffer: at 30 MHz the display fills only about 1.9 Mpx/s, so 5 ms is about 9,000 pixels. A 60 pt turn counter alone is about 10,000 pixels.

### Text fitting
- **Text wrap is off** (`setTextWrap(false)`). The library default wraps a too-wide string to x = 0 one line down, right over the next element. Too-wide text is now clipped at the screen edge, and `printAt` logs `UI: text too wide: "..."` to serial.
- The t3n fonts are bigger than their names suggest. Measured: Arial 12 has a 12 px cap height, 18 px line spacing and ink to +15; Arial 16 Bold has 16 / 23 / +20; the 60 pt comma reaches +74.
- **Shrink to fit keeps the weight:** bold text (buttons, values) steps down 16 → 14 → 13 → 12 → 11 → 10 Bold; regular text 14 → … → 10. Buttons also take a two-line label (`"\n"`).
- **Messages** use one helper: 12 pt on one line, else 11, else 11 on two lines where there is room.
- **Turn counter:** 60 pt up to 9,999 turns, 48 pt for 5-digit targets. The choice follows the target, so it never changes mid-run.
- Every fixed string and the worst-case dynamic ones were measured against the real font data on the host.

### Touch handling
- `T_IRQ` gives a quick "touched?" check; coordinates are only read while touched.
- Sampled at ~100 Hz; a pressure threshold and 2 matching samples in a row filter out resistive-panel noise.
- Events: **down**, **up** and **long-press** (1 s). Buttons highlight on down and act when the finger lifts inside them (sliding off cancels); STOP acts on down.
- Raw readings are mapped to screen pixels with the **stored calibration**.

## 5. Screens

Mockups are 320 × 240 landscape; `[ ... ]` is a button. Every screen has a **status bar**: the title on the left, the machine state on the right:

| State | Text |
|---|---|
| idle, spindle off | **ready - motor free** (green) |
| idle, spindle holding (2 min after a run) | **ready - holding** |
| driver not configured | **driver not ready** (red) |
| winding | **winding** (orange), **winding - HOT** (red) after the driver's over-temperature pre-warning |
| paused | **paused** (orange) |

### Splash (boot)
"Pickup Winder **NG**" with "version 1.0", and a pickup bobbin whose coil winds itself up in 1.5 s.
- The bottom line shows "driver ready" or "driver not ready - check 24 V" once start-up is done.
- It covers the up-to-3 s wait for USB serial, so the screen is never blank at power-up.
- After 2.5 s, or on a tap, it goes to Idle, or to touch calibration on first boot.
- Holding the screen shows "keep holding to recalibrate touch"; at 3 s, calibration starts.

### Idle (home)
```
+--------------------------------------------------+
| IDLE                         ready - motor free  |
+--------------------------------------------------+
|  Turns      8,000               Dir  CW          |
|  RPM        1500   (actual 1489.7)               |
|  Bobbin     42 AWG, 12.0 mm, 22 layers           |
|  +--------------------------------------------+  |
|  |                  START                     |  |
|  +--------------------------------------------+  |
|  [       SETUP       ]   [       GUIDE       ]   |
+--------------------------------------------------+
```
- **Bobbin** shows what will be wound: AWG (" H" for heavy insulation), inner width and layers, or "guide off - by hand".
- If START can't start, the reason shows in red above it for 5 s.
- With the guide on, START first moves the guide to the start position: START turns into **CANCEL** and "moving the guide to the start position..." shows.
- **GUIDE** turns orange as **HOME GUIDE!** while the guide is on but not homed (START would refuse).

### Setup
Tap a value to edit it with the keypad; Direction toggles on tap. Locked (grey) during a run.
```
+--------------------------------------------------+
| SETUP                        ready - motor free  |
+--------------------------------------------------+
|  [ Turns                               8000 ]    |
|  [ RPM                                 1500 ]    |
|  [ Accel (RPM/s)                        600 ]    |
|  [ Direction                             CW ]    |
|                                                  |
|  [ BACK ] [ BOBBIN ] [     DIAGNOSTICS     ]     |
+--------------------------------------------------+
```

### Keypad
The title and allowed range are in the status bar. On OK, the value goes through `app` validation; an error shows in red and the keypad stays open. The first digit replaces the shown value; no leading zeros.
```
+--------------------------------------------------+
| RPM                                  10 .. 1500  |
+--------------------------------------------------+
| [         1500 ]   [ 7 ] [ 8 ] [ 9 ]             |
|  (error text)      [ 4 ] [ 5 ] [ 6 ]             |
| [    CANCEL    ]   [ 1 ] [ 2 ] [ 3 ]             |
| [      OK      ]   [ . ] [ 0 ] [DEL]             |
+--------------------------------------------------+
```
The `.` key is enabled only for lengths in mm (inner width, margin), with one decimal (0.1 mm).

### Running
```
+--------------------------------------------------+
| WINDING                      winding             |
+--------------------------------------------------+
|        4,512  / 8,000                            |
|  [##########################..................]  |
|  1489.7 rpm        3:02          left 2:21       |
|  +--------------------------------------------+  |
|  |                   STOP                     |  |
|  +--------------------------------------------+  |
+--------------------------------------------------+
```
- The info line shows the measured RPM, the elapsed winding time (**layer n / N** instead when the guide is used) and the time left.
- After the first tap, STOP turns orange and reads **"STOPPING... / TAP AGAIN TO STOP NOW"**.

### Paused (after STOP)
```
+--------------------------------------------------+
| PAUSED                       paused              |
+--------------------------------------------------+
|        4,537  / 8,000                            |
|  [##########################..................]  |
|        4537.25 turns done, 3,463 to go           |
|  +---------------------+  +---------------------+|
|  |       RESUME        |  |       FINISH        ||
|  +---------------------+  +---------------------+|
+--------------------------------------------------+
```
- **RESUME** winds the remaining turns. The count stays exact across pauses.
- **FINISH** ends the run and shows the Done screen. It needs a **second tap within 3 s** ("TAP AGAIN"), because it can't be undone.

### Done
```
+--------------------------------------------------+
| DONE                         ready - holding     |
+--------------------------------------------------+
|                   COMPLETE                       |
|   turns  8,000 / 8,000                           |
|   steps  25,600,000                              |
|   driver pulse check  OK                         |
|   winding time  5:24                             |
|  [        OK        ]   [       SETUP       ]    |
+--------------------------------------------------+
```
- The headline is **COMPLETE**, **ENDED EARLY** (FINISH) or **COUNT MISMATCH**.
- With the guide, the steps line is replaced by **spindle pulse check** and **guide pulse check**.
- After an over-temperature pre-warning: "driver got hot - lower the current".
- **OK** returns to Idle, where START begins the next run. It doesn't restart straight away, because the bobbin usually needs changing first, and a spindle that starts from a single tap here would be easy to set off by accident.

### Fault
A red status bar, the fault (e.g. "driver over-temperature", "UART not responding (motor power lost?)", "traverse: short to ground"), **"count unreliable"**, and the turns done when it happened. The faulty driver is already disabled. Buttons: **RE-INIT DRIVER** and **OK** (to Idle).

### Bobbin
The guide settings, from **Setup → BOBBIN** or **Guide → BOBBIN**. Locked during a run.
```
+--------------------------------------------------+
| BOBBIN                       ready - motor free  |
+--------------------------------------------------+
| [ Guide        ON ]   [ First       away ]       |
| [ AWG          42 ]   [ Insul.    single ]       |
| [ Margin      1.0 ]   [ Inner w.    12.0 ]       |
| [ Flanges (rail mm)           5.0 - 17.0 ]       |
|   0.071 mm wire: 140 turns/layer, 22 layers      |
|  [     BACK     ]          [     GUIDE    ]      |
+--------------------------------------------------+
```
- **Guide**, **First** (first layer away from / toward home), **AWG** (42 → 43 → 44) and **Insul.** (single / heavy) change on tap. The wire diameter including insulation comes from `WIRE_SIZES` in `config.h` and is the pitch per turn.
- **Margin** is kept clear inside **both** flanges. **Inner w.** is the inner width, flange to flange (typing it moves flange 2). Both open the keypad in mm with one decimal.
- **Flanges** are the carriage positions on the rail where the wire meets each flange's inner face. They are **taught on the Guide screen**; tapping this row opens it. See [TRAVERSE.md](TRAVERSE.md#how-the-bobbin-is-located).
- The summary line shows the wire, turns per layer and layers for the set turns, or in red why it won't fit (no room between the margins, bobbin past 50 mm, guide too slow for the RPM).

### Guide
Jog, home and teach the bobbin, from **Idle → GUIDE** or **Bobbin → GUIDE**.
```
+--------------------------------------------------+
| GUIDE                        ready - motor free  |
+--------------------------------------------------+
|  Position   12.345 mm                            |
|        homed  -  endstop open                    |
| [-10][ -1][-0.1][+0.1][ +1][+10]                 |
| [   HOME   ] [   SET    ] [   SET    ]           |
|              [ FLANGE 1 ] [ FLANGE 2 ]           |
| [ BOBBIN ] [  GO TO FLANGE 1  ] [ BACK ]         |
+--------------------------------------------------+
```
- **Jog** in mm: minus is toward home (the motor), plus is away. Before homing, at most ±5 mm per jog. The position is orange until homed.
- **HOME** becomes **STOP** while homing or moving.
- **Teaching the bobbin:** jog until the wire meets the inside of flange 1 (the one nearer home), tap **SET FLANGE 1**; jog to flange 2, tap **SET FLANGE 2**. The state line confirms, e.g. "flange 2 set at 17.0 mm - inner width 12.0". Positions round to 0.1 mm.
- **GO TO FLANGE** moves to flange 1, then flange 2 on the next tap (the label shows which), to check that the wire lines up with both.
- SET and GO TO need homing; all motion is refused during a run.
- The state line shows homed / NOT HOMED and the endstop, "guide driver not found - check wiring, re-init" if the driver doesn't answer, "winding - guide follows the spindle" during a run, or homing errors for 5 s.

### Diagnostics
From **Setup → DIAGNOSTICS**, refreshed twice a second:
- firmware name and version;
- loop max (orange above 5 ms);
- spindle: ready / not ready, motor free / holding, run current;
- guide: driver found, homed, position;
- endstop: open / PRESSED;
- uptime.

Buttons: **SPINDLE** and **GUIDE** open the Driver screen for that TMC2209, **TOUCH TEST**, **BACK** (to Setup).

### Driver
Live status of one TMC2209 (opened from Diagnostics), the same data as serial `drv`, refreshed twice a second:
- which driver, version, and whether it is configured;
- mode (red if not as configured), microsteps, chopper settings;
- run / hold current as set, and the current used right now (`cs_actual`);
- motor enabled or free (the driver's EN input), temperature band (`t120` … `t157`, `otpw`, `ot`);
- faults (shorts, driver reset, undervoltage, drv_err) and MSCNT.

Open-load flags are not shown: the datasheet says they are often set falsely at standstill. Each UART read takes about 5 ms, so the screen reads **one register per loop pass**; reads pause while the motor turns (the fault watch owns the UART then). Buttons: **RE-INIT DRIVER** (both drivers) and **BACK** (to Diagnostics).

### Touch test
From Diagnostics: raw and calibrated touch values, loop max, and a dot wherever the screen is touched, with five markers to check the accuracy. Buttons: **CLEAR**, **CALIBRATE**, **BACK**.

### Touch calibration
- **When it runs:** automatically on first boot (no valid data in EEPROM), when the screen is **held for 3 s during boot**, from the touch test, or with serial `calibrate`.
- **How:** tap 3 crosses (enough for a full mapping, including rotation or mirroring), then a 4th check cross, which must land within 12 px. Otherwise it starts again.
- The result is saved to EEPROM.

## 6. Pause / resume

In `src/winder.*`, used by both the screen and serial:
- STOP (ramped) or E-STOP (next step) ends the TeensyStep4 move. The run stays open: **Paused**.
- **RESUME** starts a new move of `target − done` steps in the same direction, with the same ramps, from the same position counter. The driver is checked first.
- **FINISH** ends a paused run early (**ENDED EARLY**).
- The end-of-run step check and the **driver pulse check** (MSCNT at the very start and very end) cover the whole run, pauses included.
- The guide needs nothing extra: its position is a function of the spindle's step count.

## 7. Implementation phases

| Phase | Content | State |
|---|---|---|
| **UI-1** | Display + touch bring-up, touch test screen, calibration | Done, tested |
| **UI-2** | `app` refactor, EEPROM settings | Done, tested |
| **UI-3** | Idle, Setup, Keypad on a frame buffer | Done, tested |
| **UI-4** | Running, Paused, Done, Fault + pause / resume | Done, tested |
| **UI-5** | Driver screen | Done, tested |
| — | Text fitting, splash screen, heat (motor free / holding, HOT warning) | Done, tested |
| **UI-6** | Bobbin, Guide and Diagnostics screens; layer n / N; guide pulse check | Done; guide parts tested once the guide is wired |
| **UI-7** | Presets | Optional, SD card |

## 8. Verification

- **Calibration:** taps land correctly near all four corners and the centre, and calibration survives a power cycle.
- **Timing:** loop max 0.4 ms on the touch test screen. Known exception: the fault poll reads one driver's GSTAT and DRV_STATUS every 100 ms while winding, about 10 ms of blocking UART. Stepping is interrupt-driven, so the count is unaffected, but a STOP tap can wait up to ~10 ms.
- **Exact count with the UI active:** runs at 1500 RPM with the screen updating and touches during the run → `OK`, pulse check `OK`.
- **STOP response:** the ramp-down starts on the first tap; a second tap stops on the next step; RESUME finishes with the exact total.
- **Serial at the same time:** `status` during a screen-started run, and `stop` from serial.
- **Fault path:** 24 V off during a run → the Fault screen, driver disabled; **RE-INIT** recovers after power returns.
- **Guide screens (after wiring):** homing, jog, flange teaching, layer n / N and the guide pulse check; see the checklist in [TRAVERSE.md](TRAVERSE.md#bring-up-checklist-after-wiring).

## 9. Decisions

- Screen orientation: landscape, **pin header on the left**.
- **STOP pauses the run** → Paused screen with RESUME / FINISH.
- Flicker-free numbers: `ILI9341_t3n` frame buffer + async DMA.
- Done → **OK** returns to Idle instead of restarting.
- The bobbin is located by **teaching both flanges** on the Guide screen, not by typing positions.
- Driver details live in **Diagnostics**, not on the home screen.
