#include <IRremote.hpp>

// 1 - 3125149440
// 2 - 3108437760

#define IR_RECEIVE_PIN 7
#define LED 6

const uint32_t ON = 3125149440;
const uint32_t OFF = 3108437760;

void setup() {
  Serial.begin(9600);
  pinMode(LED, OUTPUT);
  IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);  // <-- change this
  digitalWrite(LED, LOW);
}

void loop() {
  if (IrReceiver.decode()) {
    uint32_t code = IrReceiver.decodedIRData.decodedRawData;
    Serial.println(code);

    if (code == ON) {
      digitalWrite(LED, HIGH);
    }

    else if (code == OFF) {
      digitalWrite(LED, LOW);
    }
    IrReceiver.resume();
  }
}
