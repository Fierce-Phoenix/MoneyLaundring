# MoneyLaundring

ESP32 controller for a two-axis rack-and-pinion gantry ("moving dartboard").
Each axis drives its motor until an IR sensor hits the black-body endpoint,
then reverses. Speed is adjustable per axis over the serial console.

## Hardware

ESP32 DevKit (30-pin WROOM) · L298N dual H-bridge · 2× TT gear motors ·
4× IR endpoint sensors (2 per axis) · 12V supply. Full pin map and hookup in
[`docs/wiring.md`](docs/wiring.md).

## How it works

Two independent axes. Each one runs a small non-blocking state machine: drive
in the current direction, watch the sensor at that end, and on detection flip
direction (with a short lockout so it doesn't re-trigger on the same marker).
Design details in [`docs/architecture.md`](docs/architecture.md).

## Build (PlatformIO)

```
pio run                 # compile for the ESP32
pio run -t upload       # flash
pio device monitor      # serial monitor @ 115200
pio test -e native      # run the bounce-logic unit tests (no board needed)
```

## Serial commands (@ 115200)

`x<0-255>` axis X speed · `y<0-255>` axis Y speed · `s<0-255>` both ·
`p` pause · `r` resume · `?` status.

## Status

Built in modules. See `docs/architecture.md` for the build order and
`docs/claude-code/` for the per-module build specs. Current: **Module 1
(bring-up)** — verify wiring, motor direction, and sensor polarity on the real
board before the bounce logic goes in.

> Pin map in `docs/wiring.md` is a recommended default until confirmed against
> the physical board.
