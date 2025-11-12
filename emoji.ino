/*
 * Sketch: Morse_Transceiver_MQTT_v9
 * (Version 9 - Custom Emojis)
 *
 * This version changes the morseMap to store Strings
 * instead of single chars. This allows us to map
 * custom morse codes to text emojis like ":)" and "<3".
 *
 * See the 'initializeMorseMap' function to add your own!
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <map>
#include <string>
#include <WiFi.h>
#include <PubSubClient.h>

// ---
// --- 1. ! REQUIRED: EDIT THIS SECTION ! ---
// ---
const char* WIFI_SSID = "DGI";
const char* WIFI_PASSWORD = "dgi@2024!";
// ---
// --- 2. ! REQUIRED: MAKE THIS TOPIC UNIQUE ! ---
// ---
const char* MQTT_TOPIC = "morse/tj-project-final/999"; // <-- Use a new, unique topic
// ---

// --- MQTT Definitions ---
const char* MQTT_BROKER = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
String clientID = "MorseESP32-";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// --- Pin Definitions ---
const int MORSE_BUTTON_PIN = 32; // The ONE red button
const int BUZZER_PIN = 26;

// OLED Display (I2C)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---
// --- MORSE MAP MODIFICATION (Part 1) ---
// ---
// The map now holds a <string, String> pair, not <string, char>
std::map<std::string, String> morseMap;
// ---
String currentMorse = "";
String translatedMessage = "";

// --- Timing (One-Button) ---
unsigned long buttonPressTime = 0;
unsigned long lastReleaseTime = 0;
unsigned long lastDebounceTime = 0;
const long debounceTime = 50;
const long dashThreshold = 250;
const long letterTimeout = 1000;
const long wordTimeout = 2000;
const long resetTimeout = 5000;

// --- Button State ---
int buttonState = HIGH;
int lastButtonState = HIGH;
bool letterCommitted = false;
bool wordCommitted = false;

// --- Buzzer ---
const int buzzerFrequency = 800;
const int notifyFrequency = 1200;

// --- Receiving Messages ---
String receivedMessage = "";
volatile bool newMessage = false;
bool messageIsDisplaying = false;
unsigned long messageDisplayStartTime = 0;
const long messageDisplayTime = 5000;

// Function Prototypes
void initializeMorseMap();
void translateAndClear();
void updateDisplay();
bool sendMessage();
void setup_wifi();
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void reconnect_mqtt();

// ---
// --- WiFi & MQTT Functions (Unchanged from v8)
// ---

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("Connecting to WiFi");
  display.println(WIFI_SSID);
  display.display();
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.print(".");
    display.display();
    if (retries++ > 20) {
      Serial.println("\nFailed to connect to WiFi!");
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("WIFI FAILED!");
      display.println("Check credentials.");
      display.display();
      while(true);
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  char buffer[length + 1];
  memcpy(buffer, payload, length);
  buffer[length] = '\0';
  String fullMessage = String(buffer);

  Serial.print("Full msg received: "); Serial.println(fullMessage);

  int colonIndex = fullMessage.indexOf(':');
  if (colonIndex == -1) {
    Serial.println("Ignoring malformed message (no colon).");
    return;
  }

  String senderID = fullMessage.substring(0, colonIndex);
  String messageContent = fullMessage.substring(colonIndex + 1);

  if (senderID == clientID) {
    Serial.println("This is my own message (echo). Ignoring.");
    return;
  }

  receivedMessage = messageContent;
  newMessage = true;
  
  Serial.print("Message from OTHER: "); Serial.println(receivedMessage);
}

void reconnect_mqtt() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(1);
    display.println("Connecting to MQTT");
    display.println(MQTT_BROKER);
    display.display();
    
    if (mqttClient.connect(clientID.c_str())) {
      Serial.println("CONNECTED!");
      display.println("CONNECTED!");
      display.display();
      delay(500);
      
      Serial.print("Subscribing to topic: ");
      Serial.println(MQTT_TOPIC);
      if(mqttClient.subscribe(MQTT_TOPIC)) {
        Serial.println("Subscription successful!");
      } else {
        Serial.println("Subscription FAILED!");
      }
      
      updateDisplay();
    } else {
      Serial.print("FAILED, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      
      display.println("FAILED!");
      display.display();
      delay(5000);
    }
  }
}

// ---
// --- Main Setup & Loop (Unchanged from v8)
// ---

void setup() {
  Serial.begin(115200);

  clientID += String(random(0xffff), HEX);
  Serial.print("My client ID is: ");
  Serial.println(clientID);

  pinMode(MORSE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Morse Transceiver");
  display.println("v9 (Emojis!)");
  display.display();
  delay(1000);

  setup_wifi();
  
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqtt_callback);

  initializeMorseMap();
  updateDisplay();
  lastReleaseTime = millis();
}

void loop() {
  if (!mqttClient.connected()) {
    reconnect_mqtt();
  }
  mqttClient.loop();

  if (messageIsDisplaying && (millis() - messageDisplayStartTime > messageDisplayTime)) {
    messageIsDisplaying = false;
    updateDisplay();
    lastReleaseTime = millis();
  }

  if (newMessage) {
    newMessage = false;
    
    Serial.println("Playing notification sound!");
    tone(BUZZER_PIN, notifyFrequency, 100);
    delay(150);
    tone(BUZZER_PIN, notifyFrequency, 100);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("MSG RECEIVED:");
    display.setTextSize(2);
    display.setTextWrap(true);
    display.println(receivedMessage);
    display.display();

    currentMorse = "";
    translatedMessage = "";
    
    messageIsDisplaying = true;
    messageDisplayStartTime = millis();
  }

  if (!messageIsDisplaying) {
    int reading = digitalRead(MORSE_BUTTON_PIN);
    if (reading != lastButtonState) {
      lastDebounceTime = millis();
    }
    lastButtonState = reading;

    if ((millis() - lastDebounceTime) > debounceTime) {
      if (reading != buttonState) {
        buttonState = reading;
        if (buttonState == LOW) {
          Serial.println("Red Morse Button (32) PRESSED");
          buttonPressTime = millis();
          letterCommitted = false;
          wordCommitted = false;
          tone(BUZZER_PIN, buzzerFrequency);
        } else {
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

    if (buttonState == HIGH) {
      unsigned long timeSinceRelease = millis() - lastReleaseTime;
      
      if (timeSinceRelease > resetTimeout && translatedMessage.length() > 0) {
        Serial.println("Reset Timeout (5s)");
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
          Serial.println("Word Timeout (2s)... ADDING SPACE AND SENDING");
          translatedMessage += " ";
          
          bool sentOK = sendMessage();
          
          if (sentOK) {
            display.setCursor(0, 56);
            display.print("...SENT!");
          } else {
            display.setCursor(0, 56);
            display.print("...FAIL!");
          }
          display.display();
          delay(500);
        }
        updateDisplay();
        wordCommitted = true;
        letterCommitted = true;
      }
      else if (timeSinceRelease > letterTimeout && !letterCommitted) {
        if (currentMorse.length() > 0) {
          Serial.println("Letter Timeout (1s)");
          translateAndClear();
          updateDisplay();
        }
        letterCommitted = true;
      }
    }
  }
}

// ---
// --- Morse & Display Functions
// ---

bool sendMessage() {
  if (translatedMessage.length() == 0) {
    Serial.println("Message is empty, not sending.");
    return false;
  }

  String messageToSend = clientID + ":" + translatedMessage;

  Serial.print("Publishing message: ");
  Serial.println(messageToSend);

  if (mqttClient.publish(MQTT_TOPIC, messageToSend.c_str())) {
    Serial.println("Publish SUCCESS");
    return true;
  } else {
    Serial.println("Publish FAILED!");
    return false;
  }
}

// ---
// --- MORSE MAP MODIFICATION (Part 2) ---
// ---
void initializeMorseMap() {
    // Note: all chars 'A' are now strings "A"
    morseMap[".-"] = "A"; morseMap["-..."] = "B"; morseMap["-.-."] = "C";
    morseMap["-.."] = "D"; morseMap["."] = "E"; morseMap["..-."] = "F";
    morseMap["--."] = "G"; morseMap["...."] = "H"; morseMap[".."] = "I";
    morseMap[".---"] = "J"; morseMap["-.-"] = "K"; morseMap[".-.."] = "L";
    morseMap["--"] = "M"; morseMap["-."] = "N"; morseMap["---"] = "O";
    morseMap[".--."] = "P"; morseMap["--.-"] = "Q"; morseMap[".-."] = "R";
    morseMap["..."] = "S"; morseMap["-"] = "T"; morseMap["..-"] = "U";
    morseMap["...-"] = "V"; morseMap[".--"] = "W"; morseMap["-..-"] = "X";
    morseMap["-.--"] = "Y"; morseMap["--.."] = "Z";
    morseMap["-----"] = "0"; morseMap[".----"] = "1"; morseMap["..---"] = "2";
    morseMap["...--"] = "3"; morseMap["....-"] = "4"; morseMap["....."] = "5";
    morseMap["-...."] = "6"; morseMap["--..."] = "7"; morseMap["---.."] = "8";
    morseMap["----."] = "9";
    
    // ---
    // --- ADD YOUR NEW EMOJIS HERE ---
    // ---
    Serial.println("Loading custom emoji codes...");
    morseMap[".-.-."] = ":)";    // Smiley face
    morseMap["...-..-"] = ":(";    // Frowny face
    morseMap[".-..-."] = "<3";    // Heart
    morseMap["-..-."] = "XD";    // Laughing
    morseMap[".--.-."] = ":P";    // Sticking tongue out
    
    // You can add more here. Just pick a cool dot/dash combo!
    // morseMap["------"] = "SHRUG";
}
// ---

// ---
// --- MORSE MAP MODIFICATION (Part 3) ---
// ---
void translateAndClear() {
  if (currentMorse.length() > 0) {
    std::string currentMorseStd = currentMorse.c_str(); // Convert Arduino String to std::string
    
    if (morseMap.count(currentMorseStd)) {
      // It found a match in our map!
      // morseMap[currentMorseStd] is now a String
      translatedMessage += morseMap[currentMorseStd];
    } else {
      // It didn't find a match
      translatedMessage += "?";
    }
    currentMorse = "";
  }
}
// ---

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
