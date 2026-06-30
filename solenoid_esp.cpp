#include <Arduino.h>
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <esp_now.h>
#include <ElegantOTA.h>
#include <esp_system.h>

// --- Pin Assignments ---
#define S1 13
#define S2 14
#define S3 16
#define S4 19 
#define S5 18

const int numLocalSolenoids = 5;
const int solenoidPins[numLocalSolenoids] = {S1, S2, S3, S4, S5};

// --- Solenoid State Machine ---
unsigned long strikeStartTime[numLocalSolenoids] = {0};
unsigned long holdStartTime[numLocalSolenoids]   = {0};
bool isStriking[numLocalSolenoids] = {false};
bool isHolding[numLocalSolenoids]  = {false};

int POWER_HOLD = 60;
int strikeTime = 40;
int holdTime   = 40; // <--- MASSIVE REDUCTION: Changed from 200ms to 40ms for fast staccato strikes

// --- WiFi ---
const char* hotspotSSID     = "POCOF3";
const char* hotspotPassword = "00000000";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Use a fixed char buffer instead of String for logText
#define LOG_BUF_SIZE 2048
char logBuf[LOG_BUF_SIZE];
int  logBufLen = 0;

void appendToLogBuf(const char* msg) {
  int msgLen = strlen(msg);
  int needed = msgLen + 1; // +1 for newline
  if (logBufLen + needed >= LOG_BUF_SIZE) {
    int half = LOG_BUF_SIZE / 2;
    memmove(logBuf, logBuf + half, logBufLen - half);
    logBufLen -= half;
    logBuf[logBufLen] = '\0';
  }
  memcpy(logBuf + logBufLen, msg, msgLen);
  logBufLen += msgLen;
  logBuf[logBufLen++] = '\n';
  logBuf[logBufLen]   = '\0';
}

// --- Safe pending log queue (for ESP-NOW / other ISR contexts) ---
#define PENDING_LOG_MAX 16
#define PENDING_MSG_LEN 120
char pendingLogs[PENDING_LOG_MAX][PENDING_MSG_LEN];
volatile int pendingHead = 0;
volatile int pendingTail = 0;

void enqueuePendingLog(const char* msg) {
  int next = (pendingTail + 1) % PENDING_LOG_MAX;
  if (next != pendingHead) {
    strncpy(pendingLogs[pendingTail], msg, PENDING_MSG_LEN - 1);
    pendingLogs[pendingTail][PENDING_MSG_LEN - 1] = '\0';
    pendingTail = next;
  }
}

void flushPendingLogs() {
  while (pendingHead != pendingTail) {
    const char* msg = pendingLogs[pendingHead];
    appendToLogBuf(msg);
    ws.textAll(msg);
    pendingHead = (pendingHead + 1) % PENDING_LOG_MAX;
  }
}

void addLog(const char* msg) {
  appendToLogBuf(msg);
  ws.textAll(msg);
}

void addLog(const String& msg) { addLog(msg.c_str()); }

struct EspNowMessage { char text[100]; };

// --- Forward declarations ---
void playNote(int noteID, int velocity);

// --- WebSocket event handler ---
void onWsEvent(AsyncWebSocket *srv, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {

  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] Client #%u connected  heap=%u\n",
                  client->id(), ESP.getFreeHeap());
    if (logBufLen > 0) {
      client->text(logBuf); 
    }

  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] Client #%u disconnected heap=%u\n",
                  client->id(), ESP.getFreeHeap());

  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len
        && info->opcode == WS_TEXT && len < 32) {
      data[len] = 0;
      
      // 1. Parse Solenoid "HIT,<id>,<vel>" commands
      if (strncmp((char*)data, "HIT,", 4) == 0) {
        int id = 0, vel = 0;
        sscanf((char*)data + 4, "%d,%d", &id, &vel);
        if (id > 0) playNote(id, vel);
      } 
      // 2. Parse Stepper "MOVE", "CFG", or "CMD" commands to forward via ESP-NOW
      else if (strncmp((char*)data, "MOVE,", 5) == 0 || strncmp((char*)data, "CFG,", 4) == 0 || strncmp((char*)data, "CMD,", 4) == 0) {
        EspNowMessage moveMsg;
        strncpy(moveMsg.text, (char*)data, sizeof(moveMsg.text) - 1);
        moveMsg.text[sizeof(moveMsg.text) - 1] = '\0';
        
        // Send movement target to the BROADCAST MAC ADDRESS
        uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        esp_now_send(broadcastMac, (uint8_t *) &moveMsg, sizeof(moveMsg));
        
        char buf[60];
        snprintf(buf, sizeof(buf), "[ESP-NOW TX] %s", moveMsg.text);
        addLog(buf);
      }
    }

  } else if (type == WS_EVT_ERROR) {
    Serial.printf("[WS] Client #%u error\n", client->id());
  }
}

