# Architecture — MoneyLaundring

## Goal

A two-axis rack-and-pinion gantry controller on ESP32. Each axis drives
its motor in one direction until the IR sensor at that end reports the
black-body endpoint, then reverses, with adjustable speed. Both axes run
independently and without blocking.

## Design decision: separate pure logic from hardware

The reversing/bounce logic lives in a pure `Axis` class that never calls
Arduino functions, includes `Arduino.h`, or includes `config.h` (verified
by grep — see reports/module-2-report.md). It takes the current time and
the two debounced sensor states as inputs and returns a motor command
(`AxisOutput`: run/dir/state/fault/reversed). The hardware wrappers
(`Motor`, `LimitSensor`) do the actual `digitalRead` / PWM; `GantryController`
is the only class that bridges the two.

Why: it makes the reversing logic unit-testable on a normal computer with
native `pio test` — no ESP32 needed to prove the state machine is correct.
Putting `digitalRead` calls directly inside the decision logic can only be
tested by flashing and eyeballing. This split is the difference between "a
sketch" and a maintainable, testable repo.

Alternative considered: single-file sketch with everything inline in
`loop()`. Simpler to write, but untestable and hard to extend safely once
bounce logic and a watchdog are added. Rejected.

## Design decision: BringupConsole is kept and extended, not replaced

Module 2 did not add a second console or a build flag to switch between
"bring-up mode" and "run mode." `BringupConsole` from Module 1 is the one
and only console, extended with `run`/`halt`/`sx`/`sy`/`st`. The
bring-up commands (`scan`, `pol`, `speed`, `xf`/`xr`/`yf`/`yr`) all refuse
with a clear message while the gantry is running, and the run-starting
command (`run`) refuses while a scan or jog is in progress, so the two
modes never interleave. This was a deliberate simplification: an operator
bringing up new hardware and an operator running the finished gantry use
the same serial session, the same baud rate, and the same `arm` concept,
with no separate firmware image to flash and no compile-time toggle to
remember.

## Design decision: GantryController is the only hardware glue for Axis

`GantryController` owns two `Axis` instances plus references to the two
`Motor` and four `LimitSensor` objects. It is the only class that:
- builds each `Axis`'s `AxisConfig` from `config.h`'s `REVERSE_LOCKOUT_MS`
  / `REVERSE_PAUSE_MS` / `MAX_TRAVEL_MS`,
- feeds each `Axis::update()` the current debounced `LimitSensor::active()`
  states,
- maps the resulting `AxisDir` to `Motor::FORWARD`/`REVERSE` using the
  `X_FORWARD_IS_TOWARD_MAX` / `Y_FORWARD_IS_TOWARD_MAX` constants,
- calls `Motor::drive()`/`stop()` only when the commanded run state,
  direction, or duty actually changed since the previous tick (not every
  loop), and
- on either axis faulting, immediately stops both motors, halts both
  axes, latches a fault flag, and prints which axis/fault/likely cause.

`GantryController` has no idea `BringupConsole` exists; `BringupConsole`
holds a reference to one `GantryController` and drives it (`startRun`,
`haltRun`, `setSpeedX`/`Y`, `printStatus`, polling `takeFaultFlag()` once
per loop). This keeps the pure/hardware split intact one level up: `Axis`
doesn't know about `Motor`, and `GantryController` doesn't know about
`BringupConsole`.

## What's built

**`Motor`** (hardware) — one L298N channel.
- `begin(enaPin, in1Pin, in2Pin, pwmChannel, freqHz, resBits)`
- `drive(Direction dir, uint8_t duty)` — sets direction pins, then writes
  PWM duty, clamped to `MAX_DUTY` with a printed notice if the requested
  duty was higher. This clamp is unconditional; there is no code path that
  skips it. `drive()` is called from exactly two places in the whole
  codebase: `BringupConsole::cmdJog()` (gated by armed + not-running) and
  `GantryController::applyAxisCommand()` (only reached while running and
  only for a non-faulted axis).
- `stop()` — both direction pins LOW, duty 0.
- Uses arduino-esp32 **2.0.x** LEDC API only: `ledcSetup`, `ledcAttachPin`,
  `ledcWrite(channel, duty)`. Never the 3.x `ledcAttach(pin, freq, res)`.

