# Architecture — MoneyLaundring

## Goal

Two-axis gantry controller on ESP32. Each axis drives its motor in one
direction until the IR sensor at that end reports the black-body endpoint, then
reverses. Speed is adjustable per axis over the serial console. Both axes run
independently and without blocking.

## Design decision: separate pure logic from hardware

The one decision worth stating up front: the **bounce logic lives in a pure
module (`Axis`) that never calls Arduino functions**. It takes the current time
and the two sensor states as inputs and returns a motor command. The hardware
wrappers (`Motor`, `LimitSensor`) do the actual `digitalRead` / PWM.

Why: it makes the reversing logic unit-testable on a normal computer with
`pio test` — no ESP32 needed to prove the state machine is correct. A flat
`.ino` with `digitalRead` inside the logic can only be tested by flashing and
eyeballing. This split is the difference between "a sketch" and "a repo."

Alternative considered: single-file sketch (everything in `loop()`). Simpler to
write, but untestable and hard to extend. Rejected for a project that wants to
be maintainable.

## Modules and contracts

**`Motor`** (hardware) — one L298N channel.
- `begin(enaPin, in1Pin, in2Pin, pwmChannel, freqHz, resBits)`
- `drive(Direction dir, uint8_t duty)` — set direction pins + PWM duty 0–255
- `stop()` — both direction pins LOW, duty 0
- Uses arduino-esp32 **2.0.x** LEDC API: `ledcSetup`, `ledcAttachPin`,
  `ledcWrite(channel, duty)`.

**`LimitSensor`** (hardware) — one IR endpoint sensor.
- `begin(pin, activeLevel)` — `pinMode(pin, INPUT)`
- `bool active()` — debounced read; true when the pin equals `activeLevel`
- Debounce: reading must be stable for `DEBOUNCE_MS` before it flips.

**`Axis`** (pure logic, no Arduino) — one motor + its two limits.
- State: `Direction dir`, `unsigned long lockoutUntilMs`
- `Command update(unsigned long nowMs, bool minActive, bool maxActive, bool enabled)`
  - returns `{ Direction dir, bool run }`
- Rules: if `!enabled` → stop. If `nowMs < lockoutUntilMs` → keep current dir,
  run. Else if heading toward MAX and `maxActive` → flip to REVERSE + set
  lockout. Else if heading toward MIN and `minActive` → flip to FORWARD + set
  lockout. The lockout (`REVERSE_LOCKOUT_MS`) stops re-triggering on the same
  marker before the rack physically leaves it.

**`Console`** (hardware) — serial command parser @ 115200.
- `x<0-255>` set axis X speed · `y<0-255>` set axis Y speed · `s<0-255>` both
- `p` pause (stop both) · `r` resume · `?` print status

**`config.h`** — pin map (from `docs/wiring.md`) + tunables:
`DEFAULT_SPEED`, `REVERSE_LOCKOUT_MS`, `SENSOR_ACTIVE_LEVEL`, `DEBOUNCE_MS`,
`PWM_FREQ`, `PWM_RES`.

## Repo layout

```
MoneyLaundring/
  platformio.ini         ; esp32dev (arduino) + native test env
  include/config.h       ; pin map + tunables
  src/
    main.cpp             ; wire objects together; setup()/loop()
    Motor.h  Motor.cpp
    LimitSensor.h  LimitSensor.cpp
    Axis.h  Axis.cpp     ; pure, no Arduino
    Console.h  Console.cpp
  test/test_axis/        ; native unit tests for Axis
  docs/                  ; this file + wiring.md + module prompts
  reports/               ; module-N-report.md (gitignored)
```

## Build order (one module at a time)

- **Module 1 — bring-up.** Scaffold the project, `config.h`, `Motor`,
  `LimitSensor`, and a *diagnostic* `main` that jogs each axis both ways and
  continuously prints all four sensor readings. Purpose: confirm the pin map,
  motor direction, and sensor polarity on the real board. No bounce logic yet.
- **Module 2 — bounce logic + tests.** Implement `Axis` as pure logic with
  native `pio test` coverage, then replace the diagnostic loop with real
  bounce behaviour (keep diagnostic behind a build flag).
- **Module 3 — console + finish.** `Console` speed control, pause/resume,
  status, final README, manual hardware test checklist.

Each module ends with a completion report in `reports/`. The next module is
written only after reading the previous report.
