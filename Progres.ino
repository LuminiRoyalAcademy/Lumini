/*
 * Sketch: Morse_Transceiver_MQTT_v17_Firebase
 *
 * This version adds four new emoji animations AND
 * logs all sent messages to a Firebase Realtime Database.
 * * (Version 3 - Fully Corrected)
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <map>
#include <string>

// --- REQUIRED LIBRARIES ---
#include <WiFi.h>
#include <PubSubClient.h>
#include <Firebase_ESP_Client.h> // <-- NEW FIREBASE LIBRARY


// ---
// --- 1. ! REQUIRED: EDIT THIS SECTION ! ---
// ---
const char* WIFI_SSID = "DGI";     // <-- Your WiFi
const char* WIFI_PASSWORD = "dgi@2024!"; // <-- Your WiFi Password
// ---
// --- 2. ! REQUIRED: MAKE THIS TOPIC UNIQUE ! ---
// ---
const char* MQTT_TOPIC = "morse/tj-project-final/999"; //
// ---
// --- 3. ! REQUIRED: FIREBASE CREDENTIALS ! ---
// ---
const char* FIREBASE_HOST = "https://luminix-bcc28-default-rtdb.asia-southeast1.firebasedatabase.app/"; // <-- Your URL
const char* FIREBASE_AUTH = "YOUR_NEW_DATABASE_SECRET_HERE";                                           // <-- EDIT THIS (Use your NEW secret)
// ---


// --- MQTT Definitions ---
const char* MQTT_BROKER = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
String clientID = "MorseESP32-";
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// --- NEW: FIREBASE GLOBAL OBJECTS ---
FirebaseConfig config;
FirebaseAuth auth;
FirebaseData fbdo;

// --- Pin Definitions ---
const int MORSE_BUTTON_PIN = 32; // The ONE red button
const int BUZZER_PIN = 26;

// OLED Display (I2C)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Morse Code State ---
std::map<std::string, String> morseMap;
String currentMorse = "";
String translatedMessage = "";
String receivedMessage = "";
bool newMessage = false;

// --- Morse Timing (Simple) ---
const int DOT_TIME = 200; // ms
const int DASH_TIME = 600; // ms
const int WORD_GAP_TIME = 1400; // ms (1.4 seconds)

unsigned long lastReleaseTime = 0;
unsigned long pressStartTime = 0;
bool isPressing = false;

// --- Skull bitmap ---
// (Moved here so it's defined before it is used)
static const unsigned char PROGMEM epd_bitmap_Skull[] = {
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xc0, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xfc, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x7f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00,
 0x00, 0x03, 0xff, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00,
 0x00, 0x0f, 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00,
 0x00, 0x1f, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xff, 0xfc, 0x00, 0x00, 0x00,
 0x00, 0x3f, 0xff, 0xff, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xff, 0xfc, 0x00, 0x00, 0x00,
 0x00, 0x3f, 0xff, 0x00, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0x00, 0xfc, 0x00, 0x00, 0x00,
 0x00, 0x3f, 0xff, 0xff, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00,
 0x00, 0x1f, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00,
 0x00, 0x07, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x03, 0xff, 0xff, 0xc0, 0x00, 0x00, 0x00,
 0x00, 0x01, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xfe, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x3f, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x03, 0xc0, 0x00, 0x00, 0x00, 0x00
};

// ---
// --- NEW: FIREBASE LOGGING FUNCTION ---
// ---
// (Moved to the correct global scope)
void logMessageToFirebase(String senderID, String message) {
  Serial.println("--- LOGGING SENT MSG TO FIREBASE ---");
  
  // We will create a JSON object to send
  FirebaseJson json;
  json.set("sender", senderID);
  json.set("message", message);
  
  // This is the NEW, correct syntax for a server timestamp
  json.set("timestamp/.sv", "timestamp"); 

  // We will PUSH this data to a path called "/morse_log/"
  String logPath = "/morse_log";

  // CORRECTED Firebase Function: Use Firebase.RTDB.pushJSON
  // (This was the source of the 'pushJSON' error)
  if (Firebase.RTDB.pushJSON(&fbdo, logPath, &json)) {
    Serial.println("Firebase: Log push OK");
  } else {
    Serial.println("Firebase: Log push FAILED");
    Serial.println(fbdo.errorReason()); // This will tell us why
  }
  Serial.println("------------------------------------");
}


// ---
// --- Morse & Display Functions (Unchanged)
// ---

// --- THIS IS THE sendMessage() FUNCTION YOU WROTE ---
// --- It has been MODIFIED to call the Firebase logger ---
bool sendMessage() {
  if (translatedMessage.length() == 0) {
    Serial.println("Message is empty, not sending.");
    return false;
  }

  String messageToSend = clientID + ":" + translatedMessage;

  Serial.print("Publishing message: ");
  Serial.println(messageToSend);
  
  bool mqtt_success = false; // We add this to track success
  
  if (mqttClient.publish(MQTT_TOPIC, messageToSend.c_str())) {
    Serial.println("Publish SUCCESS");
    mqtt_success = true;
  } else {
    Serial.println("Publish FAILED!");
    mqtt_success = false;
  }
  
  // --- NEW: Log to Firebase ---
  // If the MQTT publish was successful, we log it.
  if (mqtt_success) {
    logMessageToFirebase(clientID, translatedMessage);
  }
  
  return mqtt_success;
}


void initializeMorseMap() {
    morseMap.clear();
    // Letters
    morseMap[".-"] = "A";
    morseMap["-..."] = "B";
    morseMap["-.-."] = "C";
    morseMap["-.."] = "D";
    morseMap["."] = "E";
    morseMap["..-."] = "F";
    morseMap["--."] = "G";
    morseMap["...."] = "H";
    morseMap[".."] = "I";
    morseMap[".---"] = "J";
    morseMap["-.-"] = "K";
    morseMap[".-.."] = "L";
    morseMap["--"] = "M";
    morseMap["-."] = "N";
    morseMap["---"] = "O";
    morseMap[".--."] = "P";
    morseMap["--.-"] = "Q";
    morseMap[".-."] = "R";
    morseMap["..."] = "S";
    morseMap["-"] = "T";
    morseMap["..-"] = "U";
    morseMap["...-"] = "V";
    morseMap[".--"] = "W";
    morseMap["-..-"] = "X";
    morseMap["-.--"] = "Y";
    morseMap["--.."] = "Z";

    // EMOJI CODES
    morseMap["...---"] = "EMOJI_WINK";      // (S, O)
    morseMap[".-.-"] = "EMOJI_HEART";     // (A, A)
    // --- NEW EMOJI CODES ---
    morseMap["...-.."] = "EMOJI_LAUGH";     // (S, L)
    morseMap["-..-"] = "EMOJI_ANGRY";     // (X)
    morseMap["---..."] = "EMOJI_SURPRISED"; // (O, S)
    morseMap["...-.-"] = "EMOJI_SKULL";     // (S, K)
    
}

void translateAndClear() {
  if (currentMorse.length() > 0) {
    std::string currentMorseStd = currentMorse.c_str();
    if (morseMap.count(currentMorseStd)) {
      translatedMessage += morseMap[currentMorseStd];
      if (morseMap[currentMorseStd].startsWith("EMOJI_")) {
        translatedMessage += String(char(0));
      }
      
    } else {
      translatedMessage += "?";
    }
    currentMorse = "";
  }
}

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
  
  String displayMessage = translatedMessage;
  displayMessage.replace("EMOJI_WINK", "[WINK]");
  displayMessage.replace("EMOJI_HEART", "[<3]");
  
  // --- NEW: Add text previews for new emojis ---
  displayMessage.replace("EMOJI_LAUGH", "[HAH!]");
  displayMessage.replace("EMOJI_ANGRY", "[GRR!]");
  displayMessage.replace("EMOJI_SURPRISED", "[WHOA!]");
  displayMessage.replace("EMOJI_SKULL", "[SKUL]");
  
  display.println(displayMessage);
  display.display();
}

// --- NEW: Heart Animation (Fixed) ---
void playAnimation_HEART() {
  for(int i=5; i<=25; i+=4) {
    display.clearDisplay();
    int circleX1 = 64 - i / 2;
    int circleX2 = 64 + i / 2;
    int circleY = 32 - i / 4;
    int circleR = i / 2;
    display.fillCircle(circleX1, circleY, circleR, SSD1306_WHITE);
    display.fillCircle(circleX2, circleY, circleR, SSD1306_WHITE);
    display.fillTriangle(
      circleX1 - circleR, circleY + 5,
      64, 32 + i,
      circleX2 + circleR, circleY + 5,
      SSD1306_WHITE
    );
    display.display();
    delay(30);
  }
  delay(500);
}

// --- NEW: Wink Animation ---
void playAnimation_WINK() {
  // Frame 1: Open eye
  display.clearDisplay();
  display.fillCircle(64, 32, 20, SSD1306_WHITE);
  display.fillCircle(64, 32, 10, SSD1306_BLACK);
  display.display();
  delay(200);
  // Frame 2: Closed eye
  display.clearDisplay();
  display.drawRect(44, 31, 40, 4, SSD1306_WHITE);
  display.display();
  delay(150);
}

// ---
// --- NEW ANIMATIONS (from your file) ---
// ---
void playAnimation_LAUGH() {
  for (int i=0; i<3; i++) {
    display.clearDisplay();
    display.fillCircle(64, 32, 20, SSD1306_WHITE); // Head
    display.fillCircle(56, 30, 3, SSD1306_BLACK); // Left Eye
    display.fillCircle(72, 30, 3, SSD1306_BLACK); // Right Eye
    display.drawPixel(56, 36, SSD1306_WHITE); // Tear
    display.drawPixel(72, 36, SSD1306_WHITE); // Tear
    display.drawRect(54, 42, 20, 5, SSD1306_BLACK); // Mouth
    display.display();
    delay(150);
    
    display.clearDisplay();
    display.fillCircle(64, 34, 20, SSD1306_WHITE); // Head (shaking down)
    display.fillCircle(56, 32, 3, SSD1306_BLACK); // Left Eye
    display.fillCircle(72, 32, 3, SSD1306_BLACK); // Right Eye
    display.drawPixel(56, 38, SSD1306_WHITE); // Tear
    display.drawPixel(72, 38, SSD1306_WHITE); // Tear
    display.drawRect(54, 44, 20, 5, SSD1306_BLACK); // Mouth
    display.display();
    delay(150);
  }
}

void playAnimation_ANGRY() {
  display.clearDisplay();
  display.fillCircle(64, 32, 20, SSD1306_WHITE); // Head
  // Angry eyes
  display.drawLine(52, 28, 60, 30, SSD1306_BLACK);
  display.drawLine(76, 28, 68, 30, SSD1306_BLACK);
  // Frown
  display.drawRect(56, 45, 16, 3, SSD1306_BLACK);
  display.display();
  delay(1000);
}

void playAnimation_SURPRISED() {
  display.clearDisplay();
  display.fillCircle(64, 32, 20, SSD1306_WHITE); // Head
  // Wide eyes
  display.fillCircle(56, 30, 4, SSD1306_BLACK);
  display.fillCircle(72, 30, 4, SSD1306_BLACK);
  // 'O' mouth
  display.fillCircle(64, 45, 5, SSD1306_BLACK);
  display.display();
  delay(1000);
}

void playAnimation_SKULL() {
  display.clearDisplay();
  display.drawBitmap(32, 0, epd_bitmap_Skull, 64, 64, SSD1306_WHITE);
  display.display();
  delay(1000);
}

// This function will call the correct animation
void playAnimation(String emojiName) {
  if (emojiName == "EMOJI_WINK") {
    playAnimation_WINK();
  } else if (emojiName == "EMOJI_HEART") {
    playAnimation_HEART();
  } else if (emojiName == "EMOJI_LAUGH") {
    playAnimation_LAUGH();
  } else if (emojiName == "EMOJI_ANGRY") {
    playAnimation_ANGRY();
  } else if (emojiName == "EMOJI_SURPRISED") {
    playAnimation_SURPRISED();
  } else if (emojiName == "EMOJI_SKULL") {
    playAnimation_SKULL();
  }
}


// ---
// --- Setup & Loop (Main Functions)
// ---

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Connecting to:");
  display.println(WIFI_SSID);
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.print(".");
    display.display();
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  display.println("\nWiFi Connected!");
  display.display();
}

void mqtt_reconnect() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Connecting to MQTT...");
    display.display();
    
    if (mqttClient.connect(clientID.c_str())) {
      Serial.println("connected");
      display.println("MQTT Connected!");
      display.display();
      mqttClient.subscribe(MQTT_TOPIC);
      delay(500);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      display.println("MQTT FAILED!");
      display.display();
      delay(5000);
    }
  }
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


// ---
// ---
// --- MAIN SETUP() ---
// ---
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
  display.println("Booting up...");
  display.println("My Client ID:");
  display.println(clientID);
  display.display();
  delay(1000);

  setup_wifi();
  
  // --- NEW: INITIALIZE FIREBASE (NEW SYNTAX) ---
  Serial.println("Initializing Firebase...");
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Connecting to Firebase");
  display.display();

  // Assign the database URL (MODERN)
  config.database_url = FIREBASE_HOST;
  
  // Assign the database secret (MODERN)
  auth.user.token = FIREBASE_AUTH;
  
  // Set a 1-minute timeout (MODERN)
  config.timeout.rtdb_read_timeout_ms = 1000 * 60; 

  Firebase.begin(&config, &auth); // Pass pointers
  Firebase.reconnectWiFi(true);

  Serial.println("Firebase initialized.");
  display.println("Firebase OK.");
  display.display();
  delay(500);
  // --- END NEW ---

  
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqtt_callback);

  initializeMorseMap();
  updateDisplay();
  lastReleaseTime = millis();
}

// ---
// ---
// --- MAIN LOOP() ---
// ---
// ---
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi();
  }

  if (!mqttClient.connected()) {
    mqtt_reconnect();
  }
  mqttClient.loop();

  // Check for new message
  if (newMessage) {
    newMessage = false;
    Serial.print("Processing new message: "); Serial.println(receivedMessage);
    
    String tempMsg = receivedMessage;
    int emojiIndex = tempMsg.indexOf(char(0));
    
    while(emojiIndex != -1) {
      String textPart = tempMsg.substring(0, emojiIndex);
      String fullEmojiCode = "";
      
      // We need to find the emoji code *after* the null char
      // To do this, we re-check the morse map
      std::string textBeforeEmojiStd = textPart.c_str();
      
      // Find what the emoji *was* by re-translating
      for (auto const& [key, val] : morseMap) {
          // Check if the value (e.g., "EMOJI_WINK") matches the substring
          // in the received message *after* the null character.
          if (val == receivedMessage.substring(emojiIndex+1, emojiIndex+1+val.length())) {
              if (val.startsWith("EMOJI_")) {
                  fullEmojiCode = val;
                  break;
              }
          }
      }
      
      if (fullEmojiCode != "") {
        Serial.print("Found emoji: "); Serial.println(fullEmojiCode);
        playAnimation(fullEmojiCode); // Call the main animation handler
        tempMsg = tempMsg.substring(emojiIndex + 1 + fullEmojiCode.length());
      } else {
        // Failsafe
        tempMsg = tempMsg.substring(emojiIndex + 1);
      }
      emojiIndex = tempMsg.indexOf(char(0));
    }
    
    receivedMessage = "";
    updateDisplay();
  }


  // --- Morse Button Logic ---
  unsigned long currentTime = millis();
  bool isPressed = (digitalRead(MORSE_BUTTON_PIN) == LOW);

  if (isPressed && !isPressing) {
    // --- BUTTON PRESS ---
    isPressing = true;
    pressStartTime = currentTime;
    translateAndClear();
    
    // Feedback
    digitalWrite(BUZZER_PIN, HIGH);
    display.fillCircle(120, 5, 3, SSD1306_WHITE);
    display.display();

  } else if (!isPressed && isPressing) {
    // --- BUTTON RELEASE ---
    isPressing = false;
    unsigned long pressDuration = currentTime - pressStartTime;
    lastReleaseTime = currentTime;
    
    // Feedback
    digitalWrite(BUZZER_PIN, LOW);
    display.fillRect(118, 3, 7, 7, SSD1306_BLACK); // Clear feedback dot
    
    if (pressDuration > DASH_TIME) {
      currentMorse += "-";
    } else if (pressDuration > DOT_TIME) {
      currentMorse += ".";
    }
    
    updateDisplay();
    
  } else if (!isPressed && !isPressing) {
    // --- IDLE (NOT PRESSING) ---
    if (currentTime - lastReleaseTime > WORD_GAP_TIME && translatedMessage.length() > 0) {
      // Time to send the message
      translateAndClear(); // Get the last letter
      
      Serial.println("WORD GAP DETECTED. SENDING MESSAGE.");
      if (sendMessage()) {
        translatedMessage = "Sent!";
      } else {
        translatedMessage = "SEND FAILED!";
      }
      updateDisplay();
      
      delay(1000); // Wait 1 sec
      translatedMessage = "";
      currentMorse = "";
      updateDisplay();
      lastReleaseTime = currentTime; // Reset timer
      
    } else if (currentTime - lastReleaseTime > DASH_TIME && currentMorse.length() > 0) {
      // Time to add a letter
      translateAndClear();
      updateDisplay();
      lastReleaseTime = currentTime; // Reset timer
    }
  }
}

// ---
// --- CUSTOM GFX FUNCTIONS
// ---

// (These were removed because they were buggy and
// the new HEART animation uses standard GFX functions)

// End of file