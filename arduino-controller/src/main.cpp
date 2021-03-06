#include <AccelStepper.h>
#include <OneButton.h>
#include <Fsm.h>
#include <EEPROM.h>
#include <Wire.h>

// Forward declarations
boolean moveBlindsUpButtonActive();
boolean moveBlindsDownButtonActive();
boolean atTop();
boolean atBottom();
boolean autoModeEnabled();
boolean readyForAutoCmd();
boolean isBrightOutside();
boolean stopPinActive();
boolean atSetMidpointPosition();
boolean isValidCommand(int command);
int getI2CAddress();
void handleReceivedCommand();
long getDesiredBlindPosition();
void receiveI2CCommand(int numByets);
void i2cStateRequestHandler();
void setReceivedCommand(int command);
int getReceivedCommand();
void storeCurrentPositionAsBottom();
void setDesiredBlindPosition(long newPosition);
void homeStepper();
void moveBlinds(long longPosition);
void stopBlinds();
void initializeBottomPosition();
void initializePinIO();
void disableSolenoid();
void initializeSwitchHandlers();
void initializeStepper();
void initializeFSMTransitions();
void enableSolenoid();
void disableSolenoid();
void setCurrentPositionAsHome();
void setSolenoidLowPowerMode();
void runAutoModeRoutines();
void onStoppedStateEnter();
void onMovingStateEnter();
void onTransMovingToStoppedByMotorsAtTop();
void onTransStoppedToMovingByDownClick();
void onTransStoppedToMovingByUpClick();
void onTransStoppedToMovingByMoveToPosition();
void onTransMovingToMovingByMoveCommand();

#define DEBUG_MODE true

// Pin 0 not used
// Pin 1 not used
#define STEP_PIN    2
#define I2C_4       4
#define DIRECTION   5
#define SOLENOID    6
#define AUTO_MODE   7
#define ENABLE_PIN  8
#define STOP_PIN    9
#define I2C_0       10
#define I2C_1       11
#define I2C_2       12
#define I2C_3       13
#define UP_PIN      A0
#define DOWN_PIN    A1
#define TEMPERATURE A2
#define LIGHT       A3
#define I2C_SDA     A4
#define I2C_SCL     A5

// Stepper driver modules
#define DRV8825 0
#define TMC2209 1

// Should be one of the above driver modules
#define DRIVER_MODULE TMC2209

#if DRIVER_MODULE == DRV8825
  #define STEPS_PER_SHAFT_REVOLUTION 800L
  #define DIRECTION_PIN_INVERTED false
  #define STEP_PIN_INVERTED false
  #define ENABLE_PIN_INVERTED true
  #define SOLENOID_ENABLE_STEPPER_BUMP 250
#elif DRIVER_MODULE == TMC2209
  #define STEPS_PER_SHAFT_REVOLUTION 1600L
  #define DIRECTION_PIN_INVERTED true
  #define STEP_PIN_INVERTED false
  #define ENABLE_PIN_INVERTED true
  #define SOLENOID_ENABLE_STEPPER_BUMP 400
#else
  #error You need to select a DRIVER_MODULE
#endif

#define GEAR_RATIO 5.18181818L
#define AUTO_TRANSITION_INTERVAL (unsigned long)18000000 // 5 hours (or use 100000 for testing)
#define REVOLUTION (long)(STEPS_PER_SHAFT_REVOLUTION * GEAR_RATIO)
#define MAX_SPEED REVOLUTION * 0.4 // The UNO can do just over 4000 steps/s :'(
#define ACCELERATION MAX_SPEED * 1.8
#define SWITCHES_ACTIVE_LOW true
#define SWITCHES_USE_PULLUP true
#define BOTTOM_POSITION_ADDRESS 0
#define SLIP_CORRECTION 2 * REVOLUTION
#define SOLENOID_PWM_LEVEL 110
#define CYCLES_BEFORE_SOLENOID_LOW_POWER 30000L
#define HOMING_STATE_RETURN_VALUE 200

