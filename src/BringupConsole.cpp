#include "BringupConsole.h"

#include "config.h"

namespace {

// All candidate GPIOs the `scan` command may put into INPUT mode, verbatim
// from the Module 1 spec. The six configured motor-driver pins are removed
// from this list at scan time (never touched by scan at all) so a wire
// still attached to a direction/enable pin can't be reassigned to INPUT
// mid-bring-up.
constexpr uint8_t SCAN_CANDIDATE_PINS[] = {
    4, 5, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39,
};
constexpr size_t SCAN_CANDIDATE_COUNT =
    sizeof(SCAN_CANDIDATE_PINS) / sizeof(SCAN_CANDIDATE_PINS[0]);

constexpr unsigned long SCAN_BASELINE_MS = 1000;

bool isMotorDriverPin(uint8_t pin) {
  return pin == X_ENA || pin == X_IN1 || pin == X_IN2 || pin == Y_ENB ||
         pin == Y_IN3 || pin == Y_IN4;
}

}  // namespace

void BringupConsole::begin(Motor &motorX, Motor &motorY, LimitSensor &xMin,
                            LimitSensor &xMax, LimitSensor &yMin,
                            LimitSensor &yMax) {
  _motorX = &motorX;
  _motorY = &motorY;
  _xMin = &xMin;
  _xMax = &xMax;
  _yMin = &yMin;
  _yMax = &yMax;

  _activeLevel = SENSOR_ACTIVE_LEVEL;
  _jogDuty = DEFAULT_SPEED;
  _lastCommandMs = millis();
  _lastMonitorMs = millis();
}

void BringupConsole::printPinMapAndTunables() {
  Serial.println("  Motor driver (L298N):");
  Serial.print("    X_ENA=");
  Serial.print(X_ENA);
  Serial.print("  X_IN1=");
  Serial.print(X_IN1);
  Serial.print("  X_IN2=");
  Serial.println(X_IN2);
  Serial.print("    Y_ENB=");
  Serial.print(Y_ENB);
  Serial.print("  Y_IN3=");
  Serial.print(Y_IN3);
  Serial.print("  Y_IN4=");
  Serial.println(Y_IN4);
  Serial.println("  IR endpoint sensors:");
  Serial.print("    X_MIN=");
  Serial.print(X_MIN);
  Serial.print("  X_MAX=");
  Serial.println(X_MAX);
  Serial.print("    Y_MIN=");
  Serial.print(Y_MIN);
  Serial.print("  Y_MAX=");
  Serial.println(Y_MAX);
  Serial.println("  Tunables:");
  Serial.print("    SERIAL_BAUD=");
  Serial.println(SERIAL_BAUD);
  Serial.print("    PWM_FREQ=");
  Serial.print(PWM_FREQ);
  Serial.print("  PWM_RES=");
  Serial.println(PWM_RES);
  Serial.print("    DEBOUNCE_MS=");
  Serial.println(DEBOUNCE_MS);
  Serial.print("    SENSOR_ACTIVE_LEVEL (compiled default)=");
  Serial.println(SENSOR_ACTIVE_LEVEL == HIGH ? "HIGH" : "LOW");
  Serial.print("    current active level (runtime, via pol)=");
  Serial.println(_activeLevel == HIGH ? "HIGH" : "LOW");
  Serial.print("    JOG_MS=");
  Serial.println(JOG_MS);
  Serial.print("    MONITOR_PERIOD_MS=");
  Serial.println(MONITOR_PERIOD_MS);
  Serial.print("    ARM_TIMEOUT_MS=");
  Serial.println(ARM_TIMEOUT_MS);
  Serial.print("    SCAN_WINDOW_MS=");
  Serial.println(SCAN_WINDOW_MS);
  Serial.print("    SUPPLY_VOLTS=");
  Serial.print(SUPPLY_VOLTS);
  Serial.print("  MOTOR_RATED_VOLTS=");
  Serial.println(MOTOR_RATED_VOLTS);
  Serial.print("    MAX_DUTY=");
  Serial.println(MAX_DUTY);
  Serial.print("    current jog speed (duty)=");
  Serial.println(_jogDuty);
}

