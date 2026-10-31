# MoneyLaundring

ESP32 controller for a two-axis rack-and-pinion gantry ("moving dartboard").
Each axis drives its motor until an IR sensor at that end detects the
black-body endpoint, then reverses, with adjustable speed per axis. See
[`docs/architecture.md`](docs/architecture.md) for the full build order and
design decisions.

## Hardware

38-pin ESP32 DevKit (micro-USB) · red L298N dual H-bridge, 12V barrel-jack
fed · 2× TT gear motors on perpendicular rack-and-pinion axes · 4× 3-pin IR
obstacle modules (VCC/GND/OUT, LM393 trim-pot), 2 per axis. Full pin map and
hookup instructions in [`docs/wiring.md`](docs/wiring.md).

> **The pin map, sensor polarity, and FORWARD-direction mapping are all
> unconfirmed defaults**, not verified against the real board. Use the
> `scan`, `mon`, `pol`, and `xf`/`xr`/`yf`/`yr` console commands to check
> them before ever typing `run`. See the calibration step in
> [`docs/wiring.md`](docs/wiring.md).

## How it works

Two independent axes. The reversing decision lives in a pure, hardware-
independent `Axis` class (unit-tested natively, no ESP32 required — see
`test/test_axis/`): each axis drives toward one end, and once the sensor
there goes active, reverses after a short pause, with a lockout so sensor
chatter can't cause a second reversal too soon. `GantryController` is the
only class that connects `Axis` to the real `Motor`/`LimitSensor` hardware.
Design details in [`docs/architecture.md`](docs/architecture.md).

## Build (PlatformIO)

```
pio run -e esp32dev          # compile for the ESP32
pio run -e esp32dev -t upload # flash
pio device monitor            # serial console @ 115200
pio test -e native             # run the native Axis unit tests (no board needed)
```

## Console commands (@ 115200, newline-terminated)

| Command       | Effect                                                         |
|---------------|------------------------------------------------------------------|
| `?`           | print the command list                                          |
| `map`         | print the pin map and tunables                                  |
| `mon`         | toggle the continuous sensor status line (on by default)        |
| `pol`         | flip the active sensor level at runtime (not while running)     |
| `scan`        | (disarmed, not running) watch candidate GPIOs for level changes |
| `arm`         | enable motor output; auto-disarms if idle too long (not while running) |
| `disarm`      | disable motor output (also ends any run)                        |
| `stop`        | stop both motors immediately and disarm (also ends any run)     |
| `speed <n>`   | set jog duty 0-255, clamped to `MAX_DUTY` (not while running)   |
| `xf` / `xr`   | jog axis X forward/reverse for `JOG_MS` (armed, not while running) |
| `yf` / `yr`   | jog axis Y forward/reverse for `JOG_MS` (armed, not while running) |
| `run`         | start autonomous bounce on both axes (armed, idle only)          |
| `halt`        | stop autonomous bounce, stay armed                               |
| `sx <0-255>`  | set axis X run speed, clamped to `MAX_DUTY` (allowed while running) |
| `sy <0-255>`  | set axis Y run speed, clamped to `MAX_DUTY` (allowed while running) |
| `st`          | print gantry status: state, heading, speed, reversals, fault, per axis |

If either axis faults (unexpected endpoint pattern, no endpoint ever seen,
the carriage never leaving the end it started at, or wiring that implies
the motor is wired backwards), the controller stops both motors
immediately, the console auto-disarms, and it prints which axis, which
fault, and a likely cause.

## Status

Built in modules, each ending with a completion report in `reports/`
(gitignored, not committed). See `docs/architecture.md` for what's built,
what's planned, and why the design is split the way it is.

- **Module 1 — bring-up.** `config.h`, `Motor`, `LimitSensor`,
  `BringupConsole`. No bounce logic.
- **Module 2 — axis bounce logic, native tests, run mode.** `Axis` (pure
  bounce decision logic, native-tested) and `GantryController` (hardware
  glue), extended `BringupConsole` with `run`/`halt`/`sx`/`sy`/`st`.
- **Module 3 (current) — verification and hardening.** Confirmed the
  native test suite actually runs and every `Axis` rule is proven by a
  test that can fail (boundary tests, a stuck-at-limit test, inverted-
  mapping simulated-carriage runs, a deterministic fuzz test, and a
  four-mutation sanity check). Closed a safety gap by adding the
  `STUCK_AT_LIMIT` fault (catches a carriage resting at an end with
  inverted motor wiring, which `OPPOSITE_LIMIT_HIT` cannot). Removed a
  hardware-glue risk by auditing and fixing `GantryController`'s
  per-motor command cache so a jog can never leave a stale cache entry
  behind for the next `run`.
- **Module 4 — planned.** Hardware calibration values once observed on the
  real board, optional NVS persistence of run speeds, an
  `AUTO_RUN_ON_BOOT` flag defaulting off, the final README, and a hardware
  test checklist.

Nothing in this repo has been verified against real hardware — there was no
access to the physical board while writing it. Treat the pin map,
`SENSOR_ACTIVE_LEVEL`, and `X_FORWARD_IS_TOWARD_MAX`/`Y_FORWARD_IS_TOWARD_MAX`
as unconfirmed until checked with `scan`/`mon`/`pol`/`xf`/`yf`.
