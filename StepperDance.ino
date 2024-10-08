#include <Servo.h>

// Pin Definitions
#define EncoderPinA 2        
#define EncoderPinB 3        
#define ButtonPin 4          
#define dirPin 5             // Digital pin for Y stepper motor direction control
#define stepPin 6            // Digital pin for Y stepper motor step control
#define Z_limitSwitchPin 7   // Digital pin for Z limit switch input
#define limitSwitchPin 8     // Digital pin for Y limit switch input
#define ServoPin 9 
#define Z_dirPin 10          // Digital pin for Z stepper motor direction control
#define Z_stepPin 11         // Digital pin for Z stepper motor step control          

// Constants
const unsigned long BAUD_RATE = 115200;
const int Y_AXIS_STEPS = 34;  // Steps for 6.8mm movement for color cartridge
const int Y_AXIS_STEPS_M = 72;  // Steps for 14.4mm movement for monochrome cartridge
const float MM_PER_STEP = 0.2;  // Millimeters per step
const unsigned long DEBOUNCE_DELAY = 300;  // Milliseconds
const unsigned long SMOOTH_WINDOW = 140;  // Milliseconds
const int STEPPER_WRITE_DELAY = 1000;  // Microseconds between steps (lower = faster)
const int SERVO_RANGE = 180;
const int BACKOFF_STEPS = 450;  // Steps to accommodate reduced build area
const int Z_BACKOFF_STEPS = 50; // Get off the switch a little
const unsigned long HOMING_DIRECTION_CHANGE_DELAY = 50;  // Milliseconds
const int SMALL_ROLL_INS_THRESHOLD = 4;  // New constant for small roll-ins
volatile int clicksSinceEdge = 0;
const int clicksPerPaper = 350;


// Enum for printer modes
enum PrinterMode {
  IDLE,
  COLOR,
  MONOCHROME
};

Servo SwitchServo;
boolean SEEKING_PAPER_FEED = false;
int ButtonState = 0;
boolean StateA;
volatile boolean StateB;
volatile boolean RotationDetected = false;
volatile int EncoderClicks = 0;
unsigned long ThisClick = 0;
unsigned long ClickInterval = 0;
volatile long currentYPosition = 0;
volatile long currentZPosition = 0;
volatile boolean EncoderClicksDecreased = false; // Track direction of paper feed mechanism to determine new page roll-in.
boolean IS_MOVING = false;
boolean MOVE_SENT = false;
PrinterMode currentMode = IDLE;

void moveYAxis(int steps, unsigned long delay = STEPPER_WRITE_DELAY);

void setup() {
  SwitchServo.attach(ServoPin);  
  Serial.begin(BAUD_RATE);
  pinMode(EncoderPinA, INPUT_PULLUP);
  pinMode(EncoderPinB, INPUT_PULLUP);
  pinMode(ButtonPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(EncoderPinA), EncoderRotated, CHANGE);

  // Setup Y-axis stepper motor and limit switch
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  pinMode(limitSwitchPin, INPUT_PULLUP);

  // Setup Z-axis stepper motor and limit switch
  pinMode(Z_stepPin, OUTPUT);
  pinMode(Z_dirPin, OUTPUT);
  pinMode(Z_limitSwitchPin, INPUT_PULLUP);

  closePaperFeedSensor();

  Serial.println("Printer initialized. Send 'c' for color mode, 'm' for monochrome mode, 'x' for idle.");
}

