# Touch UI plan

A standalone touch front panel for the winder, on the 2.8" ILI9341 + HR2046 touch screen. **Touch only: no hardware buttons.** USB serial stays available alongside it.

Status: **UI-1 to UI-4 done** (display + touch + calibration, shared `app` layer with EEPROM settings, Idle / Setup / Keypad, Running / Paused / Done / Fault with pause/resume). Next: UI-5 (Driver screen). Overall roadmap: [TBD.md](TBD.md). Current firmware: [README.md](README.md).

## 1. Goals and constraints

- **Touch only**, landscape **320 × 240**, resistive touch (finger or fingernail, works with wire in hand).
- **Big targets:** at least 60 × 48 px; STOP is far larger than anything else.
- **One brain:** the UI and the serial console drive the same app API, so they can never disagree. Either can be used at any time.
- **Settings are locked while winding**, the same rule as the serial console today.
- **The UI must never affect the count.** Steps come from timer interrupts (TeensyStep4, TMR4, priority 16). The UI only runs in `loop()`, beside the 100 ms driver fault poll.

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

> Check this against the real wiring during bring-up (UI-1). The test screen prints raw touch values, which shows quickly whether touch and display are both working.

**Libraries:**
- `ILI9341_t3n` (KurtE, pinned in `platformio.ini`): display driver with a **frame buffer and DMA**, plus its own Arial / Arial Bold fonts in 8–96 pt. The bundled `ILI9341_t3` is excluded with `lib_ignore`, because its font files have the same names.
- `XPT2046_Touchscreen` (bundled): works with the HR2046 (an XPT2046 clone).
- `EEPROM` (bundled): settings and touch calibration.

**SPI sharing:** the display runs at 30 MHz and touch at 2 MHz on the same bus. **Touch is not sampled while a frame is being sent by DMA**: `touch::begin()` takes a "bus busy" check. The UART to the TMC2209 is a separate peripheral and is unaffected.

## 3. Safety without hardware buttons

The decision is no physical buttons. These are the mitigations:

1. **STOP is always on screen** during a run, and it's the biggest target (the bottom half of the screen).
2. **Tap STOP once** = ramped stop (the count stays exact). **Tap again while it ramps down** = immediate stop.
3. **Touch is checked on every loop pass**, even in the middle of a redraw. Redraws are split into chunks of 5 ms or less, so a tap is never missed.
4. **A frozen UI can't over-wind.** TeensyStep4 stops on the exact target step by itself, without the main loop.
5. **Serial `stop` / `estop` keep working** in parallel.
6. **The 24 V power switch is the emergency off.** Keep it within reach of where you sit.
7. **Recommended hardware tweak:** a **10 kΩ pull-up from each driver's EN pin to 3.3 V**. While the Teensy resets, boots or is being flashed, its pins float. The pull-up keeps both drivers disabled until the firmware enables them.

## 4. Architecture

```
src/
  app.h / app.cpp          settings + actions, shared by serial and UI
  main.cpp                 setup/loop, serial console (calls app)
  ui/
    ui.h / ui.cpp          screen manager: current screen, switching, loop hook
    touch.h / touch.cpp    calibration, debounce, tap / long-press events
    widgets.h / .cpp       Button, ValueField, ProgressBar, Keypad, StatusBar
    theme.h                colours, fonts, sizes
    screens/               one class per screen
      idle.cpp  setup.cpp  keypad.cpp  running.cpp  paused.cpp
      done.cpp  fault.cpp  driver.cpp  calibrate.cpp
```

### `app`: the shared layer (refactor out of `main.cpp`)
- It holds the **settings struct**: turns, rpm, accel, direction, and later wire Ø, bobbin width and start offset. Settings are **saved in EEPROM** with a version number and checksum. If the data is invalid, it falls back to the defaults in `config.h`.
- **Actions:** `setTurns`, `setRpm`, `setAccel`, `setDir`, `start`, `stop`, `resume`, `finish`, `reinitDriver`, `release`.
- Each action returns **`nullptr` on success or a short error text**, e.g. `"rpm must be 10..1500"` or `"accel 327..423 not supported"`. Serial prints that text and the UI shows it.
- The serial console's behaviour doesn't change.

