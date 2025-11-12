/*
 * Sketch: Morse_Transceiver.ino
 * (Version 3 - Corrected for ESP32 Core v3.x.x / IDF v5.x)
 *
 * This ONE sketch is uploaded to BOTH ESP32 units.
 * Each unit can both send and receive Morse code messages.
 *
 * - Red Button (32):   Morse input
 * - Green Button (33): Send message
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <map>
#include <string>
#include <esp_now.h>
#include <WiFi.h>

// --- Pin Definitions ---
const int MORSE_BUTTON_PIN = 32; // Red button
const int SEND_BUTTON_PIN = 33;  // Green button
const int BUZZER_PIN = 26;

// OLED Display (I2C)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Morse Code Definitions ---
std::map<std::string, char> morseMap;
String currentMorse = "";
String translatedMessage = "";

// --- Timing (One-Button) ---
unsigned long buttonPressTime = 0;
unsigned long lastReleaseTime = 0;
unsigned long lastDebounceTime = 0;

const long debounceTime = 50;
const long dashThreshold = 250;     // ms (Press longer than this for a dash)
const long letterTimeout = 1000;    // ms (1s)
const long wordTimeout = 2000;      // ms (2s)
const long resetTimeout = 5000;     // ms (5s)

// --- Button State ---
int buttonState = HIGH;
int lastButtonState = HIGH;
bool letterCommitted = false;
bool wordCommitted = false;

// --- Buzzer Settings ---
const int buzzerFrequency = 800;

// --- ESP-NOW Definitions ---
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

// --- Variables for receiving messages ---
String receivedMessage = "";
volatile bool newMessage = false;
unsigned long messageDisplayStartTime = 0;
const long messageDisplayTime = 5000; // Show received msg for 5 seconds

// --- Button state for 'Send' button ---
int lastSendButtonState = HIGH;
unsigned long lastSendDebounce = 0;

// --- Function Prototypes ---
void initializeMorseMap();
void translateAndClear();
void updateDisplay();
void sendMessage();

// ---
// --- FIXED CALLBACKS (for Core v3.x.x / IDF v5+)
// ---

// --- NEW: Callback function when data is RECEIVED ---
// (This one was correct in the last version, but included for completeness)
void OnDataRecv(const esp_now_recv_info_t * info, const uint8_t *incomingData, int len) {
  char buffer[len + 1];
  memcpy(buffer, incomingData, len);
  buffer[len] = '\0';
  
  receivedMessage = String(buffer);
  newMessage = true; // Set the flag to show the new message
  Serial.print("Message received: ");
  Serial.println(receivedMessage);
}

// --- Callback function when data is SENT ---
// *** THIS IS THE NEW, CORRECTED VERSION ***
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}


// ---
// --- END OF FIXED CALLBACKS
// ---


void setup() {
  Serial.begin(115200);

  // Initialize Pins
  pinMode(MORSE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(SEND_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  // --- Initialize ESP-NOW ---
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register BOTH callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  // Add the broadcast peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }
  // --- End of ESP-NOW Init ---

  // Initialize OLED Display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Morse Transceiver");
  display.println("Ready!");
  display.display();
  delay(1000);

  initializeMorseMap();
  updateDisplay();
  lastReleaseTime = millis();
}

void loop() {

  // --- 1. Check if a new message was received ---
  if (newMessage) {
    newMessage = false; // Clear the flag
    
    // Clear the screen and show the received message
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("MSG RECEIVED:");
    display.setTextSize(2);
    display.setTextWrap(true);
    display.println(receivedMessage);
    display.display();

    // Clear our *own* typing buffers
    currentMorse = "";
    translatedMessage = "";
    
    // Wait 5 seconds
    delay(messageDisplayTime);

    // Go back to the main typing screen
    updateDisplay();
    lastReleaseTime = millis(); // Reset timers
    return; // <-- Stray 'E' was here and is now REMOVED
  }

  // --- 2. Read Morse Button State ---
  int reading = digitalRead(MORSE_BUTTON_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  lastButtonState = reading;

  if ((millis() - lastDebounceTime) > debounceTime) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) { // Button Pressed
        buttonPressTime = millis();
        letterCommitted = false;
        wordCommitted = false;
        tone(BUZZER_PIN, buzzerFrequency);
      } else { // Button Released
        noTone(BUZZER_PIN);
        unsigned long pressDuration = millis() - buttonPressTime;
        if (pressDuration >= dashThreshold) {
          currentMorse += "-";
        } else {
          currentMorse += ".";
        }
        updateDisplay();
        lastReleaseTime = millis();
      }
    }
  }

  // --- 3. Handle Timeouts ---
  if (buttonState == HIGH) {
    unsigned long timeSinceRelease = millis() - lastReleaseTime;
    
    if (timeSinceRelease > resetTimeout && translatedMessage.length() > 0) {
      translatedMessage = "";
      currentMorse = "";
      updateDisplay();
      letterCommitted = true;
      wordCommitted = true;
    }
    else if (timeSinceRelease > wordTimeout && !wordCommitted) {
      if (currentMorse.length() > 0) {
        translateAndClear();
      }
      if (translatedMessage.length() > 0) {
        translatedMessage += " ";
      }
      updateDisplay();
      wordCommitted = true;
      letterCommitted = true;
    }
    else if (timeSinceRelease > letterTimeout && !letterCommitted) {
      if (currentMorse.length() > 0) {
        translateAndClear();
        updateDisplay();
      }
      letterCommitted = true;
    }
  }
  
  // --- 4. Handle Send Button Press ---
  int sendReading = digitalRead(SEND_BUTTON_PIN);
  if (sendReading != lastSendButtonState) {
    lastSendDebounce = millis();
  }
  lastSendButtonState = sendReading;

  if ((millis() - lastSendDebounce) > debounceTime) {
    if (sendReading == LOW && translatedMessage.length() > 0) {
      sendMessage();
    }
  }
}

// --- Function to send the message ---
void sendMessage() {
  Serial.print("Broadcasting message: ");
  // *** THIS IS THE TYPO FIX (Println -> println) ***
  Serial.println(translatedMessage);

  // Send message via ESP-NOW to the broadcast address
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)translatedMessage.c_tEtr(), translatedMessage.length());

  if (result == ESP_OK) {
    display.setCursor(0, 56);
    display.print("...SENT!");
  }
  else {
    display.setCursor(0, 56);
    display.print("...SEND FAIL!");
  }
  display.display();
  delay(500); // Show "SENT" message for a moment

  // Clear the message after sending
  translatedMessage = "";
  currentMorse = "";
  updateDisplay();
  lastReleaseTime = millis(); // Reset timers
  letterCommitted = true;
  wordCommitted = true;
}

// --- Morse Map ---
void initializeMorseMap() {
    morseMap[".-"] = 'A'; morseMap["-..."] = 'B'; morseMap["-.-."] = 'C';
    morseMap["-.."] = 'D'; morseMap["."] = 'E'; morseMap["..-."] = 'F';
    morseMap["--."] = 'G'; morseMap["...."] = 'H'; morseMap[".."] = 'I';
    morseMap[".---"] = 'J'; morseMap["-.-"] = 'K'; morseMap[".-.."] = 'L';
    morseMap["--"] = 'M'; morseMap["-."] = 'N'; morseMap["---"] = 'O';
    morseMap[".--."] = 'P'; morseMap["--.-"] = 'Q'; morseMap[".-."] = 'R';
    morseMap["..."] = 'S'; morseMap["-"] = 'T'; morseMap["..-"] = 'U';
    morseMap["...-"] = 'V'; morseMap[".--"] = 'W'; morseMap["-..-"] = 'X';
    morseMap["-.--"] = 'Y'; morseMap["--.."] = 'Z';
    morseMap["-----"] = '0'; morseMap[".----"] = '1'; morseMap["..---"] = '2';
    morseMap["...--"] = '3'; morseMap["....-"] = '4'; morseMap["....."] = '5';
    morseMap["-...."] = '6'; morseMap["--..."] = '7'; morseMap["---.."] = '8';
    morseMap["----."] = '9';
}

// --- Translate Function ---
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

// --- Display Function ---
void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);

  display.setTextSize(2);
  display.print(">");
  display.println(currentMorse);

  display.setTextSize(1);
  display.println("");
  display.setCursor(0, 24);
  display.println("Message:");
  display.setTextSize(2);
  display.setTextWrap(true);
  display.println(translatedMessage);

  display.display();
}
