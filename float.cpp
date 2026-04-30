#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>
#include "MS5837.h"

// ─── Pin definitions (from your schematic) ────────────────────
#define SDA_PIN   8
#define SCL_PIN   9
const int AIN1  = 1;
const int AIN2  = 2;
const int ADC   = 3;   // actuator position feedback

// ─── Actuator positions (tune to your float) ──────────────────
const int POS_SINK    = 500;    // fully retracted → negative buoyancy → sink
const int POS_NEUTRAL = 2100;   // middle → neutral buoyancy → hold depth
const int POS_FLOAT   = 3700;   // fully extended → positive buoyancy → rise
const int ADC_fullRetract = 500;
const int ADC_fullExtend  = 3700;
const int tolerance       = 25;

// ─── Depth targets (metres) ───────────────────────────────────
const float DEPTH_DEEP      = 2.5;   // target deep hold
const float DEPTH_SHALLOW   = 0.40;  // target shallow hold
const float DEPTH_TOLERANCE = 0.33;  // ±33 cm as per rules

// ─── Timing ───────────────────────────────────────────────────
const unsigned long HOLD_TIME     = 30000;  // 30 seconds hold
const unsigned long LOG_INTERVAL  = 5000;   // log every 5 seconds

// ─── Company info (edit before competition) ───────────────────
const String COMPANY_ID = "TEAM01";  // replace with your MATE number

// ─── BLE UUIDs ────────────────────────────────────────────────
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "abcd1234-ab12-cd34-ef56-abcdef123456"

// ─── BLE commands from laptop ─────────────────────────────────
// "PREDEPLOY"  → transmit one data packet (before dropping in water)
// "START"      → begin vertical profiles
// "TRANSMIT"   → send all logged data packets after recovery

// ─── Data logging ─────────────────────────────────────────────
struct DataPacket {
  unsigned long timeMs;   // milliseconds since float started
  float         depth;    // metres
  float         pressure; // mbar
};

const int MAX_PACKETS = 200;
DataPacket log_data[MAX_PACKETS];
int        packetCount = 0;
unsigned long missionStartMs = 0;

// ─── State machine ────────────────────────────────────────────
enum State {
  IDLE,
  DESCENDING_1,
  HOLDING_DEEP_1,
  ASCENDING_1,
  HOLDING_SHALLOW_1,
  DESCENDING_2,
  HOLDING_DEEP_2,
  ASCENDING_2,
  HOLDING_SHALLOW_2,
  PROFILES_DONE,
};

State currentState  = IDLE;
unsigned long holdStart    = 0;
unsigned long lastLogTime  = 0;
int           profilesDone = 0;

MS5837 sensor;

BLEServer*         pServer         = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool               deviceConnected = false;

// ─── Motor helpers ────────────────────────────────────────────
void stopMotor() {
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
  analogWrite(PWMA, 0);
}

void extendActuator() {       // positive buoyancy → rise
  digitalWrite(STBY, HIGH);
  digitalWrite(AIN1, HIGH);
  digitalWrite(AIN2, LOW);
  analogWrite(PWMA, 200);
}

void retractActuator() {      // negative buoyancy → sink
  digitalWrite(STBY, HIGH);
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, HIGH);
  analogWrite(PWMA, 200);
}

// Returns true when target reached
bool moveTo(int targetPos) {
  int raw = analogRead(ADC);
  if (raw < ADC_fullRetract) raw = ADC_fullRetract;
  if (raw > ADC_fullExtend)  raw = ADC_fullExtend;
  int error = targetPos - raw;
  if (abs(error) <= tolerance) {
    stopMotor();
    return true;
  }
  if (error > 0) extendActuator();
  else           retractActuator();
  return false;
}

// ─── Logging ──────────────────────────────────────────────────
void logPacket() {
  if (packetCount >= MAX_PACKETS) return;
  sensor.read();
  DataPacket p;
  p.timeMs   = millis() - missionStartMs;
  p.depth    = sensor.depth();
  p.pressure = sensor.pressure();
  log_data[packetCount++] = p;

  Serial.print("LOG #"); Serial.print(packetCount);
  Serial.print(" | t="); Serial.print(p.timeMs / 1000.0, 1); Serial.print("s");
  Serial.print(" | depth="); Serial.print(p.depth, 3); Serial.print("m");
  Serial.print(" | pressure="); Serial.print(p.pressure / 10.0, 3); Serial.println("kPa");
}

// ─── BLE send helper ──────────────────────────────────────────
void bleSend(String msg) {
  if (deviceConnected) {
    pCharacteristic->setValue(msg.c_str());
    pCharacteristic->notify();
    Serial.print("BLE >> "); Serial.println(msg);
  }
}