void loop() {
  ButtonState = digitalRead(ButtonPin);
 
  if (ButtonState == HIGH) {
    SEEKING_PAPER_FEED = !SEEKING_PAPER_FEED;
    if (SEEKING_PAPER_FEED) {
      Serial.println("Button: Awaiting new print job, homing Y-axis...");
      homeYAxis();
      Serial.println("Button: Y-axis homing completed for new print job.");
    } else {
      Serial.println("Button: Not awaiting new print job, Y-axis not homed.");
    }
    delay(DEBOUNCE_DELAY);
  }

  if (Serial.available() > 0) {
    char input = Serial.read();
    handleSerialInput(input);
  }

  if (RotationDetected) {
    noInterrupts();
    StateA = digitalRead(EncoderPinA);
    if (StateA != StateB) {
      EncoderClicks++;  
      clicksSinceEdge++;
      EncoderClicksDecreased = false;
    } else {
      EncoderClicks--;  
      clicksSinceEdge--;
      EncoderClicksDecreased = true;
    }
    RotationDetected = false;
    interrupts();
   
    IS_MOVING = true;
    ThisClick = millis();
   
    Serial.print("EncoderClicks: ");
    Serial.println(EncoderClicks);
    Serial.print("clicksSinceEdge: ");
    Serial.println(clicksSinceEdge);
    if (abs(EncoderClicks) >= SMALL_ROLL_INS_THRESHOLD) {
      EncoderClicks = abs(EncoderClicks);
      if (SEEKING_PAPER_FEED && IS_MOVING) { //Removed unnecessary SEEKING_Y_INCREMENT logic, improves readability
        SwitchServo.write(SERVO_RANGE);
        Serial.println("servo opened");
        clicksSinceEdge = 0;
        MOVE_SENT = true; 
        SEEKING_PAPER_FEED = false;
      } else if (IS_MOVING && !MOVE_SENT) {
        if(currentMode == COLOR){
          moveYAxis(Y_AXIS_STEPS);
          Serial.print("Moved Y-axis ");
          Serial.print(Y_AXIS_STEPS * MM_PER_STEP);
          Serial.println("mm");
        }
        if(currentMode == MONOCHROME){
          moveYAxis(Y_AXIS_STEPS_M, STEPPER_WRITE_DELAY/1.4);
          Serial.print("Moved Y-axis ");
          Serial.print(Y_AXIS_STEPS_M * MM_PER_STEP);
          Serial.println("mm");
        }
        MOVE_SENT = true;
      }
    }
  }

  ClickInterval = millis() - ThisClick;
 
  if (IS_MOVING) {
    if (ClickInterval >= SMOOTH_WINDOW) {
      IS_MOVING = false;
      MOVE_SENT = false;
    }
  }

  if(clicksSinceEdge >= clicksPerPaper){
    closePaperFeedSensor();
    //if(!IS_MOVING || EncoderClicksDecreased){
    EncoderClicks = 0;  // Reset EncoderClicks at the start of a new print job
    clicksSinceEdge = 0;
    Serial.println("Initializing for new page");
    SEEKING_PAPER_FEED = true;
    Serial.println("Awaiting new page, resetting Y-axis...");
    moveYAxis(-currentYPosition);
    Serial.println("Y-axis reset for new page.");
    //}
  }
}

void handleSerialInput(char input) {
  switch (input) {
    case 'c':
      currentMode = COLOR;
      EncoderClicks = 0;  // Reset EncoderClicks at the start of a new print job
      clicksSinceEdge = 0;
      Serial.println("Initializing color mode");
      closePaperFeedSensor();
      SEEKING_PAPER_FEED = true;
      Serial.println("Awaiting new print job, homing Y-axis...");
      homeYAxis();
      Serial.println("Y-axis homing completed for new print job.");
      Serial.println("Homing Z-axis...");
      homeZAxis();
      Serial.println("Z-axis homing completed for new print job.");
      break;
    case 'm':
      currentMode = MONOCHROME;
      EncoderClicks = 0;  // Reset EncoderClicks at the start of a new print job
      clicksSinceEdge = 0;
      Serial.println("Initializing monochrome mode");
      closePaperFeedSensor();
      SEEKING_PAPER_FEED = true;
      Serial.println("Awaiting new print job, homing Y-axis...");
      homeYAxis();
      Serial.println("Y-axis homing completed for new print job.");
      Serial.println("Homing Z-axis...");
      homeZAxis();
      Serial.println("Z-axis homing completed for new print job.");
      break;
    case 'x':
      currentMode = IDLE;
      closePaperFeedSensor();
      SEEKING_PAPER_FEED = false;
      Serial.println("Printer is now idle.");
      break;
    case '\n':  // Newline character
    case '\r':  // Carriage return
      // Ignore these characters
      break;
    default:
      Serial.println("Invalid input. Use 'c' for color, 'm' for monochrome, or 'x' for idle.");
  }
}

