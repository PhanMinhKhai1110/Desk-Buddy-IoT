#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Servo.h>

// DESK BUDDY - Arduino Uno R3
// OLED + DHT11 + 2x TTP223 + HC-SR04 + 2x SG90 + passive buzzer D9
#define DHT_PIN       2
#define TOUCH1_PIN    3
#define TOUCH2_PIN    4
#define SERVO_LEFT    5
#define SERVO_RIGHT   6
#define TRIG_PIN      7
#define ECHO_PIN      8
#define BUZZER_PIN    9
#define OLED_ADDR     0x3C
#define DHT_TYPE      DHT11

Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHT dht(DHT_PIN, DHT_TYPE);
Servo leftEar, rightEar;

const unsigned long SLEEP_AFTER_MS = 5000UL;
const unsigned long SENSOR_INTERVAL_MS = 150UL;
const unsigned long DHT_INTERVAL_MS = 2000UL;
const unsigned long DISPLAY_INTERVAL_MS = 120UL;
const unsigned long SERVO_INTERVAL_MS = 20UL;
const unsigned long DEBOUNCE_MS = 180UL;
const unsigned long SOUND_GAP_MS = 45UL;

const float WAKE_DISTANCE_CM = 5.0;
const float RELEASE_DISTANCE_CM = 7.0;

// Chinh goc theo co cau tai thuc te. Hai servo thuong phai quay nguoc chieu co hoc.
const int LEFT_DOWN = 65;
const int LEFT_UP = 115;
const int RIGHT_DOWN = 115;
const int RIGHT_UP = 65;
const bool REVERSE_LEFT_SERVO = false;
const bool REVERSE_RIGHT_SERVO = false;

enum Emotion : byte {
  NORMAL, HAPPY, SAD, ANGRY, SURPRISED, SLEEPING,
  LOVE, CONFUSED, WINK, EXCITED, SUSPICIOUS, SHY
};
Emotion currentEmotion = HAPPY;

unsigned long lastInteraction = 0, lastSensorRead = 0, lastDhtRead = 0;
unsigned long lastDisplayDraw = 0, lastServoUpdate = 0;
unsigned long lastTouch1 = 0, lastTouch2 = 0;
bool previousTouch1 = false, previousTouch2 = false;
bool nearObject = false, showInfo = false;
float distanceCm = -1.0, temperatureC = NAN, humidityPct = NAN;
int leftPosition = 90, rightPosition = 90, leftTarget = 90, rightTarget = 90;

// 12 bo giai dieu ngan, tu tao, moi cam xuc mot mau not rieng.
// Can buzzer passive de nghe duoc cao do khac nhau.
const uint16_t emotionNotes[12][4] PROGMEM = {
  {523, 659, 587, 0},       // NORMAL
  {659, 784, 988, 1175},    // HAPPY
  {440, 392, 349, 330},     // SAD
  {880, 784, 698, 587},     // ANGRY
  {1047, 1319, 1175, 1568}, // SURPRISED
  {392, 349, 330, 294},     // SLEEPING
  {523, 659, 784, 988},     // LOVE
  {740, 494, 659, 440},     // CONFUSED
  {988, 1175, 784, 1047},   // WINK
  {784, 988, 1175, 1568},   // EXCITED
  {392, 554, 466, 622},     // SUSPICIOUS
  {587, 698, 659, 523}      // SHY
};
const uint16_t emotionDuration[12] PROGMEM = {
  115, 95, 210, 100, 115, 240, 145, 135, 95, 80, 160, 165
};
uint16_t soundNotes[6];
uint16_t soundDurations[6];
byte soundCount = 0, soundIndex = 0;
unsigned long nextSoundAt = 0;
bool soundPlaying = false;

const char* emotionName(Emotion e) {
  switch (e) {
    case NORMAL: return "NORMAL"; case HAPPY: return "HAPPY";
    case SAD: return "SAD"; case ANGRY: return "ANGRY";
    case SURPRISED: return "SURPRISED"; case SLEEPING: return "SLEEPING";
    case LOVE: return "LOVE"; case CONFUSED: return "CONFUSED";
    case WINK: return "WINK"; case EXCITED: return "EXCITED";
    case SUSPICIOUS: return "SUSPICIOUS"; case SHY: return "SHY";
  }
  return "UNKNOWN";
}

void stopSound() {
  noTone(BUZZER_PIN);
  soundPlaying = false;
  soundCount = soundIndex = 0;
}