// ─── Pre-deploy packet (required before descending) ───────────
void sendPreDeployPacket() {
  sensor.read();
  unsigned long t = millis();
  // Format: TEAM01 0:00:00 9.8kPa 0.00m
  unsigned long totalSec = t / 1000;
  int hh = totalSec / 3600;
  int mm = (totalSec % 3600) / 60;
  int ss = totalSec % 60;

  char buf[80];
  snprintf(buf, sizeof(buf), "%s %02d:%02d:%02d %.2fkPa %.3fm",
    COMPANY_ID.c_str(), hh, mm, ss,
    sensor.pressure() / 10.0,
    sensor.depth());

  bleSend(String(buf));
  Serial.println("Pre-deploy packet sent.");
}

// ─── Transmit all logged data after recovery ──────────────────
void transmitAllData() {
  Serial.println("Transmitting all logged packets...");
  bleSend("=== DATA TRANSMISSION START ===");
  bleSend("COMPANY:" + COMPANY_ID);
  bleSend("PACKETS:" + String(packetCount));

  for (int i = 0; i < packetCount; i++) {
    DataPacket& p = log_data[i];

    unsigned long totalSec = p.timeMs / 1000;
    int hh = totalSec / 3600;
    int mm = (totalSec % 3600) / 60;
    int ss = totalSec % 60;

    char buf[80];
    snprintf(buf, sizeof(buf), "#%03d %02d:%02d:%02d %.2fkPa %.3fm",
      i + 1, hh, mm, ss,
      p.pressure / 10.0,
      p.depth);

    bleSend(String(buf));
    delay(50);  // small gap so BLE doesn't flood
  }

  bleSend("=== DATA TRANSMISSION END ===");
  Serial.println("Transmission complete.");
}

// ─── State transitions ────────────────────────────────────────
void enterState(State s) {
  currentState = s;
  switch (s) {
    case IDLE:
      stopMotor();
      Serial.println(">> IDLE");
      break;
    case DESCENDING_1:
      missionStartMs = millis();
      lastLogTime    = millis();
      Serial.println(">> DESCENDING profile 1 → 2.5m");
      break;
    case HOLDING_DEEP_1:
      moveTo(POS_NEUTRAL);   // was POS_RETRACTED
      if (depth < DEPTH_DEEP - DEPTH_TOLERANCE || depth > DEPTH_DEEP + DEPTH_TOLERANCE) {
        holdStart = now;
        Serial.println("Drifted out of deep range, resetting 30s timer.");
      }
      if (now - holdStart >= HOLD_TIME) {
        enterState(ASCENDING_1);
      }
      break;
    case ASCENDING_1:
      lastLogTime = millis();
      Serial.println(">> ASCENDING → 40cm (profile 1)");
      break;
    case HOLDING_SHALLOW_1:
      moveTo(POS_NEUTRAL);   // was POS_EXTENDED
      if (depth < DEPTH_SHALLOW - DEPTH_TOLERANCE || depth > DEPTH_SHALLOW + DEPTH_TOLERANCE) {
        holdStart = now;
        Serial.println("Drifted out of shallow range, resetting 30s timer.");
      }
      if (now - holdStart >= HOLD_TIME) {
        enterState(DESCENDING_2);
      }
      break;
    case DESCENDING_2:
      lastLogTime = millis();
      Serial.println(">> DESCENDING profile 2 → 2.5m");
      break;
    case HOLDING_DEEP_2:
      moveTo(POS_NEUTRAL);   // was POS_RETRACTED
      if (depth < DEPTH_DEEP - DEPTH_TOLERANCE || depth > DEPTH_DEEP + DEPTH_TOLERANCE) {
        holdStart = now;
        Serial.println("Drifted out of deep range, resetting 30s timer.");
      }
      if (now - holdStart >= HOLD_TIME) {
        enterState(ASCENDING_2);
      }
      break;
    case ASCENDING_2:
      lastLogTime = millis();
      Serial.println(">> ASCENDING → 40cm (profile 2)");
      break;
    case HOLDING_SHALLOW_2:
      moveTo(POS_NEUTRAL);   // was POS_EXTENDED
      if (depth < DEPTH_SHALLOW - DEPTH_TOLERANCE || depth > DEPTH_SHALLOW + DEPTH_TOLERANCE) {
        holdStart = now;
        Serial.println("Drifted out of shallow range, resetting 30s timer.");
      }
      if (now - holdStart >= HOLD_TIME) {
        enterState(PROFILES_DONE);
      }
      break;
    case PROFILES_DONE:
      stopMotor();
      bleSend("PROFILES COMPLETE. Send TRANSMIT to download data.");
      Serial.println(">> PROFILES DONE. Waiting for recovery.");
      break;
  }
}

// ─── BLE callbacks ────────────────────────────────────────────
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("BLE connected.");
  }
  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("BLE disconnected.");
    pServer->startAdvertising();
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    String cmd = pChar->getValue().c_str();
    cmd.trim();
    Serial.print("CMD: "); Serial.println(cmd);

    if (cmd == "PREDEPLOY") {
      // Send one data packet before dropping in water (5 points)
      sendPreDeployPacket();
    }
    else if (cmd == "START") {
      // Float is in the water, begin profiles
      if (currentState == IDLE) {
        packetCount = 0;
        enterState(DESCENDING_1);
      } else {
        bleSend("Already running.");
      }
    }
    else if (cmd == "TRANSMIT") {
      // Float recovered, send all logged data
      transmitAllData();
    }
    else {
      bleSend("Unknown command. Use: PREDEPLOY | START | TRANSMIT");
    }
  }
};

