#include "GantryController.h"

#include "config.h"

namespace {

const char *faultName(AxisFault fault) {
  switch (fault) {
    case AxisFault::NONE:
      return "NONE";
    case AxisFault::TRAVEL_TIMEOUT:
      return "TRAVEL_TIMEOUT";
    case AxisFault::BOTH_LIMITS_ACTIVE:
      return "BOTH_LIMITS_ACTIVE";
    case AxisFault::OPPOSITE_LIMIT_HIT:
      return "OPPOSITE_LIMIT_HIT";
    case AxisFault::STUCK_AT_LIMIT:
      return "STUCK_AT_LIMIT";
  }
  return "UNKNOWN";
}

const char *likelyCause(AxisFault fault) {
  switch (fault) {
    case AxisFault::OPPOSITE_LIMIT_HIT:
      return "likely cause: that axis's FORWARD mapping constant is flipped "
             "(X_FORWARD_IS_TOWARD_MAX / Y_FORWARD_IS_TOWARD_MAX in config.h)";
    case AxisFault::BOTH_LIMITS_ACTIVE:
      return "likely cause: sensor polarity or placement is wrong for that axis";
    case AxisFault::TRAVEL_TIMEOUT:
      return "likely cause: no endpoint was seen - check that axis's sensor "
             "wiring, or its run speed";
    case AxisFault::STUCK_AT_LIMIT:
      return "likely cause: the axis is not leaving the end it started at - "
             "if it started at that end, flip that axis's FORWARD mapping "
             "constant; otherwise check for a jam or too low a speed";
    default:
      return "";
  }
}

const char *stateName(AxisState state) {
  switch (state) {
    case AxisState::IDLE:
      return "IDLE";
    case AxisState::RUNNING:
      return "RUNNING";
    case AxisState::FAULT:
      return "FAULT";
  }
  return "UNKNOWN";
}

const char *dirName(AxisDir dir) {
  return dir == AxisDir::TOWARD_MAX ? "TOWARD_MAX" : "TOWARD_MIN";
}

}  // namespace

void GantryController::begin(Motor &motorX, Motor &motorY, LimitSensor &xMin,
                              LimitSensor &xMax, LimitSensor &yMin,
                              LimitSensor &yMax) {
  _motorX = &motorX;
  _motorY = &motorY;
  _xMin = &xMin;
  _xMax = &xMax;
  _yMin = &yMin;
  _yMax = &yMax;

  AxisConfig cfg{REVERSE_LOCKOUT_MS, REVERSE_PAUSE_MS, MAX_TRAVEL_MS, MAX_CLEAR_MS};
  _axisX.begin(cfg);
  _axisY.begin(cfg);

  _speedX = (RUN_SPEED_X > MAX_DUTY) ? MAX_DUTY : RUN_SPEED_X;
  _speedY = (RUN_SPEED_Y > MAX_DUTY) ? MAX_DUTY : RUN_SPEED_Y;
}

void GantryController::startRun(uint32_t nowMs) {
  _axisX.start(nowMs, AxisDir::TOWARD_MAX);
  _axisY.start(nowMs, AxisDir::TOWARD_MAX);
  _cmdX = MotorCommandState{};
  _cmdY = MotorCommandState{};
  _telX = AxisTelemetry{};
  _telY = AxisTelemetry{};
  _telX.lastEventMs = nowMs;
  _telY.lastEventMs = nowMs;
  _running = true;
}

void GantryController::haltRun() {
  _motorX->stop();
  _motorY->stop();
  _axisX.halt();
  _axisY.halt();
  _cmdX = MotorCommandState{};
  _cmdY = MotorCommandState{};
  _running = false;
}

void GantryController::setSpeedX(uint8_t duty) {
  uint8_t clamped = duty;
  if (clamped > MAX_DUTY) {
    Serial.print("[sx] requested ");
    Serial.print(duty);
    Serial.print(" exceeds MAX_DUTY ");
    Serial.print(MAX_DUTY);
    Serial.print("; clamped to ");
    Serial.println(MAX_DUTY);
    clamped = MAX_DUTY;
  }
  _speedX = clamped;
  Serial.print("[sx] axis X run speed set to ");
  Serial.println(_speedX);
}