### Rendering (decided in UI-3: frame buffer + DMA)
- **All drawing goes into a RAM frame buffer** (320×240×2 = 150 KB, allocated from RAM2 at boot). This takes microseconds, and nothing half-drawn is ever visible, so there's **no flicker**.
- **`ui::tft()` marks the frame dirty.** The screen manager sends a dirty frame to the display by **async DMA**, at most **10 frames/s**.
  - A full transfer takes about 41 ms at 30 MHz SPI, so 10 fps leaves the bus free for touch about 60% of the time.
  - A screen change is just a RAM clear plus redraw (about 1 ms).
- **Still redraw only what changed:** it keeps frames rare, so the bus stays free for touch.
- **Fixed field boxes**: numbers are right-aligned in a box of fixed width, so the layout never shifts.
- Loop budget: **5 ms or less per pass** for UI work, measured on the touch test screen.
- **Measured:**
  - UI-1, drawing directly to the display: 12.3 ms, then 2.3 ms after redrawing only the changed boxes.
  - UI-3, frame buffer: **0.4 ms**.
- Why the frame buffer: at 30 MHz the display fills only about 1.9 Mpx/s, so 5 ms is about 9,000 pixels. A 60 pt turn counter alone is about 10,000 pixels, so direct drawing would have flickered and broken the budget.

### Touch handling
- `T_IRQ` gives a quick "touched?" check; the coordinates are only read while touched.
- A pressure threshold and 2 matching samples in a row filter out resistive-panel noise.
- Events: **tap** (finger lifted inside the same button), and **long-press** (held 1 s, used for the calibration entry).
- Raw readings are mapped to screen pixels with the **stored calibration** (see Calibration below).

## 5. Screens

Mockups are 320 × 240 landscape. `[ ... ]` is a button. Every screen has a **status bar** at the top: title, driver state (● ready / ○ not ready / ✖ fault) and an over-temperature warning.

### Idle (home)
```
+--------------------------------------------------+
| IDLE                         ● driver ready      |
+--------------------------------------------------+
|  Turns      8000                                 |
|  RPM        1500   (actual 1489.7)               |
|  Accel      600 RPM/s       Dir  CW              |
|                                                  |
|  +--------------------------------------------+  |
|  |                 S T A R T                  |  |
|  +--------------------------------------------+  |
|  [   SETUP   ]                  [  DRIVER   ]    |
+--------------------------------------------------+
```

### Setup
Tap a value to edit it with the keypad. Direction toggles on tap.
```
+--------------------------------------------------+
| SETUP                        ● driver ready      |
+--------------------------------------------------+
|  [ Turns            8000 ]                       |
|  [ RPM              1500 ]                       |
|  [ Accel (RPM/s)     600 ]                       |
|  [ Direction          CW ]                       |
|                                                  |
|  [   BACK   ]                                    |
+--------------------------------------------------+
```

### Keypad
Shows the allowed range. On OK, the value goes through `app` validation; an error is shown in red and the keypad stays open.
```
+--------------------------------------------------+
| RPM   (10 .. 1500)                               |
|   [            1500 ]                            |
|   [ 7 ] [ 8 ] [ 9 ]   [  ⌫  ]                    |
|   [ 4 ] [ 5 ] [ 6 ]   [  .  ]                    |
|   [ 1 ] [ 2 ] [ 3 ]   [CANCEL]                   |
|   [     0     ]       [  OK  ]                   |
+--------------------------------------------------+
```
The `.` key is enabled only for decimal fields (wire Ø, width and offset, later).

### Running
```
+--------------------------------------------------+
| WINDING                      ● 1489.7 rpm        |
+--------------------------------------------------+
|            4,512  / 8,000                        |
|  [##########################..................]  |
|  elapsed 3:02        ETA 2:21                    |
|  +--------------------------------------------+  |
|  |                                            |  |
|  |                  S T O P                   |  |
|  |                                            |  |
|  +--------------------------------------------+  |
+--------------------------------------------------+
```
- The counter is in **Arial 60 Bold**, the largest that fits "10,999 / 11,000"; it can drop to 48 pt if needed.
- After the first tap, STOP turns orange and reads **"STOPPING — TAP AGAIN = STOP NOW"**.