void BringupConsole::printBanner() {
  Serial.println();
  Serial.println("=============================================================");
  Serial.println(" MoneyLaundring — Module 1 hardware bring-up console");
  Serial.println("=============================================================");
  Serial.println(" !!! PIN MAP BELOW IS UNCONFIRMED !!!");
  Serial.println(" It was never read off the physical board. Use `scan`, `mon`,");
  Serial.println(" and `pol` to verify every pin and the sensor polarity against");
  Serial.println(" the real wiring BEFORE typing `arm`. Motors will not move");
  Serial.println(" until you arm, and auto-disarm after an idle timeout.");
  Serial.println("-------------------------------------------------------------");
  printPinMapAndTunables();
  Serial.println("-------------------------------------------------------------");
  Serial.println(" Type ? for the command list.");
  Serial.println("=============================================================");
}

void BringupConsole::cmdHelp() {
  Serial.println("Commands:");
  Serial.println("  ?           show this help");
  Serial.println("  map         print pin map and tunables");
  Serial.println("  mon         toggle the continuous sensor status line (on by default)");
  Serial.println("  pol         flip the active sensor level and print the new value");
  Serial.println("  scan        scan candidate GPIOs for level changes (disarmed only)");
  Serial.println("  arm         enable motor output (auto-disarms if idle too long)");
  Serial.println("  disarm      disable motor output");
  Serial.println("  stop        stop both motors immediately and disarm");
  Serial.println("  speed <n>   set jog duty 0-255 (clamped to MAX_DUTY)");
  Serial.println("  xf          jog X forward for JOG_MS (armed only)");
  Serial.println("  xr          jog X reverse for JOG_MS (armed only)");
  Serial.println("  yf          jog Y forward for JOG_MS (armed only)");
  Serial.println("  yr          jog Y reverse for JOG_MS (armed only)");
}

void BringupConsole::cmdMap() {
  Serial.println("[map] pin map and tunables:");
  printPinMapAndTunables();
}

void BringupConsole::applyActiveLevelToSensors() {
  _xMin->setActiveLevel(_activeLevel);
  _xMax->setActiveLevel(_activeLevel);
  _yMin->setActiveLevel(_activeLevel);
  _yMax->setActiveLevel(_activeLevel);
}

void BringupConsole::cmdMon() {
  _monitoring = !_monitoring;
  Serial.print("[mon] monitor ");
  Serial.println(_monitoring ? "ON" : "OFF");
}

void BringupConsole::cmdPol() {
  _activeLevel = (_activeLevel == HIGH) ? LOW : HIGH;
  applyActiveLevelToSensors();
  Serial.print("[pol] active level is now ");
  Serial.println(_activeLevel == HIGH ? "HIGH" : "LOW");
  Serial.print("[pol] to make this permanent, set SENSOR_ACTIVE_LEVEL = ");
  Serial.print(_activeLevel == HIGH ? "HIGH" : "LOW");
  Serial.println(" in include/config.h");
}

void BringupConsole::printSensorLine() {
  Serial.print("[mon] X_MIN(");
  Serial.print(X_MIN);
  Serial.print(")=");
  Serial.print(_xMin->raw());
  Serial.print(_xMin->active() ? "*" : " ");
  Serial.print("  X_MAX(");
  Serial.print(X_MAX);
  Serial.print(")=");
  Serial.print(_xMax->raw());
  Serial.print(_xMax->active() ? "*" : " ");
  Serial.print("  Y_MIN(");
  Serial.print(Y_MIN);
  Serial.print(")=");
  Serial.print(_yMin->raw());
  Serial.print(_yMin->active() ? "*" : " ");
  Serial.print("  Y_MAX(");
  Serial.print(Y_MAX);
  Serial.print(")=");
  Serial.print(_yMax->raw());
  Serial.print(_yMax->active() ? "*" : " ");
  Serial.print("  (level/active*, pol=");
  Serial.print(_activeLevel == HIGH ? "HIGH" : "LOW");
  Serial.println(")");
}

