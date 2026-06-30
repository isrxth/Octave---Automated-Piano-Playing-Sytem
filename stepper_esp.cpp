// --- Firmware for Team Obsidian Robotic Piano ---
// Module: Stepper Controller ESP32 (Slave)

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <AccelStepper.h>
#include <ElegantOTA.h>
#include <WebServer.h>

const char* hotspotSSID     = "POCOF3";
const char* hotspotPassword = "00000000";

WebServer server(80);

AccelStepper stepper(1, 18, 19);
AccelStepper stepper2(1, 21, 22);

#define LIMIT_M1_LEFT  25
#define LIMIT_M1_RIGHT 26
#define LIMIT_M2_LEFT  32
#define LIMIT_M2_RIGHT 33

// --- CONFIGURATION FOR SNAPPY MOVEMENT ---
int  runSpeedVal   = 3500;  
int  accelVal      = 3000; // Controlled snappy acceleration profile

volatile long targetM1 = 0;
volatile long targetM2 = 0;
volatile bool newCmdM1 = false;
volatile bool newCmdM2 = false;

// Hardware Flags
bool m1WasMoving = false;
bool m2WasMoving = false;
volatile bool triggerZeroCmd = false; // Safe flag to trigger quick-zeroing outside of ISR

struct Message { char text[100]; };
uint8_t receiverMac[] = {0xB4, 0x8A, 0x0A, 0xB2, 0xAA, 0xBC};
Message msg;

// Function to send target confirmation status messages back to the Master Solenoid ESP32
void sendFeedback(const char* logMsg) {
  Message reply;
  strncpy(reply.text, logMsg, sizeof(reply.text) - 1);
  reply.text[sizeof(reply.text) - 1] = '\0';
  esp_now_send(receiverMac, (uint8_t *) &reply, sizeof(reply));
}

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  Message incomingMsg;
  memcpy(&incomingMsg, incomingData, min(len, (int)sizeof(incomingMsg)));
  incomingMsg.text[sizeof(incomingMsg.text) - 1] = '\0';
  
  if (strncmp(incomingMsg.text, "MOVE,", 5) == 0) {
    char motorDigit = incomingMsg.text[6];
    long targetStep = atol(incomingMsg.text + 8);
    if (motorDigit == '1') { targetM1 = targetStep; newCmdM1 = true; }
    else if (motorDigit == '2') { targetM2 = targetStep; newCmdM2 = true; }
  }
  else if (strncmp(incomingMsg.text, "CFG,SPD,", 8) == 0) {
    runSpeedVal = atoi(incomingMsg.text + 8);
    stepper.setMaxSpeed(runSpeedVal);
    stepper2.setMaxSpeed(runSpeedVal);
    Serial.printf("[CFG] Speed updated to: %d\n", runSpeedVal);
  }
  else if (strncmp(incomingMsg.text, "CFG,ACC,", 8) == 0) {
    accelVal = atoi(incomingMsg.text + 8);
    stepper.setAcceleration(accelVal);
    stepper2.setAcceleration(accelVal);
    Serial.printf("[CFG] Accel updated to: %d\n", accelVal);
  }
  // --- NEW: QUICK-ZERO TRIGGER ---
  else if (strncmp(incomingMsg.text, "CMD,ZERO", 8) == 0) {
    triggerZeroCmd = true;
  }
}