// ─── Setup ────────────────────────────────────────────────────
void setup() {
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(PWMA, OUTPUT);
  pinMode(STBY, OUTPUT);
  analogReadResolution(12);
  stopMotor();

  Serial.begin(115200);
  delay(1500);

  Wire.begin(SDA_PIN, SCL_PIN);
  while (!sensor.init()) {
    Serial.println("MS5837 init failed! Check wiring.");
    delay(5000);
  }
  sensor.setFluidDensity(1025);  // EGADS solution at World Championship

  BLEDevice::init("MATE-Float");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new CommandCallbacks());
  pService->start();
  pServer->startAdvertising();

  Serial.println("Ready. BLE name: MATE-Float");
  Serial.println("Commands: PREDEPLOY | START | TRANSMIT");
}

// ─── Loop ─────────────────────────────────────────────────────
void loop() {
  sensor.read();
  float depth = sensor.depth();
  unsigned long now = millis();

  // Periodic logging during active profiles
  bool activeProfile = (currentState != IDLE && currentState != PROFILES_DONE);
  if (activeProfile && (now - lastLogTime >= LOG_INTERVAL)) {
    logPacket();
    lastLogTime = now;
  }

  switch (currentState) {

    case IDLE:
      break;

    // ── Profile 1 ──────────────────────────────────────────────

    case DESCENDING_1:
      moveTo(POS_RETRACTED);
      if (depth >= DEPTH_DEEP - DEPTH_TOLERANCE) {
        enterState(HOLDING_DEEP_1);
      }
      break;

    case HOLDING_DEEP_1:
      // Keep actuator at retracted (neutral/negative) while holding
      moveTo(POS_RETRACTED);
      // If drifted out of range, reset hold timer
      if (depth < DEPTH_DEEP - DEPTH_TOLERANCE || depth > DEPTH_DEEP + DEPTH_TOLERANCE) {
        holdStart = now;
        Serial.println("Drifted out of deep range, resetting 30s timer.");
      }
      if (now - holdStart >= HOLD_TIME) {
        enterState(ASCENDING_1);
      }
      break;

    case ASCENDING_1:
      moveTo(POS_EXTENDED);
      if (depth <= DEPTH_SHALLOW + DEPTH_TOLERANCE && depth >= DEPTH_SHALLOW - DEPTH_TOLERANCE) {
        enterState(HOLDING_SHALLOW_1);
      }
      // Safety: stop extending if getting too close to surface
      if (depth < 0.05) {
        stopMotor();
        Serial.println("WARNING: Too close to surface!");
      }
      break;

    case HOLDING_SHALLOW_1:
      moveTo(POS_EXTENDED);
      // Reset hold timer if drifted out of range
      if (depth < DEPTH_SHALLOW - DEPTH_TOLERANCE || depth > DEPTH_SHALLOW + DEPTH_TOLERANCE) {
        holdStart = now;
        Serial.println("Drifted out of shallow range, resetting 30s timer.");
      }
      if (now - holdStart >= HOLD_TIME) {
        enterState(DESCENDING_2);
      }
      break;

    // ── Profile 2 ──────────────────────────────────────────────

    case DESCENDING_2:
      moveTo(POS_RETRACTED);
      if (depth >= DEPTH_DEEP - DEPTH_TOLERANCE) {
        enterState(HOLDING_DEEP_2);
      }
      break;

    case HOLDING_DEEP_2:
      moveTo(POS_RETRACTED);
      if (depth < DEPTH_DEEP - DEPTH_TOLERANCE || depth > DEPTH_DEEP + DEPTH_TOLERANCE) {
        holdStart = now;
        Serial.println("Drifted out of deep range, resetting 30s timer.");
      }
      if (now - holdStart >= HOLD_TIME) {
        enterState(ASCENDING_2);
      }
      break;

    case ASCENDING_2:
      moveTo(POS_EXTENDED);
      if (depth <= DEPTH_SHALLOW + DEPTH_TOLERANCE && depth >= DEPTH_SHALLOW - DEPTH_TOLERANCE) {
        enterState(HOLDING_SHALLOW_2);
      }
      if (depth < 0.05) {
        stopMotor();
        Serial.println("WARNING: Too close to surface!");
      }
      break;

    case HOLDING_SHALLOW_2:
      moveTo(POS_EXTENDED);
      if (depth < DEPTH_SHALLOW - DEPTH_TOLERANCE || depth > DEPTH_SHALLOW + DEPTH_TOLERANCE) {
        holdStart = now;
        Serial.println("Drifted out of shallow range, resetting 30s timer.");
      }
      if (now - holdStart >= HOLD_TIME) {
        enterState(PROFILES_DONE);
      }
      break;

    case PROFILES_DONE:
      stopMotor();
      break;
  }

  delay(10);
}