void BringupConsole::cmdScanStart(unsigned long nowMs) {
  if (_armed) {
    Serial.println("[scan] refused: disarm first (scan only runs while disarmed)");
    return;
  }
  if (_scanPhase != ScanPhase::IDLE) {
    Serial.println("[scan] already running");
    return;
  }

  _scanPinCount = 0;
  for (size_t i = 0; i < SCAN_CANDIDATE_COUNT && _scanPinCount < SCAN_MAX_PINS; i++) {
    uint8_t pin = SCAN_CANDIDATE_PINS[i];
    if (isMotorDriverPin(pin)) {
      continue;  // never touch the six configured motor-driver pins
    }
    pinMode(pin, INPUT);  // never OUTPUT — these are unidentified/sensor pins
    _scanPins[_scanPinCount] = pin;
    _scanPinCount++;
  }

  Serial.print("[scan] watching ");
  Serial.print(_scanPinCount);
  Serial.println(" candidate pins (motor-driver pins excluded).");
  Serial.println("[scan] warning: unconnected pins may float and flicker on their own.");
  Serial.println("[scan] capturing 1s baseline...");

  _scanPhase = ScanPhase::BASELINE;
  _scanPhaseStartMs = nowMs;
}

void BringupConsole::cmdArm(unsigned long nowMs) {
  if (_scanPhase != ScanPhase::IDLE) {
    Serial.println("[arm] refused: scan is still running");
    return;
  }
  _armed = true;
  _lastCommandMs = nowMs;
  Serial.print("[arm] ARMED. Motors may move. Auto-disarm after ");
  Serial.print(ARM_TIMEOUT_MS);
  Serial.println(" ms without a command.");
}

void BringupConsole::cmdDisarm(const char *reason) {
  bool wasArmed = _armed;
  _armed = false;
  _jogAxis = JogAxis::NONE;
  _motorX->stop();
  _motorY->stop();
  if (wasArmed) {
    Serial.print("[disarm] ");
    Serial.println(reason);
  }
}

void BringupConsole::cmdStop() {
  _motorX->stop();
  _motorY->stop();
  _jogAxis = JogAxis::NONE;
  _armed = false;
  Serial.println("[stop] both motors stopped, disarmed");
}

void BringupConsole::cmdSpeed(const String &arg) {
  if (arg.length() == 0) {
    Serial.print("[speed] current jog duty is ");
    Serial.println(_jogDuty);
    return;
  }
  long requested = arg.toInt();
  if (requested < 0) {
    requested = 0;
  }
  if (requested > 255) {
    requested = 255;
  }
  uint8_t clamped = (uint8_t)requested;
  if (clamped > MAX_DUTY) {
    Serial.print("[speed] requested ");
    Serial.print(requested);
    Serial.print(" exceeds MAX_DUTY ");
    Serial.print(MAX_DUTY);
    Serial.print("; clamped to ");
    Serial.println(MAX_DUTY);
    clamped = MAX_DUTY;
  }
  _jogDuty = clamped;
  Serial.print("[speed] jog duty set to ");
  Serial.println(_jogDuty);
}

Motor *BringupConsole::motorFor(JogAxis axis) {
  if (axis == JogAxis::X) {
    return _motorX;
  }
  if (axis == JogAxis::Y) {
    return _motorY;
  }
  return nullptr;
}

void BringupConsole::cmdJog(JogAxis axis, Direction dir, const char *label,
                             unsigned long nowMs) {
  if (!_armed) {
    Serial.print("[");
    Serial.print(label);
    Serial.println("] refused: not armed. Type `arm` first.");
    return;
  }
  if (_jogAxis != JogAxis::NONE) {
    Serial.print("[");
    Serial.print(label);
    Serial.println("] refused: a jog is already in progress.");
    return;
  }

  Motor *m = motorFor(axis);
  if (m == nullptr) {
    return;
  }

  Serial.print("[");
  Serial.print(label);
  Serial.print("] driving for ");
  Serial.print(JOG_MS);
  Serial.print(" ms @ duty ");
  Serial.println(_jogDuty);

  m->drive(dir, _jogDuty);
  _jogAxis = axis;
  _jogDir = dir;
  _jogStartMs = nowMs;
}