// Initial full calibration run on boot
void home() {
  Serial.println("Initial Boot Homing Calibration...");

  stepper.setMaxSpeed(1000);
  stepper2.setMaxSpeed(1000);
  stepper.setAcceleration(1000);
  stepper2.setAcceleration(1000);

  stepper.setSpeed(-500);
  stepper2.setSpeed(-500);

  bool m1Homed = false, m2Homed = false;
  while (!m1Homed || !m2Homed) {
    if (!m1Homed) {
      if (digitalRead(LIMIT_M1_LEFT) == LOW) {
        stepper.setSpeed(0);
        stepper.setCurrentPosition(0); 
        m1Homed = true;
      } else { stepper.runSpeed(); }
    }
    if (!m2Homed) {
      if (digitalRead(LIMIT_M2_LEFT) == LOW) {
        stepper2.setSpeed(0);
        stepper2.setCurrentPosition(0); 
        m2Homed = true;
      } else { stepper2.runSpeed(); }
    }
  }

  stepper.setSpeed(500);
  stepper2.setSpeed(500);
  bool m1MaxFound = false, m2MaxFound = false;
  long trackLengthM1 = 0, trackLengthM2 = 0;

  while (!m1MaxFound || !m2MaxFound) {
    if (!m1MaxFound) {
      if (digitalRead(LIMIT_M1_RIGHT) == LOW) {
        stepper.setSpeed(0);
        trackLengthM1 = stepper.currentPosition();
        m1MaxFound = true;
      } else { stepper.runSpeed(); }
    }
    if (!m2MaxFound) {
      if (digitalRead(LIMIT_M2_RIGHT) == LOW) {
        stepper2.setSpeed(0);
        trackLengthM2 = stepper2.currentPosition();
        m2MaxFound = true;
      } else { stepper2.runSpeed(); }
    }
  }

  stepper.setMaxSpeed(runSpeedVal);
  stepper.setAcceleration(accelVal);
  stepper2.setMaxSpeed(runSpeedVal);
  stepper2.setAcceleration(accelVal);

  stepper.moveTo(0);
  stepper2.moveTo(0);
  
  while (stepper.distanceToGo() != 0 || stepper2.distanceToGo() != 0) {
    stepper.run(); 
    stepper2.run();
  }
  Serial.println("Initial Boot Homing Complete. System initialized at absolute zero.");
}

// --- NEW: FAST ZEROING SEQUENCE ---
// Only moves left to re-sync steps, entirely bypassing the max track length measurement
void quickZero() {
  Serial.println("Quick Zeroing: Re-syncing carriages to Left Limits...");

  stepper.setMaxSpeed(1000);
  stepper2.setMaxSpeed(1000);
  stepper.setSpeed(-500);
  stepper2.setSpeed(-500);

  bool m1Zeroed = false, m2Zeroed = false;
  while (!m1Zeroed || !m2Zeroed) {
    if (!m1Zeroed) {
      if (digitalRead(LIMIT_M1_LEFT) == LOW) {
        stepper.setSpeed(0);
        stepper.setCurrentPosition(0); 
        m1Zeroed = true;
      } else { stepper.runSpeed(); }
    }
    if (!m2Zeroed) {
      if (digitalRead(LIMIT_M2_LEFT) == LOW) {
        stepper2.setSpeed(0);
        stepper2.setCurrentPosition(0); 
        m2Zeroed = true;
      } else { stepper2.runSpeed(); }
    }
  }

  // Restore high-performance variables
  stepper.setMaxSpeed(runSpeedVal);
  stepper.setAcceleration(accelVal);
  stepper2.setMaxSpeed(runSpeedVal);
  stepper2.setAcceleration(accelVal);
  
  Serial.println("Quick Zero Complete. System perfectly synchronized at step 0.");
}

bool otaModeActive = false;

