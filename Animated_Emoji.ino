
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <map>
#include <string>
#include <WiFi.h>
#include <PubSubClient.h>


const char* WIFI_SSID = "DGI";
const char* WIFI_PASSWORD = "dgi@2024!";

const char* MQTT_TOPIC = "morse/tj-project-final/999"; 





const char* MQTT_BROKER = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
String clientID = "MorseESP32-";
WiFiClient espClient;
PubSubClient mqttClient(espClient);


const int MORSE_BUTTON_PIN = 32; 
const int BUZZER_PIN = 26;


#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

std::map<std::string, String> morseMap;
String currentMorse = "";
String translatedMessage = "";


unsigned long buttonPressTime = 0;
unsigned long lastReleaseTime = 0;
unsigned long lastDebounceTime = 0;
const long debounceTime = 50;
const long dashThreshold = 250;
const long letterTimeout = 1000;
const long wordTimeout = 2000;
const long resetTimeout = 5000;


int buttonState = HIGH;
int lastButtonState = HIGH;
bool letterCommitted = false;
bool wordCommitted = false;


const int buzzerFrequency = 800;
const int notifyFrequency = 1200;


String receivedMessage = "";
volatile bool newMessage = false;
bool messageIsDisplaying = false; // Used for TEXT messages only
unsigned long messageDisplayStartTime = 0;
const long messageDisplayTime = 5000;


void initializeMorseMap();
void translateAndClear();
void updateDisplay();
bool sendMessage();
void setup_wifi();
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void reconnect_mqtt();
void playWinkAnimation();
void playHeartAnimation();

void playLaughAnimation();
void playAngryAnimation();
void playSurprisedAnimation();
void playSkullAnimation();



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
  display.println("v17 (More GIFs!)");
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

    currentMorse = "";
    translatedMessage = "";
    
    if (receivedMessage == "EMOJI_WINK") {
      Serial.println("Received EMOJI_WINK, playing animation...");
      playWinkAnimation();
      updateDisplay();
      lastReleaseTime = millis();
      
    } else if (receivedMessage == "EMOJI_HEART") {
      Serial.println("Received EMOJI_HEART, playing animation...");
      playHeartAnimation();
      updateDisplay();
      lastReleaseTime = millis();
      
    // --- NEW: CATCH THE NEW EMOJI KEYWORDS ---
    } else if (receivedMessage == "EMOJI_LAUGH") {
      Serial.println("Received EMOJI_LAUGH, playing animation...");
      playLaughAnimation();
      updateDisplay();
      lastReleaseTime = millis();
      
    } else if (receivedMessage == "EMOJI_ANGRY") {
      Serial.println("Received EMOJI_ANGRY, playing animation...");
      playAngryAnimation();
      updateDisplay();
      lastReleaseTime = millis();
      
    } else if (receivedMessage == "EMOJI_SURPRISED") {
      Serial.println("Received EMOJI_SURPRISED, playing animation...");
      playSurprisedAnimation();
      updateDisplay();
      lastReleaseTime = millis();
      
    } else if (receivedMessage == "EMOJI_SKULL") {
      Serial.println("Received EMOJI_SKULL, playing animation...");
      playSkullAnimation();
      updateDisplay();
      lastReleaseTime = millis();
      

      
    } else {
      Serial.println("Received text, displaying for 5s...");
      display.clearDisplay();
      display.setCursor(0, 0);
      display.setTextSize(1);
      display.println("MSG RECEIVED:");
      display.setTextSize(2);
      display.setTextWrap(true);
      display.println(receivedMessage);
      display.display();
      
      messageIsDisplaying = true;
      messageDisplayStartTime = millis();
    }
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
          if (translatedMessage.endsWith(" ") || translatedMessage.endsWith(String(char(0)))) {

          } else {
             translatedMessage += " ";
          }
          
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



void playWinkAnimation() {

  int y = 20; // Centered Y
  
  // Play the animation 4 times
  for (int i = 0; i < 4; i++) {
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor(33, y); // Centered X for "(o o)"
    display.print("(o o)");
    display.display();
    delay(300);
    
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor(33, y); 
    display.print("(- -)");
    display.display();
    delay(200);
  }
  
  delay(500); 
}

