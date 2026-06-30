#include <Arduino.h>

// --- Expanded Array Matrix for Notes 6 through 12 ---
const int numUnoSolenoids = 7;

// Physical output pins mapped to Note IDs [6, 7, 8, 9, 10, 11, 12]
// Note 12 maps to Pin 2 (Standard digital pin with Software PWM)
const int solenoidPins[numUnoSolenoids] = {3, 5, 6, 9, 10, 11, 2};

// Track which index requires Software PWM (Pin 2 is at Index 6)
const int softwarePwmIndex = 6;

// Tracking Arrays for the non-blocking ballistic state machine
enum SolenoidState { IDLE, PULSING, HOLDING };
SolenoidState solenoidStates[numUnoSolenoids] = {IDLE};
unsigned long stateTimers[numUnoSolenoids] = {0};
bool strikeIsHard[numUnoSolenoids] = {true};

// Software PWM helper tracking variable for Pin 2 hold state
unsigned long lastSoftPwmToggleUs = 0;
bool softPwmPinState = false;

// Strike power levels
int POWER_HARD = 255;  // 100% full power slam
int POWER_HOLD = 60;   // ~23% low power sustain hold

void setup() {
  // Open Serial port at 115200 to match the ESP32 Master node
  Serial.begin(115200);
  
  for(int i = 0; i < numUnoSolenoids; i++) {
    pinMode(solenoidPins[i], OUTPUT);
    digitalWrite(solenoidPins[i], LOW); // Force off on boot
  }

  // Print initial greeting to the serial line on startup
  Serial.println("Hello Boss");
}

void handleSolenoidStateMachine() {
  unsigned long currentMillis = millis();
  unsigned long currentMicros = micros();

  // Handle Software PWM toggling for Pin 2 if it's currently in HOLDING state
  // Creates a fast ~1kHz background square wave duty cycle matching POWER_HOLD (60/255 -> ~23%)
  if (solenoidStates[softwarePwmIndex] == HOLDING) {
    // 1000 microseconds cycle period. 230us ON time, 770us OFF time.
    unsigned int dutyOnTimeUs = (1000 * POWER_HOLD) / 255;
    unsigned int targetInterval = softPwmPinState ? dutyOnTimeUs : (1000 - dutyOnTimeUs);
    if (currentMicros - lastSoftPwmToggleUs >= targetInterval) {
      lastSoftPwmToggleUs = currentMicros;
      softPwmPinState = !softPwmPinState;
      digitalWrite(solenoidPins[softwarePwmIndex], softPwmPinState ? HIGH : LOW);
    }
  }

  // Loop through all 7 solenoid state machine registers
  for (int i = 0; i < numUnoSolenoids; i++) {
    
    // 1. PULSE STATE: Control how long the 100% raw power is applied
    if (solenoidStates[i] == PULSING) {
      int softPulseLimit = 8;
      int currentLimit = strikeIsHard[i] ? 40 : softPulseLimit;
      
      if (currentMillis - stateTimers[i] >= (unsigned long)currentLimit) {
        stateTimers[i] = currentMillis;
        solenoidStates[i] = HOLDING;
        
        // Drop immediately to low-current hold state to sustain note cleanly
        if (i == softwarePwmIndex) {
          lastSoftPwmToggleUs = micros();
          softPwmPinState = true;
          digitalWrite(solenoidPins[i], HIGH);
        } else {
          analogWrite(solenoidPins[i], POWER_HOLD);
        }
      }
    }

    // 2. HOLD STATE: Maintain the key hold for STACCATO playback before releasing completely
    if (solenoidStates[i] == HOLDING && (currentMillis - stateTimers[i] >= 40)) {
      solenoidStates[i] = IDLE;
      if (i == softwarePwmIndex) {
        digitalWrite(solenoidPins[i], LOW); // Complete cutoff for Pin 2
      } else {
        analogWrite(solenoidPins[i], 0);    // Complete cutoff for Hardware PWM pins
      }
      
      // Provide a non-blocking confirmation signal back up to the web log
      Serial.println("Feedback: Solenoid Note " + String(i + 6) + " execution frame finished cleanly.");
    }
  }
}

void loop() {
  // Always update the state machine timers in the background
  handleSolenoidStateMachine();

  // Listen for incoming note commands from the ESP32 via TX0 -> HW-209 -> RX
  if (Serial.available() > 0) {
    String data = Serial.readStringUntil('\n');
    data.trim(); // Clean up trailing spaces or hidden characters
    
    // Expects CSV packet format: "N,NoteID,Velocity" (e.g. "N,6,255")
    if (data.startsWith("N,")) { // <--- TYPO FIXED HERE
      int firstComma = data.indexOf(',');
      int secondComma = data.indexOf(',', firstComma + 1);
      
      if (firstComma != -1 && secondComma != -1) {
        int noteID = data.substring(firstComma + 1, secondComma).toInt();
        int velocity = data.substring(secondComma + 1).toInt();
        
        // Map Note IDs 6 through 12 directly to our array bounds
        if (noteID >= 6 && noteID <= 12) {
          int index = noteID - 6; // Note 6 maps to index 0, Note 12 maps to index 6
          
          // Check if the targeted solenoid channel is currently resting idle
          if (solenoidStates[index] == IDLE) {
            stateTimers[index] = millis();
            solenoidStates[index] = PULSING;
            
            // Check if the ESP32 ordered a HARD or SOFT blow
            strikeIsHard[index] = (velocity >= 200);
            
            // Fire at 100% full raw power instantly to crack the inertia
            if (index == softwarePwmIndex) {
              digitalWrite(solenoidPins[index], HIGH); // Pin 2 slams full HIGH
            } else {
              analogWrite(solenoidPins[index], POWER_HARD); // Hardware PWM pins slam 255
            }
          }
        }
      }
    }
  }
}