### Paused (after STOP)
```
+--------------------------------------------------+
| PAUSED                       ● driver ready      |
+--------------------------------------------------+
|            4,537  / 8,000                        |
|  [##########################..................]  |
|                                                  |
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
| DONE                         ● driver ready      |
+--------------------------------------------------+
|   ✔ OK          8,000 turns                      |
|   steps 25,600,000 / 25,600,000                  |
|   driver pulse check  OK                         |
|   time 5:24                                      |
|  [      OK       ]           [    SETUP    ]     |
+--------------------------------------------------+
```
**OK** returns to Idle, where START begins the next run. It doesn't restart straight away, because the bobbin usually needs changing first, and a spindle that starts from a single tap on this screen would be easy to set off by accident.

### Fault
A red screen with the fault name (e.g. "driver over-temperature" or "UART not responding"), **"count unreliable"**, and the turns done when it happened. Buttons: **[RE-INIT DRIVER]** and **[OK]** (back to Idle). The driver is already disabled at this point, as in step 1.

### Driver
Live TMC2209 status, the same as the serial `drv` command:
- version, spreadCycle, microsteps, current, `cs_actual`;
- fault flags, GSTAT, MSCNT.

Buttons: **[RE-INIT]** and **[BACK]**.

### Touch calibration
- **When it runs:** automatically on first boot (no valid data in EEPROM), or if the screen is **held for 3 s during boot**.
- **How:** tap 3 crosshairs. That's enough to compute a full mapping, including rotation or mirroring.
- The result is saved to EEPROM, and a quick "tap anywhere" test screen confirms it.

### Later
- **After the traverse (TBD Phase B):** a **Bobbin** screen (wire Ø, width, start offset), a **Jog/Home** screen (±0.1 / 1 / 10 mm buttons, HOME, live position) and **layer n / N** on the Running screen.
- **Presets:** saved recipes (e.g. "Strat neck – 42AWG – 8000T"), possibly on the SD card.

## 6. Firmware change needed: pause / resume

Today `stop` ends the run. For RESUME:
- A ramped stop ends the TeensyStep4 move as now. The run stays open, with `done` steps recorded.
- **Resume** starts a new move of `target − done` steps in the same direction, with the same ramps.
- The run keeps a **total** across all segments. The end-of-run step check and the **driver pulse check** (MSCNT read at the very start and very end) cover the whole run.
- The change is contained in `src/winder.*`. Serial gets `resume` and `finish` commands too.

## 7. Implementation phases

| Phase | Content | Testable result |
|---|---|---|
| **UI-1** | Display + touch bring-up, test screen (touch dot + raw values), calibration | Taps land under your finger everywhere on the screen |
| **UI-2** | `app` refactor, EEPROM settings | Serial console works exactly as before; settings survive a power cycle |
| **UI-3** | Idle, Setup, Keypad screens | Settings can be set and validated from the screen |
| **UI-4** | Running, Paused, Done, Fault + pause/resume | Full wind from the screen; STOP / RESUME / FINISH work |
| **UI-5** | Driver screen | Same data as serial `drv` |
| **UI-6** | Traverse screens | After TBD Phase B |
| **UI-7** | Presets | Optional, SD card |

## 8. Verification

- **UI-1:** taps land correctly near all four corners and the centre after calibration, and calibration survives a power cycle.
- **Timing:** loop time at most 5 ms per pass while the Running screen updates (debug stat). The 100 ms fault poll stays on time.
- **Exact count with the UI active:** 1000 turns at 1500 RPM, with the screen updating and touches during the run → `OK`, pulse check `OK`.
- **STOP response:** the ramp-down starts on the first tap. A second tap stops on the next step. RESUME finishes with the exact total.
- **Serial still works** at the same time: `status` during a UI-started run, and `stop` from serial.
- **Fault path:** unplug the 24 V during a run → the Fault screen appears and the driver is disabled. **RE-INIT** recovers after power returns.

## 9. Open questions

- [x] Screen orientation: landscape, **pin header on the left** (`DISPLAY_ROTATION` in `config.h`; touch calibration handles the touch axes)
- [x] Pause behaviour: **STOP pauses the run** → Paused screen with RESUME / FINISH
- [x] Wiring: matches the table above
- [x] Flicker-free numbers: decided in UI-3 — `ILI9341_t3n` frame buffer + async DMA (see §4 Rendering)