// I2C commands
#define STOP_COMMAND 101
#define DOWN_FOREVER_COMMAND 102
#define STORE_COMMAND 103
#define UP_FOREVER_COMMAND 104
#define FOREVER_POSITION 100*REVOLUTION
#define NOOP 200

// FSM events
#define AUTO_DOUBLE_CLICK   1
#define DOWN_CLICK          2
#define UP_CLICK            3
#define MOTORS_AT_TOP       5
#define STOP                6
#define MOVE_COMMAND        7

boolean isHoming = false;
unsigned long lastMoveAt = 0;
unsigned long startedAt;
long desiredBlindPosition;
long bottomPosition;
int loopCnt = 0;
int lastReceivedCommand = NOOP;

// Stepper
AccelStepper stepper = AccelStepper(AccelStepper::DRIVER, STEP_PIN, DIRECTION);

// Buttons
OneButton upSwitch = OneButton(UP_PIN, SWITCHES_ACTIVE_LOW, SWITCHES_USE_PULLUP);
OneButton downSwitch = OneButton(DOWN_PIN, SWITCHES_ACTIVE_LOW, SWITCHES_USE_PULLUP);
OneButton autoSwitch = OneButton(AUTO_MODE, SWITCHES_ACTIVE_LOW, SWITCHES_USE_PULLUP);

// FSM
State stoppedState(&onStoppedStateEnter, NULL, NULL);
State movingState(&onMovingStateEnter, NULL, NULL);
Fsm fsm(&stoppedState);

void setup() {
  Serial.begin(9600);
  Serial.print("Initializing\n");
  initializeBottomPosition();
  initializePinIO();
  disableSolenoid();  
  initializeStepper();
  Wire.begin(getI2CAddress());
  Wire.onReceive(receiveI2CCommand);
  Wire.onRequest(i2cStateRequestHandler);
  homeStepper();
  initializeSwitchHandlers();
  initializeFSMTransitions();
  fsm.run_machine();
  Serial.println("Done initialization");
  startedAt = millis();
}

void loop() {

  if (atTop() && getDesiredBlindPosition() <= 0) {
    fsm.trigger(MOTORS_AT_TOP);
  } else if (stepper.distanceToGo() == 0) {
    fsm.trigger(STOP);
  }

  handleReceivedCommand();

  // Only tick periodically to speed up the run loop. This
  // allows the motors to run a bit smoother
  // Uncomment if you're using a hardwired auto switch
  // if (loopCnt % 5000 == 0) {
  //   runAutoModeRoutines();
  // }

  if (loopCnt % 50 == 0) {
    // Tick the switches
    upSwitch.tick();
    downSwitch.tick();
    autoSwitch.tick();
  }

  // Run the steppers
  stepper.run();
  loopCnt++;
}

