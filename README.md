# MoneyLaundring

ESP32 controller for a two-axis rack-and-pinion gantry ("moving dartboard").
Each axis will eventually drive its motor until an IR sensor at that end
detects the black-body endpoint, then reverse, with adjustable speed. This
repo is currently at **Module 1: hardware bring-up** — there is no
reversing/bounce logic yet. See [`docs/architecture.md`](docs/architecture.md)
for the full build order.

## Hardware

38-pin ESP32 DevKit (micro-USB) · red L298N dual H-bridge, 12V barrel-jack
fed · 2× TT gear motors on perpendicular rack-and-pinion axes · 4× 3-pin IR
obstacle modules (VCC/GND/OUT, LM393 trim-pot), 2 per axis. Full pin map and
hookup instructions in [`docs/wiring.md`](docs/wiring.md).

> **The pin map is an unconfirmed default**, not verified against the real
> board. Use the `scan`, `mon`, and `pol` console commands below to check it
> before arming.

## Module 1: bring-up console

This module has no bounce/reversing logic at all. What it has instead is a
safe, non-blocking serial console so the operator can find and verify the
four sensor GPIOs, the sensor polarity, and each motor's direction on the
real board before any control logic exists. Motors never move until the
operator explicitly types `arm`, and arming auto-expires after an idle
timeout. Every commanded motor duty is clamped to `MAX_DUTY` (see
`include/config.h`); there is no way to bypass that clamp.

## Build (PlatformIO)

```
pio run                 # compile for the ESP32
pio run -t upload       # flash
pio device monitor      # serial console @ 115200
```

## Console commands (@ 115200, newline-terminated)

| Command       | Effect                                                         |
|---------------|------------------------------------------------------------------|
| `?`           | print the command list                                          |
| `map`         | print the pin map and tunables                                  |
| `mon`         | toggle the continuous sensor status line (on by default)        |
| `pol`         | flip the active sensor level at runtime, print the new value    |
| `scan`        | (disarmed only) watch candidate GPIOs for level changes          |
| `arm`         | enable motor output; auto-disarms if idle too long               |
| `disarm`      | disable motor output                                             |
| `stop`        | stop both motors immediately and disarm                          |
| `speed <n>`   | set jog duty 0-255, clamped to `MAX_DUTY`                        |
| `xf` / `xr`   | jog axis X forward/reverse for `JOG_MS` (armed only)              |
| `yf` / `yr`   | jog axis Y forward/reverse for `JOG_MS` (armed only)              |

## Status

Built in modules, each ending with a completion report in `reports/`
(gitignored, not committed). See `docs/architecture.md` for what's built,
what's planned, and why the design is split the way it is.

- **Module 1 (this module) — bring-up.** Scaffolding, `config.h`, `Motor`,
  `LimitSensor`, and `BringupConsole`. No bounce logic yet.
- **Module 2 — planned.** `Axis` bounce logic (pure, hardware-independent,
  native `pio test` coverage) and a per-direction maximum-travel-time
  watchdog.
- **Module 3 — planned.** Speed console refinements, final README, hardware
  test checklist.

Nothing in this repo has been verified against real hardware — there was no
access to the physical board while writing it. Treat the pin map and
`SENSOR_ACTIVE_LEVEL` as unconfirmed until checked with `scan`/`mon`/`pol`.