void GantryController::setSpeedY(uint8_t duty) {
  uint8_t clamped = duty;
  if (clamped > MAX_DUTY) {
    Serial.print("[sy] requested ");
    Serial.print(duty);
    Serial.print(" exceeds MAX_DUTY ");
    Serial.print(MAX_DUTY);
    Serial.print("; clamped to ");
    Serial.println(MAX_DUTY);
    clamped = MAX_DUTY;
  }
  _speedY = clamped;
  Serial.print("[sy] axis Y run speed set to ");
  Serial.println(_speedY);
}

void GantryController::printAxisStatusLine(const char *axisName, const Axis &axis,
                                            uint8_t speed, const AxisTelemetry &telemetry) {
  Serial.print("  ");
  Serial.print(axisName);
  Serial.print(": state=");
  Serial.print(stateName(axis.getState()));
  Serial.print(" heading=");
  Serial.print(dirName(axis.getDir()));
  Serial.print(" speed=");
  Serial.print(speed);
  Serial.print(" reversals=");
  Serial.print(axis.getReversalCount());
  Serial.print(" fault=");
  Serial.print(faultName(axis.getFault()));
  if (RUN_TELEMETRY) {
    Serial.print(" longestSegmentMs=");
    Serial.print(telemetry.longestSegmentMs);
    Serial.print(" longestClearMs=");
    Serial.print(telemetry.longestClearMs);
  }
  Serial.println();
}

void GantryController::printStatus() {
  Serial.println("[st] gantry status:");
  printAxisStatusLine("X", _axisX, _speedX, _telX);
  printAxisStatusLine("Y", _axisY, _speedY, _telY);
  Serial.print("  running=");
  Serial.println(_running ? "YES" : "no");
}

bool GantryController::takeFaultFlag() {
  bool v = _faultLatched;
  _faultLatched = false;
  return v;
}

void GantryController::invalidateCommandCache() {
  _cmdX = MotorCommandState{};
  _cmdY = MotorCommandState{};
}

// Module 4: purely observational run telemetry. Never calls
// Motor::drive()/stop(), never writes to an Axis, and never influences
// applyAxisCommand() or the fault path in update() — it only reads the
// AxisOutput/Axis/sensor state already computed by update() for this tick
// and prints a line / updates a running maximum. No-op entirely when
// RUN_TELEMETRY (config.h) is false.
void GantryController::updateTelemetry(const char *axisName, AxisTelemetry &telemetry,
                                        const Axis &axis, const AxisOutput &out,
                                        bool minActive, bool maxActive, uint32_t nowMs) {
  if (!RUN_TELEMETRY) {
    return;
  }

  if (out.reversed) {
    const uint32_t elapsed = nowMs - telemetry.lastEventMs;  // unsigned, rollover-safe
    if (elapsed > telemetry.longestSegmentMs) {
      telemetry.longestSegmentMs = elapsed;
    }
    Serial.print("[run] ");
    Serial.print(axisName);
    Serial.print(" reversed -> ");
    Serial.print(dirName(out.dir));
    Serial.print(" after ");
    Serial.print(elapsed);
    Serial.print(" ms (includes the pause), reversal ");
    Serial.println(axis.getReversalCount());
    telemetry.lastEventMs = nowMs;
    telemetry.waitingForRunAfterReversal = true;
    telemetry.waitingForClear = false;
    return;  // this tick is fully accounted for; nothing else to evaluate
  }

  // First tick the axis is allowed to run again after a reversal (the
  // post-reversal pause has elapsed): start the clear-time clock.
  if (telemetry.waitingForRunAfterReversal && out.run) {
    telemetry.waitingForRunAfterReversal = false;
    telemetry.waitingForClear = true;
    telemetry.clearStartMs = nowMs;
  }

  if (telemetry.waitingForClear) {
    // "Behind" is relative to the heading the axis reversed INTO (out.dir),
    // i.e. the end it just reversed off of — same definition Axis.cpp uses
    // internally for OPPOSITE_LIMIT_HIT/STUCK_AT_LIMIT.
    const bool behindIsMin = (out.dir == AxisDir::TOWARD_MAX);
    const bool behindActive = behindIsMin ? minActive : maxActive;
    if (!behindActive) {
      const uint32_t clearElapsed = nowMs - telemetry.clearStartMs;  // rollover-safe
      if (clearElapsed > telemetry.longestClearMs) {
        telemetry.longestClearMs = clearElapsed;
      }
      Serial.print("[run] ");
      Serial.print(axisName);
      Serial.print(" cleared the end in ");
      Serial.print(clearElapsed);
      Serial.println(" ms");
      telemetry.waitingForClear = false;
    }
  }
}

