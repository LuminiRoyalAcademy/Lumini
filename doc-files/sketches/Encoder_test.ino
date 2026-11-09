// Rotary encoder test. No other parts needed.
// Open Serial Monitor at 115200 baud. One full click prints ONE line.
// Turn one way and the total goes down, the other way and it goes up.
#include "esp_timer.h"

const int CLK_PIN = 25;
const int DT_PIN  = 33;
const int SW_PIN  = 32;
const uint8_t REST_MASK = 0b1001;      // bit 0 = rests at 0,0   bit 3 = rests at 1,1
const uint8_t STABLE_SAMPLES = 2;      // a level must hold this many 1 ms samples

volatile long clicks = 0;
volatile int8_t acc = 0;
volatile uint8_t state = 0;
volatile uint32_t edges = 0;           // accepted level changes since the last click

void encoderPoll(void* arg) {
  static const int8_t T[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
  static uint8_t lastRaw = 0xFF;
  static uint8_t stableCount = 0;

  uint8_t raw = (digitalRead(CLK_PIN) << 1) | digitalRead(DT_PIN);
  if (raw != lastRaw) {
    lastRaw = raw;
    stableCount = 0;
    return;
  }
  if (stableCount < STABLE_SAMPLES) stableCount++;
  if (stableCount < STABLE_SAMPLES || raw == state) return;

  acc += T[(state << 2) | raw];
  state = raw;
  edges++;
  if ((REST_MASK >> raw) & 1) {
    if (acc >= 2) clicks++;
    else if (acc <= -2) clicks--;
    acc = 0;
  }
}

long lastClicks = 0;
int lastSw = -1;

void setup() {
  Serial.begin(115200);
  pinMode(CLK_PIN, INPUT_PULLUP);
  pinMode(DT_PIN, INPUT_PULLUP);
  pinMode(SW_PIN, INPUT_PULLUP);
  state = (digitalRead(CLK_PIN) << 1) | digitalRead(DT_PIN);

  esp_timer_create_args_t args = {};
  args.callback = &encoderPoll;
  args.name = "encoder";
  esp_timer_handle_t timer;
  esp_timer_create(&args, &timer);
  esp_timer_start_periodic(timer, 1000);

  Serial.println("Encoder test ready. Turn slowly, one click at a time.");
}

unsigned long lastStatus = 0;

void loop() {
  // Once a second: the raw pin levels. Hands off the knob, a correctly wired module reads 1, 1, 1.
  if (millis() - lastStatus >= 1000) {
    lastStatus = millis();
    Serial.print("levels now: CLK="); Serial.print(digitalRead(CLK_PIN));
    Serial.print(" DT="); Serial.print(digitalRead(DT_PIN));
    Serial.print(" SW="); Serial.println(digitalRead(SW_PIN));
  }

  int s = digitalRead(SW_PIN);
  if (s != lastSw) {
    lastSw = s;
    Serial.println(s == LOW ? "SW pressed" : "SW released");
  }
  long k = clicks;
  if (k != lastClicks) {
    Serial.print("click: total = "); Serial.print(k);
    Serial.print(k < lastClicks ? "  (down)" : "  (up)");
    Serial.print("   level changes since last click: "); Serial.println(edges);
    edges = 0;
    lastClicks = k;
  }
}
