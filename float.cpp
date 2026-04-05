const int AIN1 = 1;
const int AIN2 = 2;
const int ADC = 3;
int ADC_fullRetract = 500;     
int ADC_fullExtend = 3700;   

const int finalpos  = 500;
const int tolerance = 25;

bool state = true;

void stopMotor() {
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
}

void extendActuator() {
  digitalWrite(AIN1, HIGH);
  digitalWrite(AIN2, LOW);
}

void retractActuator() {
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, HIGH);
}

void move() {
  int raw = analogRead(ADC);
  Serial.println(raw);

  if (raw < ADC_fullRetract) raw = ADC_fullRetract;
  if (raw > ADC_fullExtend)  raw = ADC_fullExtend;
  int error = finalpos - raw;
  if (abs(error) <= tolerance) {
    stopMotor();
    state = false;
    return;
  }

  if (error > 0) {
    extendActuator();
  } else {
    retractActuator();
  }
}

void setup() {
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);

  analogReadResolution(12);

  Serial.begin(115200);
  delay(1500);
  Serial.println("Start");

  stopMotor();
}

void loop() {
  if (state) {
    move();
  } else {
    stopMotor();
  }

  delay(10);
}

