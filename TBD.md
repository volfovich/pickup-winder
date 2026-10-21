# Roadmap

| Step | Status | Details |
|---|---|---|
| Spindle with exact turn counting | **done**, tested on hardware | [README.md](README.md) |
| Touch front panel UI-1 … UI-5 | **done**, tested on hardware | [UI.md](UI.md) |
| Wire traverse | firmware written; **hardware not wired yet** | [TRAVERSE.md](TRAVERSE.md) |
| UI-6: traverse screens (Bobbin, Guide, layer n / N) | written; full test after the traverse bring-up | [UI.md](UI.md) |
| UI-7: presets | optional, SD card | [UI.md](UI.md) |

## Later / ideas

- Scatter winding (vary pitch or turnaround points per layer, for tone)
- Presets / recipes on the SD card
- Once-per-turn spindle index sensor (hall or optical) for real-time mechanical skip detection
- Patch TeensyStep4's timer to a finer prescaler for closer-to-setpoint RPM (e.g. ~1199.8 instead of 1204.0)
- Temperature readout: the TMC2209 only flags 120 °C and above, so a real reading needs NTC thermistors (driver heatsink, motor) on free analog pins 17 / 18; show them on Running / Driver, pause the run at a set limit. Avoid DS18B20 (1-wire blocks interrupts ~60 µs per bit, jittering the spindle steps)
- A 40 mm fan on the spindle driver's heatsink (see [README.md](README.md#heat))
- Non-blocking TMC2209 UART reads: each read blocks the loop ~5 ms today (TMCStepper waits 2 ms before and after the reply)

## Decided

- Home: the motor end = 0 mm
- Endstop: the 3-wire mechanical module (on hand), powered from 3.3 V
- Touch: HR2046 (XPT2046-compatible), T_IRQ / T_OUT / T_DIN / T_CS / T_CLK pins
- Traverse motor current: 0.6 A/phase → run at 400 mA RMS, spreadCycle
- Screen: 2.8" ILI9341 + HR2046 touch, touch only, no hardware buttons
- Spindle when not winding: low hold current while paused or just finished, released after 2 min idle