void startEmotionSound(Emotion e, bool touch1Ack) {
  noTone(BUZZER_PIN);
  soundCount = soundIndex = 0;
  // TTP1 co mot not bao nhan, sau do la giai dieu cua cam xuc moi.
  if (touch1Ack) {
    soundNotes[soundCount] = 1319;
    soundDurations[soundCount++] = 65;
  }
  for (byte i = 0; i < 4; i++) {
    soundNotes[soundCount] = pgm_read_word(&emotionNotes[(byte)e][i]);
    soundDurations[soundCount] = pgm_read_word(&emotionDuration[(byte)e]);
    soundCount++;
  }
  soundPlaying = true;
  nextSoundAt = millis();
}

void playTouch2Sound() {
  // Hai tieng ting ngan, khac am bao TTP1.
  noTone(BUZZER_PIN);
  soundNotes[0] = 1568; soundDurations[0] = 55;
  soundNotes[1] = 1175; soundDurations[1] = 85;
  soundCount = 2; soundIndex = 0;
  soundPlaying = true;
  nextSoundAt = millis();
}

void updateSound(unsigned long now) {
  if (!soundPlaying || (long)(now - nextSoundAt) < 0) return;
  if (soundIndex >= soundCount) { stopSound(); return; }
  uint16_t f = soundNotes[soundIndex];
  uint16_t d = soundDurations[soundIndex];
  if (f == 0) noTone(BUZZER_PIN);
  else tone(BUZZER_PIN, f, d);
  soundIndex++;
  nextSoundAt = now + d + SOUND_GAP_MS;
}

void setEmotion(Emotion e, bool touch1Ack = false) {
  if (currentEmotion != e) {
    currentEmotion = e;
    Serial.print(F("Emotion: ")); Serial.println(emotionName(e));
    startEmotionSound(e, touch1Ack);
  } else if (touch1Ack) {
    startEmotionSound(e, true);
  }
}

void nextEmotion() {
  byte n = (byte)currentEmotion;
  for (byte i = 0; i < 12; i++) {
    n = (n + 1) % 12;
    if (n != SLEEPING) { setEmotion((Emotion)n); return; }
  }
}

float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 12000UL);
  if (duration == 0) return -1.0;
  return duration * 0.0343 / 2.0;
}

void updateUltrasonic(unsigned long now) {
  if (now - lastSensorRead < SENSOR_INTERVAL_MS) return;
  lastSensorRead = now;
  distanceCm = readDistanceCm();
  bool wasNear = nearObject;
  if (distanceCm >= 2.0 && distanceCm <= WAKE_DISTANCE_CM) nearObject = true;
  else if (distanceCm > RELEASE_DISTANCE_CM || distanceCm < 0) nearObject = false;
  if (nearObject) {
    lastInteraction = now;
    showInfo = false;
    if (!wasNear) setEmotion(HAPPY);
  }
}

void updateDHT(unsigned long now) {
  if (now - lastDhtRead < DHT_INTERVAL_MS) return;
  lastDhtRead = now;
  float t = dht.readTemperature(), h = dht.readHumidity();
  if (!isnan(t)) temperatureC = t;
  if (!isnan(h)) humidityPct = h;
}

void updateTouch(unsigned long now) {
  bool t1 = digitalRead(TOUCH1_PIN);
  bool t2 = digitalRead(TOUCH2_PIN);

  if (t1 && !previousTouch1 && now - lastTouch1 >= DEBOUNCE_MS) {
    lastTouch1 = now; lastInteraction = now; showInfo = false;
    if (currentEmotion == SLEEPING) setEmotion(HAPPY, true);
    else { nextEmotion(); startEmotionSound(currentEmotion, true); }
  }

  if (t2 && !previousTouch2 && now - lastTouch2 >= DEBOUNCE_MS) {
    lastTouch2 = now; lastInteraction = now;
    playTouch2Sound();
    if (currentEmotion == SLEEPING) {
      setEmotion(NORMAL); showInfo = true;
    } else {
      showInfo = !showInfo;
    }
  }
  previousTouch1 = t1;
  previousTouch2 = t2;
}

void updateSleep(unsigned long now) {
  if (nearObject) return;
  if (now - lastInteraction >= SLEEP_AFTER_MS && currentEmotion != SLEEPING) {
    showInfo = false;
    setEmotion(SLEEPING);
  }
}

int correctedAngle(int a, bool reverse) {
  a = constrain(a, 0, 180);
  return reverse ? 180 - a : a;
}

void setEarTargets(int l, int r) {
  leftTarget = correctedAngle(l, REVERSE_LEFT_SERVO);
  rightTarget = correctedAngle(r, REVERSE_RIGHT_SERVO);
}

