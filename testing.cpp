#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ─── Pins ─────────────────────────────────────────────────────
const int AIN1 = 1;
const int AIN2 = 2;
const int ADC  = 3;

const int ADC_fullRetract = 500;
const int ADC_fullExtend  = 3700;
const int tolerance       = 25;

// ─── Positions ────────────────────────────────────────────────
const int POS_SINK    = 500;
const int POS_NEUTRAL = 2100;
const int POS_FLOAT   = 3700;

// ─── BLE ──────────────────────────────────────────────────────
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "abcd1234-ab12-cd34-ef56-abcdef123456"

BLEServer*         pServer         = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool               deviceConnected = false;

int  targetPos = POS_NEUTRAL;
bool moving    = false;

// ─── Helpers ──────────────────────────────────────────────────
void bleSend(String msg) {
  if (deviceConnected) {
    pCharacteristic->setValue(msg.c_str());
    pCharacteristic->notify();
  }
  Serial.println(msg);
}

void stopMotor() {
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
  moving = false;
}

void extendActuator() {
  digitalWrite(AIN1, HIGH);
  digitalWrite(AIN2, LOW);
}

void retractActuator() {
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, HIGH);
}

bool moveTo(int target) {
  int raw = analogRead(ADC);
  raw = constrain(raw, ADC_fullRetract, ADC_fullExtend);
  int error = target - raw;

  if (abs(error) <= tolerance) {
    stopMotor();
    return true;
  }

  if (error > 0) extendActuator();
  else           retractActuator();
  return false;
}

// ─── BLE callbacks ────────────────────────────────────────────
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    bleSend("Connected! Commands: EXTEND | RETRACT | NEUTRAL | STOP | POS | SPEED:<0-255>");
  }
  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    stopMotor();
    Serial.println("Disconnected.");
    pServer->startAdvertising();
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    String cmd = pChar->getValue().c_str();
    cmd.trim();
    cmd.toUpperCase();
    Serial.print("CMD: "); Serial.println(cmd);

    if (cmd == "EXTEND") {
      targetPos = POS_FLOAT;
      moving    = true;
      bleSend("Moving to EXTEND (POS_FLOAT = " + String(POS_FLOAT) + ")");
    }
    else if (cmd == "RETRACT") {
      targetPos = POS_SINK;
      moving    = true;
      bleSend("Moving to RETRACT (POS_SINK = " + String(POS_SINK) + ")");
    }
    else if (cmd == "NEUTRAL") {
      targetPos = POS_NEUTRAL;
      moving    = true;
      bleSend("Moving to NEUTRAL (POS_NEUTRAL = " + String(POS_NEUTRAL) + ")");
    }
    else if (cmd == "STOP") {
      stopMotor();
      bleSend("Stopped. ADC = " + String(analogRead(ADC)));
    }
    else if (cmd == "POS") {
      int raw = analogRead(ADC);
      bleSend("ADC = " + String(raw) +
              " | SINK=" + String(POS_SINK) +
              " NEUTRAL=" + String(POS_NEUTRAL) +
              " FLOAT=" + String(POS_FLOAT));
    }
    // Custom position: send "GOTO:2500" to move to ADC value 2500
    else if (cmd.startsWith("GOTO:")) {
      int val = cmd.substring(5).toInt();
      val = constrain(val, ADC_fullRetract, ADC_fullExtend);
      targetPos = val;
      moving    = true;
      bleSend("Moving to custom position: " + String(val));
    }
    // Adjust speed: send "SPEED:150"
    else if (cmd.startsWith("SPEED:")) {
      int spd = cmd.substring(6).toInt();
      spd = constrain(spd, 0, 255);
      bleSend("Speed set to: " + String(spd));
    }
    else {
      bleSend("Unknown command. Use: EXTEND | RETRACT | NEUTRAL | STOP | POS | GOTO:<val> | SPEED:<0-255>");
    }
  }
};

// ─── Setup ────────────────────────────────────────────────────
void setup() {
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  analogReadResolution(12);
  stopMotor();

  Serial.begin(115200);
  delay(1500);

  BLEDevice::init("Float-Test");
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

  Serial.println("BLE ready. Device name: Float-Test");
}

// ─── Loop ─────────────────────────────────────────────────────
void loop() {
  if (moving) {
    bool arrived = moveTo(targetPos);
    if (arrived) {
      bleSend("Reached position. ADC = " + String(analogRead(ADC)));
    }
  }

  delay(10);
}
