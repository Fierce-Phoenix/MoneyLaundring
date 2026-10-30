# Architecture — MoneyLaundring

## Goal

A two-axis rack-and-pinion gantry controller on ESP32. Each axis will
eventually drive its motor in one direction until the IR sensor at that
end reports the black-body endpoint, then reverse, with adjustable speed.
Both axes run independently and without blocking. **Module 1 implements
none of that reversing behaviour** — it is hardware bring-up only.

## Design decision: separate pure logic from hardware

The reversing/bounce logic (Module 2 and later) will live in a pure
`Axis` class that never calls Arduino functions. It will take the current
time and the two sensor states as inputs and return a motor command. The
hardware wrappers (`Motor`, `LimitSensor`) will continue to do the actual
`digitalRead` / PWM; `Axis` will just decide what they should do.

Why: it makes the reversing logic unit-testable on a normal computer with
native `pio test` — no ESP32 needed to prove the state machine is correct.
Putting `digitalRead` calls directly inside the decision logic can only be
tested by flashing and eyeballing. This split is the difference between "a
sketch" and a maintainable, testable repo.

Alternative considered: single-file sketch with everything inline in
`loop()`. Simpler to write, but untestable and hard to extend safely once
bounce logic and a watchdog are added. Rejected.

This decision is recorded now, ahead of Module 2, so Module 2 can be built
directly against it without re-deciding the shape of the code.

## What Module 1 actually builds

Module 1 has no `Axis` class yet — there is nothing to reverse. Instead it
builds the hardware-facing pieces plus a console that lets a human safely
exercise them:

**`Motor`** (hardware) — one L298N channel.
- `begin(enaPin, in1Pin, in2Pin, pwmChannel, freqHz, resBits)`
- `drive(Direction dir, uint8_t duty)` — sets direction pins, then writes
  PWM duty, clamped to `MAX_DUTY` with a printed notice if the requested
  duty was higher. This clamp is unconditional; there is no code path that
  skips it.
- `stop()` — both direction pins LOW, duty 0.
- Uses arduino-esp32 **2.0.x** LEDC API only: `ledcSetup`, `ledcAttachPin`,
  `ledcWrite(channel, duty)`. Never the 3.x `ledcAttach(pin, freq, res)`.

**`LimitSensor`** (hardware) — one IR endpoint sensor.
- `begin(pin, activeLevel)` — `pinMode(pin, INPUT)` (no internal pull-up;
  the IR module drives OUT actively).
- `update(nowMs)` — non-blocking debounce step, called every `loop()`.
- `raw()` — instantaneous `digitalRead`, no debounce.
- `active()` — debounced reading against the current active level.
- `setActiveLevel(int)` — flip polarity at runtime (used by the console's
  `pol` command).

**`BringupConsole`** (hardware) — the actual Module 1 deliverable. A
non-blocking serial command parser at `SERIAL_BAUD`. Implements `?`, `map`,
`mon`, `pol`, `scan`, `arm`, `disarm`, `stop`, `speed <n>`, and `xf`/`xr`/
`yf`/`yr`. Boots disarmed; every motor-moving command checks the armed flag
first and refuses otherwise; arming auto-expires after `ARM_TIMEOUT_MS`
idle. `scan` only runs while disarmed and never touches the six configured
motor-driver pins or ever sets a pin to `OUTPUT`.

**`config.h`** — every pin and tunable as a named constant: the X/Y motor
pins, the four sensor pins, `PWM_FREQ`/`PWM_RES`/PWM channels,
`SERIAL_BAUD`, `DEBOUNCE_MS`, `SENSOR_ACTIVE_LEVEL` (provisional),
`JOG_MS`, `MONITOR_PERIOD_MS`, `ARM_TIMEOUT_MS`, `SCAN_WINDOW_MS`,
`SUPPLY_VOLTS`, `MOTOR_RATED_VOLTS`, the derived `MAX_DUTY`, and
`DEFAULT_SPEED`.

## Repo layout

```
MoneyLaundring/
  platformio.ini          ; esp32dev (arduino)
  include/config.h        ; pin map + tunables (all unconfirmed defaults)
  src/
    main.cpp              ; wire objects together; setup()/loop()
    Motor.h  Motor.cpp
    LimitSensor.h  LimitSensor.cpp
    BringupConsole.h  BringupConsole.cpp
  docs/                    ; this file + wiring.md
  reports/                 ; module-N-report.md (gitignored, not committed)
```

Module 2 will add `src/Axis.h`/`Axis.cpp` (pure logic) and a `[env:native]`
PlatformIO environment with `test/test_axis/` for native unit tests. Those
do not exist yet in Module 1.

## Build order (one module at a time)

- **Module 1 (this module) — bring-up.** Scaffold the PlatformIO project,
  `config.h`, `Motor`, `LimitSensor`, and `BringupConsole`. Let the
  operator confirm the pin map, motor direction, and sensor polarity on the
  real board. No bounce logic, no `Axis` class, no native tests.
- **Module 2 — planned, not built.** Implement `Axis` as pure logic with
  native `pio test` coverage (add `[env:native]` back to
  `platformio.ini`), then wire it into `main.cpp` behind the bring-up
  console (or replacing it) so the gantry actually bounces. Also add a
  per-direction maximum-travel-time watchdog that stops an axis and
  reports a fault if no endpoint sensor triggers within a configured
  travel-time budget.
- **Module 3 — planned, not built.** Speed console refinements, final
  README, and a manual hardware test checklist.

Each module ends with a completion report in `reports/`. The next module
should be written only after reading the previous module's report.
