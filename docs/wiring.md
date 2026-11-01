# Wiring & Pin Map — MoneyLaundring

This file is the **hardware contract**. The firmware's `include/config.h`
mirrors the pin map and tunables below. If the board is rewired, update
this file first, then `config.h`.

## Parts (as seen in the build photos)

| Role            | Part                                                        |
|-----------------|--------------------------------------------------------------|
| Controller      | 38-pin ESP32 DevKit, micro-USB, BOOT/RST buttons, G-prefixed silkscreen |
| Motor driver    | L298N dual H-bridge (red board), fed from a 12V barrel-jack adapter |
| Motors          | 2× yellow TT gear motors, each on its own 3D-printed rack-and-pinion axis (axes perpendicular) |
| Endpoint sensors| 4× 3-pin IR obstacle modules (VCC/GND/OUT, LM393 trim-pot), 2 per axis |

The gantry is two independent linear axes driven by rack-and-pinion. Each
axis has one motor and two endpoint sensors (one at each end of travel).
"Axis X" = one motor + its 2 limits; "Axis Y" = the other motor + its 2
limits.

## ⚠️ Pin map status: UNCONFIRMED DEFAULTS

The exact ESP32 GPIO each wire lands on could **not** be read from the
build photos. The table below lives only as named constants in
`include/config.h` and is a *starting point*, not a verified fact. Before
arming the bring-up console on the real board:

- Use the `scan` console command to watch for GPIO level changes while
  touching one sensor at a time, and confirm each sensor's actual pin.
- Use `mon` to watch all four sensors' raw/active state continuously.
- Use `pol` to flip the active level at runtime if `mon` shows the wrong
  polarity, and copy the printed value into `SENSOR_ACTIVE_LEVEL` in
  `config.h` once confirmed.
- Only type `arm` once you've checked motor direction and sensor wiring
  against this table (or corrected the table to match the real board).

Motor driver outputs are on normal output-capable GPIOs. Sensor inputs are
on GPIO 34/35/36/39, which are **input-only** (fine for inputs) and have
**no internal pull-up** — that's fine here because the IR modules drive
their OUT pin actively, not through a pull-up.

### Motor driver (L298N)

| Signal | ESP32 GPIO | L298N pin | Notes                        |
|--------|-----------|-----------|------------------------------|
| X ENA  | 25        | ENA       | PWM speed, axis X            |
| X IN1  | 26        | IN1       | direction, axis X            |
| X IN2  | 27        | IN2       | direction, axis X            |
| Y ENB  | 33        | ENB       | PWM speed, axis Y            |
| Y IN3  | 32        | IN3       | direction, axis Y            |
| Y IN4  | 13        | IN4       | direction, axis Y            |

> **Remove the ENA/ENB jumper caps** if they're still on the L298N board.
> Speed control needs ENA/ENB wired to the GPIOs above, not tied to 5V —
> otherwise the motors always run at full speed regardless of PWM duty.

### IR endpoint sensors (digital OUT)

| Signal       | ESP32 GPIO | Meaning                      |
|--------------|-----------|-------------------------------|
| X_MIN sensor | 34        | axis X, one end                |
| X_MAX sensor | 35        | axis X, other end              |
| Y_MIN sensor | 36        | axis Y, one end                |
| Y_MAX sensor | 39        | axis Y, other end              |

## Power & ground

- 12V barrel jack → L298N `12V IN` and `GND`.
- **During bring-up, power the ESP32 from USB (micro-USB), not from the
  L298N's 5V output.** Do not back-feed the ESP32 from the L298N 5V rail
  while USB is also connected — pick one supply path at a time.
- **Common ground is mandatory:** L298N `GND` ↔ ESP32 `GND`. Without this,
  sensor and direction-pin readings will be unreliable or wrong.
