const int AIN1 = 1;
const int AIN2 = 2;
const int ADC = 3;
const int ADC_fullRetract = 1000;
const int ADC_fullExtend = 4095;
bool state = true;

void move() {

  int finalpos = 3000;
  int raw = analogRead(ADC);

  if (raw < finalpos) {
    digitalWrite(AIN1, HIGH);
    digitalWrite(AIN2, LOW);
  }

  if (raw > finalpos) {
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, HIGH);
  }

  if (raw == finalpos) {
    state = false;
  }
}


void setup() {
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  Serial.begin(115200);
  delay(1500); 
  Serial.println("Start");
}

void loop() {
  while (state == true) {
    move();
  }

  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
}