**`LimitSensor`** (hardware) — one IR endpoint sensor.
- `begin(pin, activeLevel)` — `pinMode(pin, INPUT)` (no internal pull-up;
  the IR module drives OUT actively).
- `update(nowMs)` — non-blocking debounce step, called exactly once per
  `loop()` from `main.cpp` (not from `BringupConsole` — moved there in
  Module 2 so no sensor is ever updated twice in one tick).
- `raw()` — instantaneous `digitalRead`, no debounce.
- `active()` — debounced reading against the current active level.
- `setActiveLevel(int)` — flip polarity at runtime (used by the console's
  `pol` command, refused while running).

**`Axis`** (pure logic, no Arduino, no config.h) — one axis's bounce
decision engine. See `src/Axis.h`/`Axis.cpp` for the exact rule order:
not-RUNNING short-circuit, BOTH_LIMITS_ACTIVE / OPPOSITE_LIMIT_HIT checks
(suppressed for `lockoutMs + reversePauseMs` after a reversal, never
suppressed right after `start()`), then **STUCK_AT_LIMIT** (Module 3,
never suppressed — see below), then TRAVEL_TIMEOUT, the reversal trigger
(gated by `lockoutMs` since the last reversal), then the post-reversal
pause. All time arithmetic uses unsigned subtraction so it survives
`millis()` rollover. Every rule is proven by a native Unity test that has
actually been run and that a four-mutation sanity check (Module 3)
confirmed can fail — see `test/test_axis/test_axis.cpp` and
`reports/module-3-report.md`.

**STUCK_AT_LIMIT (Module 3)** closes a gap `OPPOSITE_LIMIT_HIT` cannot
cover: if the carriage is already resting at an end when `start()` is
called, and that axis's motor wiring is inverted relative to its FORWARD
mapping constant, the "drive away from this end" command actually keeps
driving into it. The limit behind the heading was already active before
any update could observe a rising edge, so it never rises and
`OPPOSITE_LIMIT_HIT` can never fire — without a second check, the axis
would just keep "running" into its end stop until the much longer
`maxTravelMs` eventually caught it. `AxisConfig.clearMs` bounds how long
the behind limit may stay continuously active while the axis is otherwise
allowed to run (not held in the post-reversal pause): a clock accumulates
elapsed time while that condition holds, resets to zero the instant the
behind limit goes inactive, and also resets at every reversal. Once the
clock reaches `clearMs`, the axis faults `STUCK_AT_LIMIT`. This check runs
immediately after the `OPPOSITE_LIMIT_HIT` check and before
`TRAVEL_TIMEOUT`, and — unlike `BOTH_LIMITS_ACTIVE`/`OPPOSITE_LIMIT_HIT`
— is never subject to the post-reversal suppression window, since
suppressing it would defeat the point (the stuck condition is exactly the
kind of thing that can start right at a reversal).

**`GantryController`** (hardware glue) — see the design decision above.
Public interface: `begin(...)`, `startRun(nowMs)`, `haltRun()`,
`isRunning()`, `setSpeedX`/`setSpeedY` (both clamped to `MAX_DUTY`),
`printStatus()`, `takeFaultFlag()` (latched, polled and cleared by the
console), `update(nowMs)`.

**`BringupConsole`** (hardware) — the Module 1 deliverable, extended in
Module 2. Non-blocking serial command parser at `SERIAL_BAUD`. Bring-up
commands: `?`, `map`, `mon`, `pol`, `scan`, `arm`, `disarm`, `stop`,
`speed <n>`, `xf`/`xr`/`yf`/`yr`. Run-mode commands (Module 2): `run`,
`halt`, `sx <0-255>`, `sy <0-255>`, `st`. Boots disarmed and not running;
every motor-moving command checks the armed flag first; `run` additionally
refuses unless armed and idle (no scan/jog in progress); the bring-up
commands refuse while running. Arming auto-expires after `ARM_TIMEOUT_MS`
idle, *except* while running (the safety stops in run mode are `halt`,
`stop`, `disarm`, and the fault shutdown, not an idle timer). `disarm`,
`stop`, and the fault shutdown (via `takeFaultFlag()`) each also end any
run. `scan` only runs while disarmed and not running, and never touches
the six configured motor-driver pins or ever sets a pin to `OUTPUT`.

