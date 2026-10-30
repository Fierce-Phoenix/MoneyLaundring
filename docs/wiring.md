# Wiring & Pin Map — MoneyLaundring

This file is the **hardware contract**. The firmware's `include/config.h` is
generated from the pin map below. If the board is rewired, update this file
first, then `config.h`.

## Components (confirmed from build photos)

| Role            | Part                                             |
|-----------------|--------------------------------------------------|
| Controller      | ESP32 DevKit, 30-pin WROOM-32, micro-USB         |
| Motor driver    | L298N dual H-bridge (red board, heatsink)        |
| Motors          | 2× TT gear motors, on a rack-and-pinion 2-axis gantry |
| Endpoint sensors| 4× IR obstacle modules (VCC/GND/OUT + trim-pot)  |
| Power           | 12V barrel jack → L298N 12V input                |

The gantry is two independent linear axes. Each axis is driven by one motor
and bounded by two IR sensors (one at each end of travel). "Axis X" = one
motor + its 2 limits; "Axis Y" = the other motor + its 2 limits.

## ⚠️ Pin map status: RECOMMENDED DEFAULTS — must be confirmed

The exact ESP32 GPIO for each wire could **not** be traced from the breadboard
photos. The table below is a *safe recommended* mapping (avoids input-only and
boot-strapping-pin traps). Before flashing, either:
 - **(A)** rewire the board to match this table, or
 - **(B)** replace these numbers with the GPIOs actually used on the board.

Motor outputs must be on normal output-capable pins. Sensor inputs are placed
on GPIO 34/35/36/39, which are **input-only** (correct for sensors) and have
**no internal pull-up** — fine, because the IR modules drive their OUT pin
actively.

### Motor driver (L298N)

| Signal | ESP32 GPIO | L298N pin | Notes                        |
|--------|-----------|-----------|------------------------------|
| X ENA  | 25        | ENA       | PWM speed, axis X            |
| X IN1  | 26        | IN1       | direction A                  |
| X IN2  | 27        | IN2       | direction A                  |
| Y ENB  | 33        | ENB       | PWM speed, axis Y            |
| Y IN3  | 32        | IN3       | direction B                  |
| Y IN4  | 13        | IN4       | direction B                  |

> If ENA/ENB still have their jumper caps on, remove them — speed control
> needs ENA/ENB wired to the two GPIOs above, not tied to 5V.

### IR endpoint sensors (digital OUT)

| Signal       | ESP32 GPIO | Meaning                      |
|--------------|-----------|-------------------------------|
| X_MIN sensor | 34        | axis X, "reverse" end         |
| X_MAX sensor | 35        | axis X, "forward" end         |
| Y_MIN sensor | 36        | axis Y, "reverse" end         |
| Y_MAX sensor | 39        | axis Y, "forward" end         |

### Power & ground

- 12V barrel jack → L298N `12V` and `GND`.
- L298N `5V` out → ESP32 `5V`/`VIN` **only if** the L298N 5V-enable jumper is
  on; otherwise power the ESP32 from USB during bring-up. Do not back-feed two
  supplies at once.
- **Common ground is mandatory:** L298N `GND` ↔ ESP32 `GND`.
- IR modules `VCC` → 5V rail, `GND` → common ground, `OUT` → GPIO above.

## Sensor polarity — to be determined at bring-up

IR obstacle modules vary: some pull OUT **LOW** on detection, some **HIGH**.
Which level means "black-body endpoint reached" is set by `SENSOR_ACTIVE_LEVEL`
in `config.h`. The Module 1 diagnostic prints every sensor's raw reading so the
correct level can be read off the real hardware instead of guessed.
