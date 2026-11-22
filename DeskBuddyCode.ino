
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ===============================
// 1. KHAI BAO CHAN
// ===============================

#define DHT_PIN 2
#define TOUCH1_PIN 3
#define TOUCH2_PIN 4
#define TRIG_PIN 7
#define ECHO_PIN 8
#define BUZZER_PIN 9

#define DHT_TYPE DHT11

Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHT dht(DHT_PIN, DHT_TYPE);

// ===============================
// 2. BIEN DU LIEU
// ===============================

float temperature = 0;
float humidity = 0;
float distance = 0;

bool showInfo = false;

int emotion = 1;
// 0 = NORMAL
// 1 = HAPPY
// 2 = SAD
// 3 = SURPRISED

bool oldTouch1 = LOW;
bool oldTouch2 = LOW;

unsigned long lastInteraction = 0;
unsigned long lastDHT = 0;

bool sleeping = false;

// ===============================
// 3. HAM DO KHOANG CACH
// ===============================

float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  unsigned long time = pulseIn(ECHO_PIN, HIGH, 12000);

  if (time == 0) {
    return -1;
  }

  return time * 0.0343 / 2;
}

// ===============================
// 4. HAM PHAT AM THANH
// ===============================

void beep() {
  tone(BUZZER_PIN, 1000, 100);
  delay(120);
}

// ===============================
// 5. VE MAT ROBOT
// ===============================

void drawFace() {
  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  // MAT BINH THUONG
  if (emotion == 0) {
    display.fillCircle(38, 25, 7, SSD1306_WHITE);
    display.fillCircle(90, 25, 7, SSD1306_WHITE);

    display.drawLine(52, 48, 76, 48, SSD1306_WHITE);
  }

  // MAT VUI
  else if (emotion == 1) {
    display.drawLine(25, 30, 38, 18, SSD1306_WHITE);
    display.drawLine(38, 18, 51, 30, SSD1306_WHITE);

    display.drawLine(77, 30, 90, 18, SSD1306_WHITE);
    display.drawLine(90, 18, 103, 30, SSD1306_WHITE);

    display.drawLine(48, 43, 56, 50, SSD1306_WHITE);
    display.drawLine(56, 50, 72, 50, SSD1306_WHITE);
    display.drawLine(72, 50, 80, 43, SSD1306_WHITE);
  }

  // MAT BUON
  else if (emotion == 2) {
    display.fillCircle(38, 25, 7, SSD1306_WHITE);
    display.fillCircle(90, 25, 7, SSD1306_WHITE);

    display.drawLine(48, 53, 56, 46, SSD1306_WHITE);
    display.drawLine(56, 46, 72, 46, SSD1306_WHITE);
    display.drawLine(72, 46, 80, 53, SSD1306_WHITE);
  }

  // MAT NGAC NHIEN
  else if (emotion == 3) {
    display.drawCircle(38, 25, 11, SSD1306_WHITE);
    display.drawCircle(90, 25, 11, SSD1306_WHITE);

    display.drawCircle(64, 49, 7, SSD1306_WHITE);
  }

  display.display();
}

// ===============================
// 6. HIEN THI THONG TIN
// ===============================

void drawInfo() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(15, 0);
  display.println("DESK BUDDY INFO");

  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);

  display.setCursor(0, 19);
  display.print("Temp: ");
  display.print(temperature, 1);
  display.println(" C");

  display.setCursor(0, 31);
  display.print("Hum: ");
  display.print(humidity, 1);
  display.println(" %");

  display.setCursor(0, 43);
  display.print("Dist: ");

  if (distance < 0) {
    display.println("--");
  } else {
    display.print(distance, 1);
    display.println(" cm");
  }

  display.setCursor(0, 56);
  display.println("T1: Emotion  T2: Info");

  display.display();
}

// ===============================
// 7. KHOI TAO HE THONG
// ===============================

void setup() {
  pinMode(TOUCH1_PIN, INPUT);
  pinMode(TOUCH2_PIN, INPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(BUZZER_PIN, OUTPUT);

  dht.begin();

  // KHOI TAO OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true);
  }

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(22, 25);
  display.println("DESK BUDDY");
  display.display();

  delay(1500);

  lastInteraction = millis();

  beep();
}

// ===============================
// 8. CHUONG TRINH CHINH
// ===============================

void loop() {

  // DOC CAM BIEN CHAM
  bool touch1 = digitalRead(TOUCH1_PIN);
  bool touch2 = digitalRead(TOUCH2_PIN);

  // TTP223 SO 1: DOI CAM XUC
  if (touch1 == HIGH && oldTouch1 == LOW) {

    emotion++;

    if (emotion > 3) {
      emotion = 0;
    }

    sleeping = false;
    showInfo = false;

    lastInteraction = millis();

    beep();
  }

  // TTP223 SO 2: BAT/TAT THONG TIN
  if (touch2 == HIGH && oldTouch2 == LOW) {

    showInfo = !showInfo;

    sleeping = false;
    lastInteraction = millis();

    beep();
  }

  oldTouch1 = touch1;
  oldTouch2 = touch2;

  // DOC CAM BIEN KHOANG CACH
  distance = readDistance();

  if (distance >= 2 && distance <= 5) {

    emotion = 1;
    sleeping = false;
    showInfo = false;

    lastInteraction = millis();
  }

  // DOC DHT11 MOI 2 GIAY
  if (millis() - lastDHT >= 2000) {

    lastDHT = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t)) {
      temperature = t;
    }

    if (!isnan(h)) {
      humidity = h;
    }
  }

  // CHE DO NGU SAU 5 GIAY
  if (millis() - lastInteraction >= 5000) {
    sleeping = true;
  }

  // HIEN THI OLED
  if (sleeping) {

    display.clearDisplay();

    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(35, 25);
    display.println("SLEEP");

    display.display();
  }

  else if (showInfo) {
    drawInfo();
  }

  else {
    drawFace();
  }

  delay(50);
}