// --- ESP-NOW callback ---
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  EspNowMessage msg;
  int copyLen = min(len, (int)sizeof(msg) - 1);
  memcpy(&msg, incomingData, copyLen);
  msg.text[copyLen] = '\0';
  
  // Forward everything including status flags right into the Web UI logs pipeline
  char buf[120];
  snprintf(buf, sizeof(buf), "[ESP-NOW] %s", msg.text);
  enqueuePendingLog(buf);
}

// --- Solenoid control ---
void triggerLocalSolenoidAsync(int index, int strikePower) {
  if (index >= 0 && index < numLocalSolenoids) {
    isStriking[index]      = true;
    isHolding[index]       = false;
    strikeStartTime[index] = millis();
    analogWrite(solenoidPins[index], strikePower);
  }
}

void updateSolenoidStateMachines() {
  unsigned long now = millis();
  for (int i = 0; i < numLocalSolenoids; i++) {
    if (isStriking[i] && (now - strikeStartTime[i] >= (unsigned long)strikeTime)) {
      isStriking[i]    = false;
      isHolding[i]     = true;
      holdStartTime[i] = now;
      analogWrite(solenoidPins[i], POWER_HOLD);
    }
    if (isHolding[i] && (now - holdStartTime[i] >= (unsigned long)holdTime)) {
      isHolding[i] = false;
      analogWrite(solenoidPins[i], 0);
    }
  }
}

// --- Note router ---
void playNote(int noteID, int velocity) {
  char buf[60];
  if (noteID >= 1 && noteID <= 5) {
    snprintf(buf, sizeof(buf), "[LOCAL] Sol%d PWR%d", noteID, velocity);
    addLog(buf);
    triggerLocalSolenoidAsync(noteID - 1, velocity);
  } else {
    Serial.printf("N,%d,%d\n", noteID, velocity);
    snprintf(buf, sizeof(buf), "[UNO] N,%d,%d", noteID, velocity);
    addLog(buf);
  }
}

// --- HTTP handlers ---
void handleHit(AsyncWebServerRequest *request) {
  if (request->method() == AsyncWebRequestMethod::HTTP_OPTIONS) {
    request->send(200, "text/plain", ""); return;
  }
  if (request->hasArg("id") && request->hasArg("vel")) {
    playNote(request->arg("id").toInt(), request->arg("vel").toInt());
    request->send(200, "text/plain", "OK");
  } else {
    request->send(400, "text/plain", "Missing Parameters");
  }
}

void handleRoot(AsyncWebServerRequest *request) {
  char buf[80];
  snprintf(buf, sizeof(buf), "{\"status\":\"ONLINE\",\"ip\":\"%s\",\"heap\":%u}",
           WiFi.localIP().toString().c_str(), ESP.getFreeHeap());
  request->send(200, "application/json", buf);
}

// --- Setup ---
void setup() {
  Serial.begin(115200);

  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("Boot reason: %d\n", (int)reason);

  for (int i = 0; i < numLocalSolenoids; i++) {
    pinMode(solenoidPins[i], OUTPUT);
    analogWrite(solenoidPins[i], 0);
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(hotspotSSID, hotspotPassword);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\nConnected! IP: %s  Heap: %u\n",
                WiFi.localIP().toString().c_str(), ESP.getFreeHeap());

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", AsyncWebRequestMethod::HTTP_GET, handleRoot);
  server.on("/hit",
    AsyncWebRequestMethod::HTTP_GET | AsyncWebRequestMethod::HTTP_POST | AsyncWebRequestMethod::HTTP_OPTIONS,
    handleHit);

  ElegantOTA.begin(&server);
  server.begin();
  Serial.printf("Server started. Heap: %u\n", ESP.getFreeHeap());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
  } else {
    // Register the receive callback
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    
    // Register the Broadcast Peer to send MOVE commands to any Stepper ESP32 listening
    esp_now_peer_info_t broadcastInfo = {};
    memcpy(broadcastInfo.peer_addr, "\xFF\xFF\xFF\xFF\xFF\xFF", 6);
    broadcastInfo.channel = 0;  // Auto-channel matching
    broadcastInfo.encrypt = false;
    
    if (esp_now_add_peer(&broadcastInfo) != ESP_OK) {
      Serial.println("Failed to add broadcast peer");
    }
    
    Serial.println("ESP-NOW Active & Broadcast Peer Added");
  }

  Serial.println("ESP32 Ready");
}

unsigned long lastWsCleanup   = 0;
unsigned long lastHeapPrint   = 0;

void loop() {
  ElegantOTA.loop();
  updateSolenoidStateMachines();
  flushPendingLogs();

  unsigned long now = millis();

  if (now - lastWsCleanup >= 5000) {
    ws.cleanupClients(4);
    lastWsCleanup = now;
  }

  if (now - lastHeapPrint >= 10000) {
    Serial.printf("[HEAP] Free: %u  MinFree: %u\n",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap());
    lastHeapPrint = now;
  }

  while (Serial.available()) {
    String msg = Serial.readStringUntil('\n');
    msg.trim();
    if (msg.length() > 0) addLog(("[UNO] " + msg).c_str());
  }

  yield();
}