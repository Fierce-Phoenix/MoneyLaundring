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
                                            uint8_t speed) {
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
  Serial.println(faultName(axis.getFault()));
}

void GantryController::printStatus() {
  Serial.println("[st] gantry status:");
  printAxisStatusLine("X", _axisX, _speedX);
  printAxisStatusLine("Y", _axisY, _speedY);
  Serial.print("  running=");
  Serial.println(_running ? "YES" : "no");
}

bool GantryController::takeFaultFlag() {
  bool v = _faultLatched;
  _faultLatched = false;
  return v;
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
}