void initializePinIO() {
  pinMode(DIRECTION, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(STOP_PIN, INPUT_PULLUP);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(SOLENOID, OUTPUT);
  pinMode(UP_PIN, INPUT_PULLUP);
  pinMode(DOWN_PIN, INPUT_PULLUP);
  pinMode(LIGHT, INPUT_PULLUP);
  pinMode(AUTO_MODE, INPUT_PULLUP);
  pinMode(I2C_0, INPUT_PULLUP);
  pinMode(I2C_1, INPUT_PULLUP);
  pinMode(I2C_2, INPUT_PULLUP);
  pinMode(I2C_3, INPUT_PULLUP);
  pinMode(I2C_4, INPUT_PULLUP);
}

void initializeFSMTransitions() {
  fsm.add_transition(&stoppedState, &movingState, DOWN_CLICK, &onTransStoppedToMovingByDownClick);
  fsm.add_transition(&stoppedState, &movingState, UP_CLICK, &onTransStoppedToMovingByUpClick);
  fsm.add_transition(&stoppedState, &movingState, MOVE_COMMAND, NULL);
  fsm.add_transition(&movingState, &movingState, MOVE_COMMAND, &onTransMovingToMovingByMoveCommand);
  fsm.add_transition(&movingState, &stoppedState, STOP, NULL);
  fsm.add_transition(&movingState, &stoppedState, DOWN_CLICK, NULL);
  fsm.add_transition(&movingState, &stoppedState, UP_CLICK, NULL);
  fsm.add_transition(&movingState, &stoppedState, MOTORS_AT_TOP, &onTransMovingToStoppedByMotorsAtTop);
}

// FSM transition events
void onStoppedStateEnter() {
  Serial.println("Stopped state enter");
  stopBlinds();
  setDesiredBlindPosition(stepper.currentPosition());
}

void onTransMovingToStoppedByMotorsAtTop() {
  Serial.println("Transition from moving up to stopped");
  stepper.stop();
  stepper.runToPosition();
  setCurrentPositionAsHome();
}

void onTransStoppedToMovingByUpClick() {
  setDesiredBlindPosition(0 - SLIP_CORRECTION);
}

void onTransStoppedToMovingByDownClick() {
  enableSolenoid();
  stepper.enableOutputs();
  stepper.runToNewPosition(stepper.currentPosition() - SOLENOID_ENABLE_STEPPER_BUMP);
  setDesiredBlindPosition(bottomPosition);
}

void onTransMovingToMovingByMoveCommand() {
  // Serial.println("In onTransMovingToMovingByMovingCommand");
  // if (stepper.currentPosition() > getDesiredBlindPosition()) {
  //   stepper.moveTo(getDesiredBlindPosition());
  // }
}

void onMovingStateEnter() {
  // Serial.println("in onMovingStateEnter");
  moveBlinds(getDesiredBlindPosition());
}

void moveBlinds(long newPosition) {
  // Serial.print("Moving blinds to: ");
  // Serial.println(newPosition);

  lastMoveAt = millis();
  enableSolenoid();
  stepper.enableOutputs();
  stepper.moveTo(newPosition);

  // Provide a natural bit of movement before putting the stepper into low power mode
  for(long i = 0; i < CYCLES_BEFORE_SOLENOID_LOW_POWER; i++) {
    stepper.run();
  }

  setSolenoidLowPowerMode();
}

void stopBlinds() {
  stepper.stop();
  stepper.runToPosition();
  disableSolenoid();
  stepper.disableOutputs();
}

void runAutoModeRoutines() {
  if (autoModeEnabled() && readyForAutoCmd()) {
    if (isBrightOutside()) {
      if (stepper.currentPosition() > 0) {
        Serial.println("It is bright outside. Moving blinds up");
        fsm.trigger(UP_CLICK);
      }
    } else {
      if (stepper.currentPosition() != bottomPosition) {
        Serial.println("It is dark outside. Moving blinds down.");
        fsm.trigger(DOWN_CLICK);
      }
    }
  }
}

boolean isValidCommand(int command) {
  return (command >= 0 && command <= 103) || command == NOOP;
}

void setReceivedCommand(int command) {
  lastReceivedCommand = command;
}

int getReceivedCommand() {
  return lastReceivedCommand;
}

void handleReceivedCommand() {
  int command = getReceivedCommand();
  if (command != NOOP) {
    if (command == STOP_COMMAND) {
      fsm.trigger(STOP);
    } else if (command == STORE_COMMAND) {
      storeCurrentPositionAsBottom();
    } else if (command == DOWN_FOREVER_COMMAND) {
      if (getDesiredBlindPosition() == FOREVER_POSITION) {
        fsm.trigger(STOP);
      } else {
        setDesiredBlindPosition(FOREVER_POSITION);
        fsm.trigger(MOVE_COMMAND);
      }
    } else if (command == UP_FOREVER_COMMAND) {
      if (getDesiredBlindPosition() == -1*FOREVER_POSITION) {
        fsm.trigger(STOP);
      } else {
        setDesiredBlindPosition(-1*FOREVER_POSITION);
        fsm.trigger(MOVE_COMMAND);
      }
    } else {
      long newPosition = bottomPosition * command / 100;
      if (newPosition == 0) {
        newPosition = (0 - SLIP_CORRECTION);
      }
      if (newPosition <= 0 && getDesiredBlindPosition() == 0) {
        // Don't action this command.
      } else if (newPosition != getDesiredBlindPosition()) {
        setDesiredBlindPosition(newPosition);
        fsm.trigger(MOVE_COMMAND);
      } else {
        fsm.trigger(STOP);
      }
    }
    setReceivedCommand(NOOP);
  }
}

void i2cStateRequestHandler() {
  // Serial.println("Received request for state");
  byte buffer[2];
  long massagedDesiredPosition, massagedSetPosition;

  if (isHoming) {
    massagedDesiredPosition = massagedSetPosition = HOMING_STATE_RETURN_VALUE;
  } else {
    massagedDesiredPosition = getDesiredBlindPosition();
    massagedSetPosition = stepper.currentPosition();
    if (massagedDesiredPosition < 0) {
      massagedDesiredPosition = 0;
    }
    if (massagedSetPosition < 0) {
      massagedSetPosition = 0;
    }
    massagedDesiredPosition = roundf(massagedDesiredPosition * 100.0 / bottomPosition);
    massagedSetPosition = roundf(massagedSetPosition * 100.0 / bottomPosition);
  }

  buffer[0] = massagedSetPosition;
  buffer[1] = massagedDesiredPosition;
  // Serial.println((String)"Sending: " + buffer[0] + " and " + buffer[1]);
  Wire.write(buffer, 2);
}

void receiveI2CCommand(int bytes) {
  int cmd;
  if (bytes != 1) {
    Serial.println((String)"Weird number of bytes!: " + bytes);
  }
  for (int i = 0; i < bytes; i++) {
    cmd = Wire.read(); // receive byte as a character
  }

  // int cmd = Wire.read();
  // Serial.print((String)"Bytes argument: " + bytes);
  if(isValidCommand(cmd)) {
    if (cmd != NOOP) {
      Serial.println((String)"Received I2C command: " + cmd);
    }
    setReceivedCommand(cmd);
  } else {
    Serial.print("Invalid command: ");
    Serial.println(cmd);
  }
}

// Just for testing out the command functionality
// void serialEvent() {
//   while(Serial.available()) {
//     int cmd = Serial.parseInt();
//     // Serial.println("Received command: " + cmd);
//     handleCommand(cmd);
//   }
// }

long getDesiredBlindPosition() {
  return desiredBlindPosition;
}

void setDesiredBlindPosition(long newDesiredPosition) {
  desiredBlindPosition = newDesiredPosition;
}

boolean readyForAutoCmd() {
  return ((millis() - lastMoveAt) > AUTO_TRANSITION_INTERVAL);
}

void disableAllSteppers() {
  stepper.disableOutputs();
}

void initializeSwitchHandlers() {
  downSwitch.attachClick([]() {
    if (!atBottom()) {
      fsm.trigger(DOWN_CLICK);
    }
  });

  upSwitch.attachClick([]() {
    if (!atTop()) {
      fsm.trigger(UP_CLICK);
    }
  });

  autoSwitch.attachDoubleClick([]() {
    Serial.println("Double click on auto switch.");
    long eepromBottomPosition;
    EEPROM.get(BOTTOM_POSITION_ADDRESS, eepromBottomPosition);
    if(bottomPosition == eepromBottomPosition) {
      Serial.println("Updated bottom position to 40 revolutions");
      bottomPosition = 40*REVOLUTION;
    } else {
      if (moveBlindsUpButtonActive() && moveBlindsDownButtonActive()) {
        bottomPosition = stepper.currentPosition();
        Serial.println("Found up and down buttons pressed. Storing stepper's current position as the bottom position: ");
        Serial.println(bottomPosition);
        storeCurrentPositionAsBottom();
      } else {
        bottomPosition = eepromBottomPosition;
        Serial.println("Didn't see up and down pressed so just resetting the bottom position to what is stored in EEPROM: ");
        Serial.println(bottomPosition);
      }
    }
  });

  autoSwitch.attachLongPressStart([]() {
    Serial.println("Auto mode engaged.");
    lastMoveAt = millis() - AUTO_TRANSITION_INTERVAL;
  });
}

void initializeStepper() {
  stepper.setEnablePin(ENABLE_PIN);
  stepper.setPinsInverted(DIRECTION_PIN_INVERTED, STEP_PIN_INVERTED, ENABLE_PIN_INVERTED);
  stepper.setMaxSpeed(MAX_SPEED);
  stepper.setAcceleration(ACCELERATION);
  stepper.disableOutputs();
}

void homeStepper() {
  Serial.println("in homeStepper");
  Serial.println("Homing...");
  isHoming = true;
  stepper.enableOutputs();
  enableSolenoid();
  stepper.moveTo(-5*REVOLUTION);
  long loopCnt = 0;
  while(!stopPinActive() && stepper.distanceToGo() != 0) {
    stepper.run();
    loopCnt++;
    if(loopCnt == CYCLES_BEFORE_SOLENOID_LOW_POWER) {
      setSolenoidLowPowerMode();
    }
  }
  stepper.stop();
  stepper.runToPosition();
  disableSolenoid();
  stepper.disableOutputs();
  if(!stopPinActive()) {
    Serial.println("Home switch isn't activated");
  } else {
    Serial.println("Stopped via home switch");
  }
  setCurrentPositionAsHome();
  isHoming = false;
  Serial.println("Set home position");
}

void initializeBottomPosition() {
  EEPROM.get(BOTTOM_POSITION_ADDRESS, bottomPosition);
  if (bottomPosition < 0) {
    Serial.println("Bottom position not configured. Defaulting to one revolution");
    bottomPosition = 1 * REVOLUTION;
  } else {
    Serial.print("Pulled bottom position from EEPROM: ");
    Serial.println(bottomPosition);
  }
}

void storeCurrentPositionAsBottom() {
  EEPROM.put(BOTTOM_POSITION_ADDRESS, stepper.currentPosition());
  bottomPosition = stepper.currentPosition();
}

void disableSolenoid() {
  digitalWrite(SOLENOID, LOW);
}

void enableSolenoid() {
  digitalWrite(SOLENOID, HIGH);
}

void setSolenoidLowPowerMode() {
  analogWrite(SOLENOID, SOLENOID_PWM_LEVEL);
}

void setCurrentPositionAsHome() {
  Serial.println("Set current position as home");
  stepper.setCurrentPosition(0);
}

boolean atBottom() {
  return stepper.currentPosition() == bottomPosition;
}

boolean stopPinActive() {
  return digitalRead(STOP_PIN) == LOW; 
}

boolean atTop() {
  return stopPinActive() || (stepper.distanceToGo() == 0 && stepper.targetPosition() <= 0);
}

boolean moveBlindsUpButtonActive() {
  return digitalRead(UP_PIN) == LOW;
}

boolean moveBlindsDownButtonActive() {
  return digitalRead(DOWN_PIN) == LOW;
}

boolean isBrightOutside() {
  return digitalRead(LIGHT) == LOW;
}

boolean autoModeEnabled() {
  return digitalRead(AUTO_MODE) == LOW;
}

int getI2CAddress() {
  Serial.print("Fetching I2C address...found: 0x");
  int address = 0;
  if (digitalRead(I2C_4) == LOW)
    address += 1;
  if (digitalRead(I2C_3) == LOW)
    address += 2;
  if (digitalRead(I2C_2) == LOW)
    address += 4;
  if (digitalRead(I2C_1) == LOW)
    address += 8;
  if (digitalRead(I2C_0) == LOW)
    address += 16;
  Serial.println(address, HEX);
  return(address);
}