void BringupConsole::handleLine(String line, unsigned long nowMs) {
  line.trim();
  if (line.length() == 0) {
    return;
  }

  _lastCommandMs = nowMs;

  String lower = line;
  lower.toLowerCase();

  String cmd = lower;
  String arg = "";
  int spaceIdx = lower.indexOf(' ');
  if (spaceIdx >= 0) {
    cmd = lower.substring(0, spaceIdx);
    arg = lower.substring(spaceIdx + 1);
    arg.trim();
  }

  if (cmd == "?") {
    cmdHelp();
  } else if (cmd == "map") {
    cmdMap();
  } else if (cmd == "mon") {
    cmdMon();
  } else if (cmd == "pol") {
    cmdPol();
  } else if (cmd == "scan") {
    cmdScanStart(nowMs);
  } else if (cmd == "arm") {
    cmdArm(nowMs);
  } else if (cmd == "disarm") {
    cmdDisarm("manual disarm");
  } else if (cmd == "stop") {
    cmdStop();
  } else if (cmd == "speed") {
    cmdSpeed(arg);
  } else if (cmd == "xf") {
    cmdJog(JogAxis::X, Direction::FORWARD, "xf", nowMs);
  } else if (cmd == "xr") {
    cmdJog(JogAxis::X, Direction::REVERSE, "xr", nowMs);
  } else if (cmd == "yf") {
    cmdJog(JogAxis::Y, Direction::FORWARD, "yf", nowMs);
  } else if (cmd == "yr") {
    cmdJog(JogAxis::Y, Direction::REVERSE, "yr", nowMs);
  } else {
    Serial.print("[?] unknown command: ");
    Serial.println(line);
    Serial.println("Type ? for the command list.");
  }
}

void BringupConsole::updateArmTimeout(unsigned long nowMs) {
  if (_armed && (nowMs - _lastCommandMs >= ARM_TIMEOUT_MS)) {
    cmdDisarm("idle timeout reached, auto-disarmed");
  }
}

void BringupConsole::updateJog(unsigned long nowMs) {
  if (_jogAxis == JogAxis::NONE) {
    return;
  }
  if (nowMs - _jogStartMs >= JOG_MS) {
    Motor *m = motorFor(_jogAxis);
    if (m != nullptr) {
      m->stop();
    }
    Serial.println("[jog] done, stopped");
    _jogAxis = JogAxis::NONE;
  }
}

void BringupConsole::updateScan(unsigned long nowMs) {
  if (_scanPhase == ScanPhase::IDLE) {
    return;
  }

  if (_scanPhase == ScanPhase::BASELINE) {
    if (nowMs - _scanPhaseStartMs >= SCAN_BASELINE_MS) {
      for (size_t i = 0; i < _scanPinCount; i++) {
        _scanLastLevel[i] = digitalRead(_scanPins[i]);
      }
      Serial.println("[scan] baseline captured. Touch/cover one sensor at a time now.");
      Serial.print("[scan] watching for ");
      Serial.print(SCAN_WINDOW_MS);
      Serial.println(" ms...");
      _scanPhase = ScanPhase::WINDOW;
      _scanPhaseStartMs = nowMs;
    }
    return;
  }

  // ScanPhase::WINDOW
  for (size_t i = 0; i < _scanPinCount; i++) {
    int level = digitalRead(_scanPins[i]);
    if (level != _scanLastLevel[i]) {
      _scanLastLevel[i] = level;
      Serial.print("[scan] GPIO ");
      Serial.print(_scanPins[i]);
      Serial.print(" -> ");
      Serial.println(level);
    }
  }

  if (nowMs - _scanPhaseStartMs >= SCAN_WINDOW_MS) {
    // Restore: the pins we touched have no other configured role in this
    // module (the six motor-driver pins were never touched), so leaving
    // them as INPUT is the correct restored state. The four sensor pins
    // among them are re-applied as INPUT here for clarity, matching how
    // LimitSensor::begin() already configured them.
    for (size_t i = 0; i < _scanPinCount; i++) {
      pinMode(_scanPins[i], INPUT);
    }
    Serial.println("[scan] window complete. Pin modes restored.");
    _scanPhase = ScanPhase::IDLE;
    _scanPinCount = 0;
  }
}

void BringupConsole::updateMonitor(unsigned long nowMs) {
  if (!_monitoring) {
    return;
  }
  if (nowMs - _lastMonitorMs >= MONITOR_PERIOD_MS) {
    _lastMonitorMs = nowMs;
    printSensorLine();
  }
}

void BringupConsole::update(unsigned long nowMs) {
  _xMin->update(nowMs);
  _xMax->update(nowMs);
  _yMin->update(nowMs);
  _yMax->update(nowMs);

  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      handleLine(_inputBuffer, nowMs);
      _inputBuffer = "";
    } else if (c != '\r') {
      _inputBuffer += c;
    }
  }

  updateArmTimeout(nowMs);
  updateJog(nowMs);
  updateScan(nowMs);
  updateMonitor(nowMs);
}
