#include <IRremote.hpp>

// 1 - 0xBA45FF00
// 2 - 0xB946FF00
// 3 - 0xB847FF00

#define IR_RECEIVE_PIN 7

void setup() {
  Serial.begin(9600);
  IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);  // <-- change this
  Serial.println("Ready — point remote and press a button");
}

void loop() {
  if (IrReceiver.decode()) {
    Serial.print("Protocol: ");
    Serial.println(IrReceiver.decodedIRData.protocol);
    Serial.print("Code (HEX): 0x");
    Serial.println(IrReceiver.decodedIRData.decodedRawData, HEX);
    IrReceiver.resume();
  }
}