- **IR modules must be powered from the ESP32 3V3 pin, not 5V.** These
  modules' OUT pin tracks their VCC rail; running them from 5V would put up
  to 5V on an ESP32 GPIO, which is outside the 3.3V-tolerant input range.
  Powering from 3V3 keeps OUT at or below 3.3V always. (An earlier draft of
  this file incorrectly said to use 5V — that was wrong and is corrected
  here.)

### Standalone power (no USB, Module 4)

Once bring-up and calibration (see `docs/hardware-test.md`) are complete
and the gantry has run cleanly under supervision, it can run unattended
with no USB connection at all:

- Fit the L298N's **5V-enable jumper** (the one that routes its onboard
  5V regulator's output to the board-edge 5V pin), so the L298N supplies
  5V from the same 12V barrel-jack input instead of needing USB.
- Wire that L298N 5V output to the ESP32's **5V/VIN** pin — not 3V3; the
  ESP32 has its own onboard 3.3V regulator fed from 5V/VIN, and feeding
  5V/VIN directly into the 3V3 pin bypasses that regulator and can damage
  the board.
- **Disconnect USB entirely before fitting the 5V-enable jumper / wiring
  this up**, and keep it disconnected while running standalone. Per the
  power rule above, the ESP32 must only ever be powered from exactly one
  source (USB, or L298N 5V) — never both.
- `AUTO_RUN_ON_BOOT` (default `false` in `config.h`) is meant for exactly
  this mode: with no USB connected, there is no serial console to type
  `arm`/`run` into, so an operator who wants the gantry to start on its
  own sets it to `true`. Be clear about what "fires" means here: on the
  classic ESP32, `esp_reset_reason()` cannot tell an EN-pin/reset-button
  reset apart from a true power cycle - both report `ESP_RST_POWERON` -
  so with the flag true, any hardware reset (not only unplugging and
  replugging 12V) starts the countdown. It reuses the normal `arm`/`run`
  code paths and the countdown is cancelable by any serial byte - see
  `docs/hardware-test.md`'s standalone-mode section and the README's
  safety rules for the full contract.

## Final constants (as of Module 4)

Every pin and tunable lives in `include/config.h` as a named constant;
nothing in `src/` hardcodes one directly. The table below is a snapshot —
`config.h` is always the source of truth, and most of these are still
**provisional defaults** pending the hardware-test checklist
(`docs/hardware-test.md`), not confirmed values.

| Constant | Default | Status |
|----------|---------|--------|
| `X_ENA`/`X_IN1`/`X_IN2`, `Y_ENB`/`Y_IN3`/`Y_IN4` | 25/26/27, 33/32/13 | provisional (motor driver pins) |
| `X_MIN`/`X_MAX`/`Y_MIN`/`Y_MAX` | 34/35/36/39 | provisional (sensor pins — confirm with `scan`) |
| `SENSOR_ACTIVE_LEVEL` | `HIGH` | provisional (confirm with `mon`/`pol`) |
| `X_FORWARD_IS_TOWARD_MAX` / `Y_FORWARD_IS_TOWARD_MAX` | `true` / `true` | provisional (confirm with `xf`/`yf`) |
| `MAX_DUTY` | 153 (derived: `255 * 6.0 / (12.0 - 2.0)`) | fixed by the 12V/6V/2V-drop formula, not a guess |
| `DEFAULT_SPEED` | 110 | arbitrary safe starting jog duty |
| `REVERSE_LOCKOUT_MS` | 400 | reasonable default, not hardware-tuned |
| `REVERSE_PAUSE_MS` | 150 | reasonable default, not hardware-tuned |
| `MAX_TRAVEL_MS` | 20000 | provisional — measure from `st`'s `longestSegmentMs` at the lowest duty you will run, after at least 4 reversals per axis (see `docs/hardware-test.md` steps 7 and 9) |
| `MAX_CLEAR_MS` | 3000 | provisional — measure from `st`'s `longestClearMs` at the lowest duty you will run, after at least 4 reversals per axis (see `docs/hardware-test.md` steps 7 and 9) |
| `RUN_SPEED_X`/`RUN_SPEED_Y` | `DEFAULT_SPEED` | adjustable at runtime via `sx`/`sy` |
| `RUN_TELEMETRY` | `true` | observation-only; see README |
| `AUTO_RUN_ON_BOOT` | `false` | only ever enable after full calibration, for standalone mode |
| `AUTO_RUN_DELAY_MS` | 10000 | countdown before an enabled auto-run fires |

