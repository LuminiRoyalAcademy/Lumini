// Lumini board and wiring test: ESP32 DevKit (30-pin, ESP32-WROOM-32, USB-C or micro-USB), 1.3" SH1106 OLED
// Libraries (Library Manager): Adafruit SH110X + Adafruit GFX
// What it does: scans the I2C bus, shows every input live on the OLED and the Serial Monitor (115200 baud),
// and beeps the buzzer when any button is pressed. Use it to check the wiring before flashing the real sketch.
// Board in the Arduino IDE: "ESP32 Dev Module" or "DOIT ESP32 DEVKIT V1".

#include <Wire.h>
#include <Adafruit_GFX.h>
#define USE_SH1106 1                 // 1 = 1.3" SH1106, 0 = 0.96" SSD1306 (needs library "Adafruit SSD1306")
#define OLED_I2C_ADDRESS 0x3C        // some modules are 0x3D
#if USE_SH1106
  #include <Adafruit_SH110X.h>
  Adafruit_SH1106G display(128, 64, &Wire, -1);
  #define WHITE_PIXEL SH110X_WHITE
#else
  #include <Adafruit_SSD1306.h>
  Adafruit_SSD1306 display(128, 64, &Wire, -1);
  #define WHITE_PIXEL SSD1306_WHITE
#endif

// ----- pins (the same as the Lumini sketch) -----
const int L_CLK = 25, L_DT = 33, L_SW = 32;     // left knob
const int R_CLK = 16, R_DT = 17, R_SW = 18;     // right knob (GPIO16 = RX2, GPIO17 = TX2 on the board)
const int NAV   = 27;                            // NAV button to GND
const int BUZZ  = 26;                            // buzzer +
const int SDA_PIN = 21, SCL_PIN = 22;

struct Enc { int clk, dt; int state; long steps; };
Enc left  = {L_CLK, L_DT, 0, 0};
Enc right = {R_CLK, R_DT, 0, 0};
const int8_t TABLE[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};   // quadrature steps

void beep(int ms) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(BUZZ, 1000); delay(ms); ledcWriteTone(BUZZ, 0); ledcWrite(BUZZ, 0);
#else
  ledcWriteTone(0, 1000); delay(ms); ledcWriteTone(0, 0);
#endif
}

void sampleEnc(Enc& e) {
  int now = (digitalRead(e.clk) << 1) | digitalRead(e.dt);
  if (now != (e.state & 3)) {
    e.steps += TABLE[((e.state & 3) << 2) | now];
    e.state = now;
  }
}

bool oledOk = false;
void scanI2C() {
  Serial.println("I2C scan (SDA 21, SCL 22):");
  int found = 0;
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.print("  found 0x"); Serial.println(a, HEX); found++; }
  }
  if (!found) Serial.println("  nothing found: check SDA/SCL and the OLED power (3V3, GND)");
}

void setup() {
  Serial.begin(115200);
  int pins[] = {L_CLK, L_DT, L_SW, R_CLK, R_DT, R_SW, NAV};
  for (int p : pins) pinMode(p, INPUT_PULLUP);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BUZZ, 1000, 8);
#else
  ledcSetup(0, 1000, 8); ledcAttachPin(BUZZ, 0);
#endif
  Wire.begin(SDA_PIN, SCL_PIN);
  scanI2C();
#if USE_SH1106
  oledOk = display.begin(OLED_I2C_ADDRESS, true);
#else
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS);
#endif
  left.state  = (digitalRead(L_CLK) << 1) | digitalRead(L_DT);
  right.state = (digitalRead(R_CLK) << 1) | digitalRead(R_DT);
  beep(120);
  Serial.println(oledOk ? "OLED started." : "OLED NOT found.");
  Serial.println("Turn each knob, press each button. Values print when something changes.");
}

unsigned long lastDraw = 0;
String lastLine = "";
bool lastBtn[3] = {false, false, false};

void loop() {
  sampleEnc(left); sampleEnc(right);                       // sample as fast as possible
  bool b[3] = {digitalRead(L_SW) == LOW, digitalRead(R_SW) == LOW, digitalRead(NAV) == LOW};
  for (int i = 0; i < 3; i++) {
    if (b[i] && !lastBtn[i]) { beep(40); Serial.println(i == 0 ? "Left knob PRESSED" : i == 1 ? "Right knob PRESSED" : "NAV PRESSED"); }
    lastBtn[i] = b[i];
  }
  if (millis() - lastDraw < 60) return;
  lastDraw = millis();

  String line = String(left.steps) + "," + right.steps + "," + b[0] + b[1] + b[2];
  if (line != lastLine) { lastLine = line; Serial.printf("L steps %ld (clk %d dt %d)  R steps %ld (clk %d dt %d)  L-sw %d R-sw %d NAV %d\n",
      left.steps, digitalRead(L_CLK), digitalRead(L_DT), right.steps, digitalRead(R_CLK), digitalRead(R_DT), b[0], b[1], b[2]); }
  if (!oledOk) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE_PIXEL);
  display.setCursor(0, 0);  display.println("Lumini board test");
  display.printf("L clk%d dt%d sw%d\n", digitalRead(L_CLK), digitalRead(L_DT), b[0]);
  display.printf("  turns %ld\n", left.steps / 4);
  display.printf("R clk%d dt%d sw%d\n", digitalRead(R_CLK), digitalRead(R_DT), b[1]);
  display.printf("  turns %ld\n", right.steps / 4);
  display.printf("NAV %s\n", b[2] ? "PRESSED" : "-");
  display.display();
}