void playHeartAnimation() {

  for (int i = 0; i < 4; i++) {

    display.clearDisplay();

    display.fillCircle(58, 28, 5, SSD1306_WHITE); // Left lobe
    display.fillCircle(70, 28, 5, SSD1306_WHITE); // Right lobe
    display.fillTriangle(53, 30, 75, 30, 64, 42, SSD1306_WHITE); // Point
    display.display();
    delay(250);

    // --- Frame 2: Big Heart ---
    display.clearDisplay();
    display.fillCircle(55, 25, 8, SSD1306_WHITE); // Left lobe
    display.fillCircle(73, 25, 8, SSD1306_WHITE); // Right lobe
    display.fillTriangle(47, 30, 81, 30, 64, 48, SSD1306_WHITE); // Point
    display.display();
    delay(250);
  }
}

// --- NEW ANIMATION FUNCTIONS ---

void playLaughAnimation() {
  // A simple text-based laughing animation
  int y = 20; // Centered Y
  
  // Play the animation 5 times
  for (int i = 0; i < 5; i++) {
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor(33, y); // Centered X
    display.print("(> <)");
    display.display();
    delay(200);
    
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor(33, y); // Centered X
    display.print("(v v)");
    display.display();
    delay(200);
  }
  
  delay(500); // Hold last frame
}

void playAngryAnimation() {
  // A simple text-based vibrating angry face
  int y = 20; // Centered Y
  
  // Play the animation 6 times
  for (int i = 0; i < 6; i++) {
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor(30, y); // Centered X
    display.print("(>_<)");
    display.display();
    delay(150);
    
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor(30, y+2); // Vibrate down
    display.print("(>O<)");
    display.display();
    delay(150);
  }
  
  delay(500); // Hold last frame
}

void playSurprisedAnimation() {
  // A simple text-based surprised face
  int y = 20; // Centered Y
  
  // Play the animation 3 times
  for (int i = 0; i < 3; i++) {
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor(30, y); // Centered X
    display.print("(o_o)");
    display.display();
    delay(400);
    
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor(30, y); // Centered X
    display.print("(O_O)");
    display.display();
    delay(300);
  }
  
  delay(500); // Hold last frame
}

void playSkullAnimation() {
  // A shape-based chattering skull
  
  // Play the animation 5 times
  for (int i = 0; i < 5; i++) {
    // --- Frame 1: Mouth Closed ---
    display.clearDisplay();
    display.fillRoundRect(50, 15, 28, 22, 6, SSD1306_WHITE); // Cranium
    display.fillRect(52, 37, 24, 8, SSD1306_WHITE);          // Jaw
    display.fillCircle(57, 26, 5, SSD1306_BLACK);           // Left eye
    display.fillCircle(71, 26, 5, SSD1306_BLACK);           // Right eye
    display.fillTriangle(62, 32, 66, 32, 64, 37, SSD1306_BLACK); // Nose
    display.display();
    delay(200);

    // --- Frame 2: Mouth Open ---
    display.clearDisplay();
    display.fillRoundRect(50, 15, 28, 22, 6, SSD1306_WHITE); // Cranium
    display.fillRect(52, 42, 24, 8, SSD1306_WHITE);          // Jaw (moved down)
    display.fillCircle(57, 26, 5, SSD1306_BLACK);           // Left eye
    display.fillCircle(71, 26, 5, SSD1306_BLACK);           // Right eye
    display.fillTriangle(62, 32, 66, 32, 64, 37, SSD1306_BLACK); // Nose
    display.display();
    delay(200);
  }
}

// --- END NEW ANIMATIONS ---


// ---
// --- Morse & Display Functions (Unchanged)
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

void initializeMorseMap() {
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
    
    morseMap["-.-.-"] = "OK"; morseMap["----"] = "YES";
    morseMap["...-."] = "NO"; morseMap[".--.--"] = "HELP"; morseMap["..--.-"] = "WAIT";  
    morseMap["-...--"] = "DONE"; morseMap["---..."] = "COME"; morseMap["--...-"] = "GO";
    morseMap[".-..--"] = "READY";

    
    // ---
    // --- EMOJI KEYWORDS (NOW WITH MORE EMOJIS) ---
    // ---
    Serial.println("Loading custom emoji codes...");
    morseMap[".-.-."] = "EMOJI_WINK";  // (A, R)
    morseMap[".-..-."] = "EMOJI_HEART"; // (A, L, F)
    
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
  
  // --- NEW: Add text previews for the new emojis ---
  displayMessage.replace("EMOJI_LAUGH", "[HAHA]");
  displayMessage.replace("EMOJI_ANGRY", "[ANGRY]");
  displayMessage.replace("EMOJI_SURPRISED", "[SURPRISED]");
  displayMessage.replace("EMOJI_SKULL", "[SKULL]");
  
  displayMessage.replace(String(char(0)), "");
  
  display.setTextSize(2);
  display.setTextWrap(true);
  display.println(displayMessage);

  display.display();
}