**`config.h`** — every pin and tunable as a named constant: the X/Y motor
pins, the four sensor pins, `PWM_FREQ`/`PWM_RES`/PWM channels,
`SERIAL_BAUD`, `DEBOUNCE_MS`, `SENSOR_ACTIVE_LEVEL` (provisional),
`JOG_MS`, `MONITOR_PERIOD_MS`, `ARM_TIMEOUT_MS`, `SCAN_WINDOW_MS`,
`SCAN_BASELINE_MS`, `SUPPLY_VOLTS`, `MOTOR_RATED_VOLTS`, the derived
`MAX_DUTY`, `DEFAULT_SPEED`, and (Module 2) `X_FORWARD_IS_TOWARD_MAX` /
`Y_FORWARD_IS_TOWARD_MAX` (provisional), `REVERSE_LOCKOUT_MS`,
`REVERSE_PAUSE_MS`, `MAX_TRAVEL_MS` (provisional), `RUN_SPEED_X`,
`RUN_SPEED_Y`, and (Module 3) `MAX_CLEAR_MS` (provisional).

## Repo layout

```
MoneyLaundring/
  platformio.ini          ; esp32dev (arduino) + native (Axis unit tests)
  include/config.h        ; pin map + tunables (all unconfirmed defaults)
  src/
    main.cpp              ; wire objects together; setup()/loop()
    Motor.h  Motor.cpp
    LimitSensor.h  LimitSensor.cpp
    Axis.h  Axis.cpp              ; pure, no Arduino, no config.h
    GantryController.h  GantryController.cpp  ; hardware glue for Axis
    BringupConsole.h  BringupConsole.cpp
  test/test_axis/test_axis.cpp    ; native Unity tests for Axis
  docs/                    ; this file + wiring.md
  reports/                 ; module-N-report.md (gitignored, not committed)
```

## Fixed loop() ordering

`main.cpp`'s `loop()` always does, in this order, exactly once per tick:
1. All four `LimitSensor::update(now)` calls.
2. `GantryController::update(now)` (reads the now-current debounced sensor
   states; no-op if not running).
3. `BringupConsole::update(now)` (reads serial input, advances its own
   state machines, polls the gantry's fault flag).

This ordering means no sensor is ever updated twice in one tick, and the
controller always acts on this tick's debounced reading, not a stale one.

## Build order (one module at a time)

- **Module 1 — bring-up.** Scaffolded the PlatformIO project, `config.h`,
  `Motor`, `LimitSensor`, and `BringupConsole`. Let the operator confirm
  the pin map, motor direction, and sensor polarity on the real board. No
  bounce logic, no `Axis` class, no native tests.
- **Module 2 — axis bounce logic, native tests, run mode.** Added `Axis`
  (pure bounce decision logic, natively unit-tested) and
  `GantryController` (hardware glue), extended `BringupConsole` with
  `run`/`halt`/`sx`/`sy`/`st`, re-added `[env:native]` for `pio test`, and
  fixed the `loop()` ordering so sensors update exactly once per tick.
- **Module 3 (this module) — verification and hardening.** The `Axis`
  bounce logic had never actually been run by any test before this
  module — ran the existing 11 native tests for the first time (all
  passed; no fix was needed). Added `STUCK_AT_LIMIT` to close the safety
  gap where a carriage resting at an end with inverted motor wiring could
  otherwise push into its end stop for up to `maxTravelMs`. Extended the
  native test suite with threshold boundary tests, a stuck-at-limit test,
  inverted-mapping simulated-carriage runs, and a deterministic
  xorshift32 fuzz test. Confirmed, via a four-mutation sanity check on
  `Axis.cpp` (each mutation restored afterward), that the test suite can
  actually catch a broken rule rather than passing regardless of what the
  code does. Also audited and fixed `GantryController`'s per-motor
  command cache so a jog can never leave it holding a stale value.
- **Module 4 — planned, not built.** Hardware calibration values (the real
  pin map, sensor polarity, and `X_FORWARD_IS_TOWARD_MAX`/
  `Y_FORWARD_IS_TOWARD_MAX` once observed on the physical board), optional
  NVS persistence of run speeds across reboots, an `AUTO_RUN_ON_BOOT` flag
  (defaulting off — nothing auto-starts without this module explicitly
  adding and enabling it), the final README, and a manual hardware test
  checklist.

Each module ends with a completion report in `reports/`. The next module
should be written only after reading the previous module's report.