void closePaperFeedSensor() {
  SwitchServo.write(0);  // Assuming 0 is the closed position
  Serial.println("Paper feed sensor closed");
}

void EncoderRotated() {
  StateB = digitalRead(EncoderPinB);
  RotationDetected = true;
}

void homeYAxis() {
  Serial.println("Starting Y-axis homing...");
  
  digitalWrite(dirPin, HIGH);  // Set direction for negative steps
  // Move towards home until the limit switch is triggered
  while (digitalRead(limitSwitchPin) == LOW) {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(STEPPER_WRITE_DELAY/2);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(STEPPER_WRITE_DELAY/2);
  }
  
  Serial.println("Limit switch triggered!");
  
  delay(HOMING_DIRECTION_CHANGE_DELAY);  // Short delay before changing direction
  
  moveYAxis(BACKOFF_STEPS);  // Move back by BACKOFF_STEPS
  
  currentYPosition = 0;  // Reset Y position to zero after homing
  Serial.println("Homing complete!");
}

void moveYAxis(int steps, unsigned long delay = STEPPER_WRITE_DELAY) {
  if (steps > 0) {
    digitalWrite(dirPin, LOW);  // Set direction for positive steps
    for (int i = 0; i < steps; i++) {
      digitalWrite(stepPin, HIGH);
      delayMicroseconds(delay);
      digitalWrite(stepPin, LOW);
      delayMicroseconds(delay);
    }
    currentYPosition += steps;  // Update current Y position
  } else if (steps < 0) {
    digitalWrite(dirPin, HIGH);  // Set direction for negative steps
    steps = -steps;  // Convert negative steps to positive value
    for (int i = 0; i < steps; i++) {
      digitalWrite(stepPin, HIGH);
      delayMicroseconds(delay);
      digitalWrite(stepPin, LOW);
      delayMicroseconds(delay);
    }
    currentYPosition -= steps;  // Update current Y position
  } else {
    return;  // If steps is zero, no movement is needed.
  }
  Serial.print("currentYPosition: ");
  Serial.println(currentYPosition);
}

void homeZAxis() {
  Serial.println("Starting Z-axis homing...");
  digitalWrite(Z_dirPin, LOW);  // Set direction for negative steps
  // Move towards home until the limit switch is triggered
  while (digitalRead(Z_limitSwitchPin) == LOW) {
    digitalWrite(Z_stepPin, HIGH);
    delayMicroseconds(STEPPER_WRITE_DELAY);
    digitalWrite(Z_stepPin, LOW);
    delayMicroseconds(STEPPER_WRITE_DELAY);
  }
  
  Serial.println("Z-limit switch triggered!");
  
  delay(HOMING_DIRECTION_CHANGE_DELAY);  // Short delay before changing direction
  
  moveZAxis(Z_BACKOFF_STEPS);  // Move up by Z_BACKOFF_STEPS
  
  currentZPosition = 0;  // Reset Z position to zero after homing
  Serial.println("Z-homing complete!");
}

void moveZAxis(int steps) {
  if (steps > 0) {
    digitalWrite(Z_dirPin, HIGH);  // Set direction for positive steps
    for (int i = 0; i < steps; i++) {
      digitalWrite(Z_stepPin, HIGH);
      delayMicroseconds(STEPPER_WRITE_DELAY);
      digitalWrite(Z_stepPin, LOW);
      delayMicroseconds(STEPPER_WRITE_DELAY);
    }
    currentZPosition += steps;  // Update current Z position
  } else if (steps < 0) {
    digitalWrite(Z_dirPin, LOW);  // Set direction for negative steps
    steps = -steps;  // Convert negative steps to positive value
    for (int i = 0; i < steps; i++) {
      digitalWrite(Z_stepPin, HIGH);
      delayMicroseconds(STEPPER_WRITE_DELAY);
      digitalWrite(Z_stepPin, LOW);
      delayMicroseconds(STEPPER_WRITE_DELAY);
    }
    currentZPosition -= steps;  // Update current Z position
  } else {
    return;  // If steps is zero, no movement is needed.
  }
  Serial.print("currentZPosition: ");
  Serial.println(currentZPosition);
}