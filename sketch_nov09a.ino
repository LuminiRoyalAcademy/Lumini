#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <map>
#include <string>

// --- Pin Definitions (One-Button) ---
const int MORSE_BUTTON_PIN = 32;

// Buzzer
const int BUZZER_PIN = 26;

// OLED Display (I2C)
// SDA is on GPIO 21, SCL is on GPIO 22 as per diagram
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Morse Code Definitions ---
std::map<std::string, char> morseMap;
String currentMorse = "";
String translatedMessage = "";

// --- Timing (One-Button) ---
unsigned long buttonPressTime = 0;  // When was the button pressed?
unsigned long lastReleaseTime = 0;  // When was it last released?
unsigned long lastDebounceTime = 0; // For button debounce

const long debounceTime = 50;       // ms
const long dashThreshold = 250;     // ms (Press longer than this for a dash)
const long letterTimeout = 1500;    // ms (1.5s pause to end a letter)
const long wordTimeout = 3000;      // ms (3s pause to add a space)
const long resetTimeout = 10000;    // ms (10s pause to clear message)

// --- Button State ---
int buttonState = HIGH;         // Current debounced state
int lastButtonState = HIGH;     // Previous raw state
bool letterCommitted = false;   // To prevent multiple letter translations
bool wordCommitted = false;     // To prevent multiple word spaces

// --- Buzzer Settings ---
const int buzzerFrequency = 800; // Hz
// Note: dotDuration and dashDuration are no longer needed

// --- Function Prototypes ---
void initializeMorseMap();
void translateAndClear();
void updateDisplay();
// Note: playTone() is no longer needed

void setup() {
  Serial.begin(115200);

  // Initialize Pins
  pinMode(MORSE_BUTTON_PIN, INPUT_PULLUP);
pinMode(BUZZER_PIN, OUTPUT);

  // Initialize OLED Display
if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
// Don't proceed, loop forever
  }
  display.clearDisplay();
  display.setTextSize(1);
display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Morse Code Ready!");
  display.display();
  delay(2000);

// Populate the Morse code map
  initializeMorseMap();

  // Initial display update
  updateDisplay();
lastReleaseTime = millis();
}

void loop() {
  // --- 1. Read Button State with Debounce ---
  int reading = digitalRead(MORSE_BUTTON_PIN);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  lastButtonState = reading;

  if ((millis() - lastDebounceTime) > debounceTime) {
    // If the button state has changed...
    if (reading != buttonState) {
      buttonState = reading;

      // --- 2. Handle Button Press (LOW) ---
      if (buttonState == LOW) {
        buttonPressTime = millis();
        letterCommitted = false; // Reset flags
        wordCommitted = false;
        
        // Start the tone (it will play until noTone() is called)
        tone(BUZZER_PIN, buzzerFrequency);
      }
      
      // --- 3. Handle Button Release (HIGH) ---
      else {
        // Stop the tone immediately on release
        noTone(BUZZER_PIN);

        unsigned long pressDuration = millis() - buttonPressTime;

        if (pressDuration >= dashThreshold) {
          // It's a DASH
          Serial.println("Dash");
          currentMorse += "-";
        } else {
          // It's a DOT
          Serial.println("Dot");
          currentMorse += ".";
        }
        
        updateDisplay();
        lastReleaseTime = millis(); // Start the inactivity timers
      }
    }
  }

  // --- 4. Handle Timeouts (if button is UP) ---
  if (buttonState == HIGH) {
    unsigned long timeSinceRelease = millis() - lastReleaseTime;

    // Check for 10s Reset Timeout
    if (timeSinceRelease > resetTimeout && translatedMessage.length() > 0) {
      Serial.println("Reset Timeout");
  translatedMessage = "";
      currentMorse = "";
      updateDisplay();
      letterCommitted = true;
      wordCommitted = true;
    }
    // Check for 3s Word Timeout
    else if (timeSinceRelease > wordTimeout && !wordCommitted) {
      if (currentMorse.length() > 0) {
        translateAndClear();
      }
  if (translatedMessage.length() > 0) {
          Serial.println("Word Timeout");
    translatedMessage += " ";
      }
      updateDisplay();
      wordCommitted = true;
      letterCommitted = true;
    }
    // Check for 1.5s Letter Timeout
    else if (timeSinceRelease > letterTimeout && !letterCommitted) {
      if (currentMorse.length() > 0) {
        Serial.println("Letter Timeout");
        translateAndClear();
        updateDisplay();
      }
      letterCommitted = true;
    }
  }
}

/**
 * @brief Populates the map with Morse code to character translations.
*/
void initializeMorseMap() {
    morseMap[".-"] = 'A'; morseMap["-..."] = 'B'; morseMap["-.-."] = 'C';
    morseMap["-.."] = 'D';
morseMap["."] = 'E'; morseMap["..-."] = 'F';
    morseMap["--."] = 'G'; morseMap["...."] = 'H'; morseMap[".."] = 'I';
    morseMap[".---"] = 'J';
morseMap["-.-"] = 'K'; morseMap[".-.."] = 'L';
    morseMap["--"] = 'M'; morseMap["-."] = 'N'; morseMap["---"] = 'O';
    morseMap[".--."] = 'P';
morseMap["--.-"] = 'Q'; morseMap[".-."] = 'R';
    morseMap["..."] = 'S'; morseMap["-"] = 'T';
morseMap["..-"] = 'U';
    morseMap["...-"] = 'V'; morseMap[".--"] = 'W'; morseMap["-..-"] = 'X';
    morseMap["-.--"] = 'Y'; morseMap["--.."] = 'Z';
morseMap["-----"] = '0'; morseMap[".----"] = '1'; morseMap["..---"] = '2';
    morseMap["...--"] = '3'; morseMap["....-"] = '4'; morseMap["....."] = '5';
morseMap["-...."] = '6'; morseMap["--..."] = '7'; morseMap["---.."] = '8';
    morseMap["----."] = '9';
}

/**
 * @brief Translates the current Morse buffer to a character and appends it.
*/
void translateAndClear() {
  if (currentMorse.length() > 0) {
  std::string currentMorseStd = currentMorse.c_str();
    if (morseMap.count(currentMorseStd)) {
    translatedMessage += morseMap[currentMorseStd];
    } else {
    translatedMessage += "?";
    }
  currentMorse = "";
  }
}

/**
 * @brief Updates the OLED display with the current state.
*/
void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);

  // Display the current live morse code input
  display.setTextSize(2);
  display.print(">");
display.println(currentMorse);

  // Display the translated message
display.setTextSize(1);
  display.println("");
  display.setCursor(0, 24);
  display.println("Message:");
  display.setTextSize(2);
  display.println(translatedMessage);

display.display();
}

// The playTone() function is no longer needed