void calculateEarPose(unsigned long now) {
  int phase = (now / 150UL) % 8;
  int wave = (phase < 4) ? phase : 7 - phase;
  int wiggle = wave * 3;
  switch (currentEmotion) {
    case NORMAL: setEarTargets(88 + wiggle, 92 - wiggle); break;
    case HAPPY: setEarTargets(105 + wiggle, 75 - wiggle); break;
    case SAD: setEarTargets(LEFT_DOWN, RIGHT_DOWN); break;
    case ANGRY: setEarTargets(115 + (wave % 2) * 4, 65 - (wave % 2) * 4); break;
    case SURPRISED: setEarTargets(LEFT_UP, RIGHT_UP); break;
    case SLEEPING: setEarTargets(LEFT_DOWN, RIGHT_DOWN); break;
    case LOVE: setEarTargets(100 + wiggle, 80 + wiggle); break;
    case CONFUSED: setEarTargets(LEFT_UP, RIGHT_DOWN); break;
    case WINK: setEarTargets(95 + wiggle, RIGHT_UP); break;
    case EXCITED: setEarTargets(108 + wiggle * 2, 72 - wiggle * 2); break;
    case SUSPICIOUS: setEarTargets(LEFT_DOWN + 12, RIGHT_UP - 12); break;
    case SHY: setEarTargets(LEFT_DOWN + 8 + wiggle, RIGHT_DOWN - 8 - wiggle); break;
  }
  leftTarget = constrain(leftTarget, 55, 125);
  rightTarget = constrain(rightTarget, 55, 125);
}

void updateServos(unsigned long now) {
  calculateEarPose(now);
  if (now - lastServoUpdate < SERVO_INTERVAL_MS) return;
  lastServoUpdate = now;
  if (leftPosition < leftTarget) leftPosition++;
  else if (leftPosition > leftTarget) leftPosition--;
  if (rightPosition < rightTarget) rightPosition++;
  else if (rightPosition > rightTarget) rightPosition--;
  leftEar.write(leftPosition);
  rightEar.write(rightPosition);
}

void drawEye(int x, int y, int pupilX, int pupilY, int w = 25, int h = 24) {
  display.fillRoundRect(x, y, w, h, 5, SSD1306_WHITE);
  int px = x + w / 2 + pupilX, py = y + h / 2 + pupilY;
  display.fillCircle(px, py, 4, SSD1306_BLACK);
  display.drawPixel(px - 1, py - 1, SSD1306_WHITE);
}
void drawClosedEyes() {
  display.drawLine(25, 29, 49, 29, SSD1306_WHITE);
  display.drawLine(79, 29, 103, 29, SSD1306_WHITE);
}
void drawHappyEyes(int o) {
  display.drawLine(25, 30 + o, 37, 19 + o, SSD1306_WHITE);
  display.drawLine(37, 19 + o, 49, 30 + o, SSD1306_WHITE);
  display.drawLine(79, 30 + o, 91, 19 + o, SSD1306_WHITE);
  display.drawLine(91, 19 + o, 103, 30 + o, SSD1306_WHITE);
}
void drawSmile(bool openMouth) {
  if (openMouth) {
    display.fillRoundRect(51, 44, 26, 13, 6, SSD1306_WHITE);
    display.drawLine(55, 47, 73, 47, SSD1306_BLACK);
  } else {
    display.drawLine(49, 45, 57, 51, SSD1306_WHITE);
    display.drawLine(57, 51, 71, 51, SSD1306_WHITE);
    display.drawLine(71, 51, 79, 45, SSD1306_WHITE);
  }
}
void drawHeart(int cx, int cy) {
  display.fillCircle(cx - 5, cy - 3, 6, SSD1306_WHITE);
  display.fillCircle(cx + 5, cy - 3, 6, SSD1306_WHITE);
  display.fillTriangle(cx - 11, cy, cx + 11, cy, cx, cy + 14, SSD1306_WHITE);
}