void GantryController::printFaultLine(const char *axisName, AxisFault fault) {
  Serial.print("[fault] axis ");
  Serial.print(axisName);
  Serial.print(" FAULT: ");
  Serial.print(faultName(fault));
  Serial.print(" - ");
  Serial.println(likelyCause(fault));
}

void GantryController::applyAxisCommand(Motor &motor, const AxisOutput &out,
                                         uint8_t speed, bool forwardIsTowardMax,
                                         MotorCommandState &cmd) {
  if (!out.run) {
    if (!cmd.hasCommand || cmd.running) {
      motor.stop();
      cmd.hasCommand = true;
      cmd.running = false;
    }
    return;
  }

  // Map AxisDir -> Motor::Direction using the axis's FORWARD mapping.
  const bool wantTowardMax = (out.dir == AxisDir::TOWARD_MAX);
  const bool wantForward = (wantTowardMax == forwardIsTowardMax);
  const Direction dir = wantForward ? Direction::FORWARD : Direction::REVERSE;

  const bool changed =
      !cmd.hasCommand || !cmd.running || cmd.dir != dir || cmd.duty != speed;
  if (changed) {
    motor.drive(dir, speed);
    cmd.hasCommand = true;
    cmd.running = true;
    cmd.dir = dir;
    cmd.duty = speed;
  }
}

void GantryController::update(uint32_t nowMs) {
  if (!_running) {
    return;
  }

  AxisOutput outX = _axisX.update(nowMs, _xMin->active(), _xMax->active());
  AxisOutput outY = _axisY.update(nowMs, _yMin->active(), _yMax->active());

  const bool faultedX = (outX.state == AxisState::FAULT);
  const bool faultedY = (outY.state == AxisState::FAULT);

  if (faultedX || faultedY) {
    // Stop everything immediately on either axis faulting, latch the
    // flag, and report which axis/fault/cause before leaving run mode.
    _motorX->stop();
    _motorY->stop();
    _axisX.halt();
    _axisY.halt();
    _cmdX = MotorCommandState{};
    _cmdY = MotorCommandState{};
    _running = false;
    _faultLatched = true;

    if (faultedX) {
      printFaultLine("X", outX.fault);
    }
    if (faultedY) {
      printFaultLine("Y", outY.fault);
    }
    return;
  }

  applyAxisCommand(*_motorX, outX, _speedX, X_FORWARD_IS_TOWARD_MAX, _cmdX);
  applyAxisCommand(*_motorY, outY, _speedY, Y_FORWARD_IS_TOWARD_MAX, _cmdY);

  // Module 4: observation only, strictly after the motor commands above
  // have already been decided and issued for this tick. Reads the same
  // AxisOutput/sensor values applyAxisCommand() just used but never calls
  // Motor::drive()/stop() or writes to an Axis itself.
  updateTelemetry("X", _telX, _axisX, outX, _xMin->active(), _xMax->active(), nowMs);
  updateTelemetry("Y", _telY, _axisY, outY, _yMin->active(), _yMax->active(), nowMs);
}