void setup() {
  Serial.begin(115200);
  pinMode(LIMIT_M1_LEFT,  INPUT_PULLUP);
  pinMode(LIMIT_M1_RIGHT, INPUT_PULLUP);
  pinMode(LIMIT_M2_LEFT,  INPUT_PULLUP);
  pinMode(LIMIT_M2_RIGHT, INPUT_PULLUP);

  if (digitalRead(LIMIT_M1_LEFT) == LOW || digitalRead(LIMIT_M2_LEFT) == LOW) {
    otaModeActive = true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(hotspotSSID, hotspotPassword);

  if (otaModeActive) {
    while (WiFi.status() != WL_CONNECTED) delay(500);
    server.on("/", []() { server.send(200, "text/plain", "OTA Mode"); });
    server.begin();
    ElegantOTA.begin(&server);
  } else {
    while (WiFi.status() != WL_CONNECTED) delay(500);
    home();
    if (esp_now_init() == ESP_OK) {
      esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
      esp_now_peer_info_t peerInfo = {};
      memcpy(peerInfo.peer_addr, receiverMac, 6);
      peerInfo.channel = 0;
      peerInfo.encrypt = false;
      esp_now_add_peer(&peerInfo);
    }
  }
}

void loop() {
  if (otaModeActive) {
    server.handleClient();
    ElegantOTA.loop();
    return;
  }

  // --- Safe Execution of Quick-Zero outside of ISR ---
  if (triggerZeroCmd) {
    triggerZeroCmd = false; // Reset flag
    Serial.println("[CMD] Auto Quick-Zero Initiated by Master UI...");
    quickZero();
    sendFeedback("ZERO_COMPLETE");
    return; // Skip normal loop execution for this cycle
  }

  // Read physical states of all end-stops
  bool m1LeftPressed  = (digitalRead(LIMIT_M1_LEFT) == LOW);
  bool m1RightPressed = (digitalRead(LIMIT_M1_RIGHT) == LOW);
  bool m2LeftPressed  = (digitalRead(LIMIT_M2_LEFT) == LOW);
  bool m2RightPressed = (digitalRead(LIMIT_M2_RIGHT) == LOW);

  // --- 1. FIXED DEBOUNCED SAFETY INTERRUPTS & REBOUNDS ---
  
  // Motor 1 Left Limit
  if (m1LeftPressed && stepper.distanceToGo() < 0) {
    delayMicroseconds(500); // Filter electrical interference noise
    if (digitalRead(LIMIT_M1_LEFT) == LOW) {
      stepper.stop();
      stepper.setCurrentPosition(0);
      stepper.moveTo(15); 
    }
  }
  // Motor 1 Right Limit
  if (m1RightPressed && stepper.distanceToGo() > 0) {
    delayMicroseconds(500);
    if (digitalRead(LIMIT_M1_RIGHT) == LOW) {
      stepper.stop();
      stepper.setCurrentPosition(stepper.currentPosition());
      stepper.moveTo(stepper.currentPosition() - 15); // Rebound left
    }
  }
  
  // Motor 2 Left Limit
  if (m2LeftPressed && stepper2.distanceToGo() < 0) {
    delayMicroseconds(500);
    if (digitalRead(LIMIT_M2_LEFT) == LOW) {
      stepper2.stop();
      stepper2.setCurrentPosition(0);
      stepper2.moveTo(15); // Rebound right
    }
  }
  // Motor 2 Right Limit
  if (m2RightPressed && stepper2.distanceToGo() > 0) {
    delayMicroseconds(500);
    if (digitalRead(LIMIT_M2_RIGHT) == LOW) {
      stepper2.stop();
      stepper2.setCurrentPosition(stepper2.currentPosition());
      stepper2.moveTo(stepper2.currentPosition() - 15); // Rebound left
    }
  }

  // --- 2. INBOUND AIR PACKETS HANDLING ---
  if (newCmdM1) {
    newCmdM1 = false;
    if (m1LeftPressed && targetM1 < stepper.currentPosition()) {
      Serial.println("[SAFETY] Command blocked: Motor 1 at Left boundary.");
      sendFeedback("AT_TARGET"); 
    } 
    else if (m1RightPressed && targetM1 > stepper.currentPosition()) {
      Serial.println("[SAFETY] Command blocked: Motor 1 at Right boundary.");
      sendFeedback("AT_TARGET"); 
    } 
    else {
      stepper.moveTo(targetM1);
      m1WasMoving = true;
    }
  }

  if (newCmdM2) {
    newCmdM2 = false;
    if (m2LeftPressed && targetM2 < stepper2.currentPosition()) {
      Serial.println("[SAFETY] Command blocked: Motor 2 at Left boundary.");
      sendFeedback("AT_TARGET"); 
    } 
    else if (m2RightPressed && targetM2 > stepper2.currentPosition()) {
      Serial.println("[SAFETY] Command blocked: Motor 2 at Right boundary.");
      sendFeedback("AT_TARGET"); 
    } 
    else {
      stepper2.moveTo(targetM2);
      m2WasMoving = true;
    }
  }

  // Smooth continuous execution via AccelStepper
  stepper.run();
  stepper2.run();

  // --- 3. HARDWARE REAL-TIME TARGET DETECTOR ---
  if (m1WasMoving && stepper.distanceToGo() == 0) {
    m1WasMoving = false;
    sendFeedback("AT_TARGET");
  }
  if (m2WasMoving && stepper2.distanceToGo() == 0) {
    m2WasMoving = false;
    sendFeedback("AT_TARGET");
  }
}