void drawFace(unsigned long now) {
  display.clearDisplay();
  bool anim = ((now / 300UL) % 2) != 0;
  bool blink = (now % 3800UL) < 130UL;
  int look = ((now / 900UL) % 3 == 0) ? -2 : (((now / 900UL) % 3 == 1) ? 0 : 2);
  switch (currentEmotion) {
    case NORMAL:
      if (blink) drawClosedEyes(); else { drawEye(25,17,look,0); drawEye(78,17,look,0); }
      display.drawLine(53,49,75,49,SSD1306_WHITE); break;
    case HAPPY:
      if (blink) drawClosedEyes(); else drawHappyEyes(anim ? -2 : 1);
      drawSmile(anim); break;
    case SAD:
      drawEye(25,20,0,3); drawEye(78,20,0,3);
      display.drawLine(23,16,50,10,SSD1306_WHITE); display.drawLine(78,10,105,16,SSD1306_WHITE);
      display.drawLine(49,53,57,47,SSD1306_WHITE); display.drawLine(57,47,71,47,SSD1306_WHITE); display.drawLine(71,47,79,53,SSD1306_WHITE); break;
    case ANGRY:
      drawEye(25,19,0,2); drawEye(78,19,0,2);
      display.drawLine(22,10,51,22,SSD1306_WHITE); display.drawLine(77,22,106,10,SSD1306_WHITE);
      display.drawLine(51,50,77,50,SSD1306_WHITE); break;
    case SURPRISED:
      drawEye(23,13,0,0,29,31); drawEye(76,13,0,0,29,31);
      display.drawCircle(64,52,anim ? 8 : 6,SSD1306_WHITE); break;
    case SLEEPING:
      drawClosedEyes(); display.drawLine(53,49,75,49,SSD1306_WHITE);
      display.setTextSize(1); display.setTextColor(SSD1306_WHITE); display.setCursor(anim ? 102 : 108, anim ? 13 : 5); display.print(F("Zz")); break;
    case LOVE:
      drawHeart(37,25); drawHeart(91,25); drawSmile(anim); break;
    case CONFUSED:
      drawEye(25,18,-3,2); drawEye(78,18,3,-2);
      display.drawLine(48,50,56,46,SSD1306_WHITE); display.drawLine(56,46,65,51,SSD1306_WHITE); display.drawLine(65,51,79,46,SSD1306_WHITE); break;
    case WINK:
      if (anim) display.drawLine(25,29,50,29,SSD1306_WHITE); else drawEye(25,17,0,0);
      drawEye(78,17,0,0); drawSmile(false); break;
    case EXCITED:
      drawEye(25,17,0,0); drawEye(78,17,0,0);
      display.fillRoundRect(49,43,30,anim ? 15 : 12,6,SSD1306_WHITE); display.drawLine(54,47,74,47,SSD1306_BLACK); break;
    case SUSPICIOUS:
      drawEye(25,19,look,1); drawEye(78,19,look,1);
      display.drawLine(23,11,50,14,SSD1306_WHITE); display.drawLine(78,8,105,15,SSD1306_WHITE); display.drawLine(53,49,75,49,SSD1306_WHITE); break;
    case SHY:
      drawEye(25,19,0,4); drawEye(78,19,0,4);
      if (anim) { display.fillCircle(14,43,5,SSD1306_WHITE); display.fillCircle(114,43,5,SSD1306_WHITE); }
      drawSmile(false); break;
  }
  display.display();
}

void drawInfo() {
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0); display.println(F("DESK BUDDY INFO")); display.drawLine(0,12,127,12,SSD1306_WHITE);
  display.setCursor(0,18); display.print(F("Temp: "));
  if (isnan(temperatureC)) display.print(F("--")); else display.print(temperatureC,1);
  display.println(F(" C"));
  display.setCursor(0,30); display.print(F("Hum:  "));
  if (isnan(humidityPct)) display.print(F("--")); else display.print(humidityPct,1);
  display.println(F(" %"));
  display.setCursor(0,42); display.print(F("Dist: "));
  if (distanceCm < 0) display.print(F("--")); else display.print(distanceCm,1);
  display.println(F(" cm"));
  display.setCursor(0,55); display.print(F("T1: emotion T2: info"));
  display.display();
}

void setup() {
  Serial.begin(9600);
  pinMode(TOUCH1_PIN, INPUT); pinMode(TOUCH2_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  dht.begin();
  leftEar.attach(SERVO_LEFT); rightEar.attach(SERVO_RIGHT);
  leftEar.write(90); rightEar.write(90);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("OLED error: check SDA/SCL and address 0x3C"));
    while (true) { }
  }
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(22,25); display.println(F("DESK BUDDY")); display.display();
  lastInteraction = millis();
  setEmotion(HAPPY);
}

void loop() {
  unsigned long now = millis();
  updateDHT(now);
  updateUltrasonic(now);
  updateTouch(now);
  updateSleep(now);
  updateServos(now);
  updateSound(now);

  if (now - lastDisplayDraw >= DISPLAY_INTERVAL_MS) {
    lastDisplayDraw = now;
    if (showInfo) drawInfo(); else drawFace(now);
  }
}