## Why MAX_DUTY exists

The L298N is fed from the 12V barrel-jack supply, but the TT motors are
rated for roughly 3-6V. Running them at full PWM duty against a 12V rail
would overdrive and likely damage them (or at minimum run them far outside
their rated speed/torque curve). `config.h` computes

```
MAX_DUTY = (uint8_t)(255.0 * MOTOR_RATED_VOLTS / (SUPPLY_VOLTS - 2.0) + 0.5)
```

with `SUPPLY_VOLTS = 12.0` and `MOTOR_RATED_VOLTS = 6.0`, which evaluates to
**153**. The `(SUPPLY_VOLTS - 2.0)` term accounts for the L298N's internal
transistor voltage drop (roughly 2V) regardless of PWM duty. Every duty
value the firmware ever writes to a motor's PWM channel is clamped to
`MAX_DUTY` inside `Motor::drive()`, with a printed notice if a higher value
was requested. There is no console command or code path that bypasses this
clamp.

## Bring-up procedure

1. Power the ESP32 from USB only (L298N 12V can be connected or not; just
   don't also back-feed 5V into the ESP32). Open the serial monitor at
   115200 baud.
2. Read the boot banner. It reprints the full pin map and tunables and
   warns that the pin map is unconfirmed.
3. Run `scan`: cover/uncover each IR sensor one at a time and watch which
   GPIO number the console reports as changing. Confirm it matches the
   `X_MIN`/`X_MAX`/`Y_MIN`/`Y_MAX` pins above (or note the real pin if it
   differs, and update this table and `config.h`).
4. Run `mon` (it is already on by default) and check that covering each
   sensor with a black-body marker makes its raw level flip, and that its
   `active()` interpretation shows `*` for the reading you expect to mean
   "endpoint reached". If the polarity is backwards, run `pol` to flip it
   at runtime, then copy the printed value into `SENSOR_ACTIVE_LEVEL` in
   `config.h` so it survives a reboot.
5. Only once pins and polarity look right: run `arm`, then `xf` (or `xr`,
   `yf`, `yr`) one at a time and visually confirm that axis moves the
   intended way for the intended duration (`JOG_MS`). Run `stop` any time
   something looks wrong — it stops both motors immediately and disarms.
6. If idle for `ARM_TIMEOUT_MS`, the console auto-disarms on its own.

## Calibration step — direction mapping (do this before ever using `run`)

Module 2 adds autonomous bouncing (`run`), which depends on knowing which
end of each axis FORWARD actually drives toward. That mapping lives in
`X_FORWARD_IS_TOWARD_MAX` / `Y_FORWARD_IS_TOWARD_MAX` in `config.h`, and
both are **provisional** (`true`) until confirmed:

1. With `mon` running (default on) and the gantry `arm`ed, type `xf` and
   watch which sensor's `*` marker lights up as the carriage approaches
   it. If `X_MAX` activates, `X_FORWARD_IS_TOWARD_MAX = true` is correct;
   if `X_MIN` activates instead, it needs to be `false`. Repeat with `yf`
   for `Y_FORWARD_IS_TOWARD_MAX`.
2. Edit `include/config.h` to match what you observed, then reflash.
3. Do not type `run` until this step is done and the pin map and sensor
   polarity (`scan`/`mon`/`pol`, above) are also confirmed on the real
   board. If the mapping is wrong, an autonomous run will fault with
   `OPPOSITE_LIMIT_HIT` on the affected axis — the gantry stops itself and
   the console disarms, but it is still safer to calibrate first than to
   rely on that fault catching it every time.

