// Lumini Smart Telegraph: 1.3" OLED (SH1106) version
// Libraries (Library Manager): Adafruit SH110X + Adafruit GFX + PubSubClient
// The two versions are identical except for this one line:  #define USE_SH1106 1

#include <Wire.h>
#include <Adafruit_GFX.h>
// --- OLED DRIVER SELECT ---
// 1.3" I2C OLEDs are almost always SH1106 (132x64 RAM). The 0.96" ones are SSD1306.
// 1 = 1.3" SH1106 (needs library "Adafruit SH110X")
// 0 = 0.96" SSD1306 (needs library "Adafruit SSD1306")
#define USE_SH1106 1
#define OLED_I2C_ADDRESS 0x3C   // most modules; some are 0x3D

#if USE_SH1106
  #include <Adafruit_SH110X.h>
  #define OLED_WHITE SH110X_WHITE
  #define OLED_BLACK SH110X_BLACK
#else
  #include <Adafruit_SSD1306.h>
  #define OLED_WHITE SSD1306_WHITE
  #define OLED_BLACK SSD1306_BLACK
#endif
#include <map>
#include <string>
#include <vector>
#include <WiFi.h>
#include <WiFiClientSecure.h>   // encrypted connection to the private broker
#include <PubSubClient.h>
#include <Preferences.h> // For remembering WiFi
#include "esp_timer.h"   // Samples the rotary encoder every millisecond

// --- Globals ---
const char* TOPIC_ROOT = "morse/tj-project-final";   // change this to something unique to your school
// --- Private broker (fill these in) ---
// Every device logs in with its own serial as the user name and its PIN as the password.
// The broker only lets a user publish as itself, so nobody can send a message as someone else.
const char* MQTT_BROKER = "YOUR-CLUSTER-ID.s1.eu.hivemq.cloud";   // <-- your broker host name
const int   MQTT_PORT = 8883;                                      // TLS port
String clientID = "lumini-";            // the serial is added at start-up (one device per serial)
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
Preferences preferences; // Preferences object

const int MORSE_BUTTON_PIN = 32;
const int BUZZER_PIN = 26;
const int NAV_PIN = 27;                 // NAV button: opens the menu (press again to close it)
const long NAV_LONG_PRESS_MS = 1000;

// --- Who this device is ---
// The serial number is asked for once, the first time the device starts, and then remembered.
// Students: digits only, for example 47.   Teachers: the letter T and digits, for example T33.
String mySerial = "";
String myPin = "";                      // chosen by the student; sent to the broker as the password
const int   PIN_MIN_LEN = 4;
const int   PIN_MAX_LEN = 6;
const char  PIN_CHARS[] = "0123456789";
String myName = "";                     // initials, looked up in DIRECTORY below (can be empty)
const char* SEND_TO_ALL = "ALL";        // recipient value meaning "announce to every device"
const int   SERIAL_MAX_LEN = 6;
const char  SERIAL_CHARS[] = "0123456789T";   // what the wheel offers when typing a serial

// --- Who this device can send to (sample entries: replace with real serials and initials) ---
// The initials are optional. They are only used to show a name next to a serial.
struct Contact { const char* serial; const char* name; };
const Contact DIRECTORY[] = {
  {"47",  "TJD"},    // sample student
  {"12",  "ABC"},    // sample student
  {"23",  "XYZ"},    // sample student
  {"T33", "TCHR"}    // sample teacher
};
const int DIRECTORY_SIZE = sizeof(DIRECTORY) / sizeof(DIRECTORY[0]);

// --- Buzzer volume ---
const int BUZZER_VOLUME = 40;           // 0 to 100. Lower = quieter. 100 is the old loudness.

// --- Screens and button events ---
enum Screen { SCREEN_COMPOSE, SCREEN_INBOX, SCREEN_PICK, SCREEN_MENU };
enum LeftEvent { LEFT_NONE, LEFT_SHORT, LEFT_HOLD2, LEFT_HOLD5, LEFT_DOUBLE };
enum NavEvent { NAV_NONE, NAV_SHORT, NAV_LONG };

// --- Inbox: the last 10 messages, newest first (kept in memory only) ---
struct InboxItem { String fromSerial; String fromName; String text; bool unread; bool announcement; };
const int INBOX_MAX = 10;
InboxItem inbox[INBOX_MAX];
int inboxCount = 0;
int inboxIndex = 0;                     // message being read on the inbox screen (0 = newest)

Screen currentScreen = SCREEN_COMPOSE;
String recipientSerial = "";            // who the message will go to ("" = nobody chosen, "ALL" = everyone)
String recipientName = "";
String topicMine;                       // what this device listens to: messages sent to it (ROOT/dm/<me>/from/<sender>)
String topicAnnounce;                   // announcements from any teacher (ROOT/announce/<teacher>)
std::vector<String> pickSerials;           // the "send to" list while choosing
std::vector<String> pickNames;
int pickIndex = 0;
const int MENU_ITEMS = 4;               // Compose, Inbox, Send to, Clear message
int menuIndex = 0;

// Set by mqtt_callback, handled by loop()
volatile bool pendingDM = false;
volatile bool pendingAnnouncement = false;
String lastDmFrom, lastDmText;
String announceFrom, announceText;
bool announcementShowing = false;
unsigned long announcementStart = 0;
unsigned long lastAlertBeep = 0;

// --- Two rotary encoders ---
// LEFT knob  (cursor): turn = move the cursor, short press = space,
//                      hold 2 s = delete, hold 5 s = send.
//                      Its push switch (SW) is on pin 32, the same pin as MORSE_BUTTON_PIN.
// RIGHT knob (wheel):  turn = scroll letters, press = insert the letter shown,
//                      hold 0.8 s = switch between UPPER and lower case.
const int  ENC_CLK_PIN   = 25;   // left knob
const int  ENC_DT_PIN    = 33;
const int  WHEEL_CLK_PIN = 16;   // right knob
const int  WHEEL_DT_PIN  = 17;
const int  WHEEL_SW_PIN  = 18;
const bool ENC_REVERSE   = false;  // true if turning the left knob right moves the cursor left
const bool WHEEL_REVERSE = false;  // true if turning the right knob right scrolls backwards
const long WHEEL_LONG_PRESS_MS = 800;
const int  MAX_MESSAGE_LEN = 100;
// Which CLK/DT levels a knob rests at between clicks (CLK = bit 1, DT = bit 0).
// Bit 0 = rests at 0,0   Bit 3 = rests at 1,1.
// Yours rest at BOTH (every click is half a cycle), so 0b1001.
// Use 0b0001 if a knob only rests at 0,0, or 0b1000 if it only rests at 1,1.
const uint8_t ENC_REST_MASK = 0b1001;
const uint8_t ENC_STABLE_SAMPLES = 2;  // a level must hold for this many 1 ms samples (removes bounce)

struct EncoderReader {
  uint8_t clkPin, dtPin;
  volatile long clicks;     // whole clicks counted by the 1 ms sampler below
  volatile int8_t acc;
  volatile uint8_t state;
  uint8_t lastRaw;
  uint8_t stableCount;
};
EncoderReader cursorEnc = {ENC_CLK_PIN, ENC_DT_PIN, 0, 0, 0, 0xFF, 0};
EncoderReader wheelEnc  = {WHEEL_CLK_PIN, WHEEL_DT_PIN, 0, 0, 0, 0xFF, 0};
void pollEncoder(EncoderReader& e);   // declared here so the Arduino IDE finds the type
long lastEncoderCount = 0;   // cursor knob
long lastWheelCount = 0;     // wheel knob
int cursorPos = 0;   // insertion point in translatedMessage (0 = before the first letter)
int viewStart = 0;   // first character of the message that is visible on screen
int wheelIndex = 0;  // letter currently shown on the wheel
const char WHEEL_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@!";
const int WHEEL_LEN = sizeof(WHEEL_CHARS) - 1;

// --- Text shortcuts ---
// Type an abbreviation and the full phrase is offered on screen.
// Press SPACE (short press on the left knob) to accept it.
// To add one, add a line below: {"abbreviation", "full phrase"}  (keep the abbreviation lower case).
struct Shortcut { const char* key; const char* text; };
const Shortcut SHORTCUTS[] = {
  {"wyd",  "what are you doing?"},
  {"wya",  "where are you at?"},
  {"wdym", "what do you mean?"},
  {"hru",  "how are you?"},
  {"hbu",  "how about you?"},
  {"omw",  "on my way"},
  {"brb",  "be right back"},
  {"gtg",  "got to go"},
  {"ttyl", "talk to you later"},
  {"cya",  "see you later"},
  {"lmk",  "let me know"},
  {"idk",  "i do not know"},
  {"ikr",  "i know right"},
  {"nvm",  "never mind"},
  {"np",   "no problem"},
  {"ty",   "thank you"},
  {"thx",  "thanks"},
  {"yw",   "you are welcome"},
  {"pls",  "please"},
  {"sry",  "sorry"},
  {"ofc",  "of course"},
  {"tbh",  "to be honest"},
  {"rn",   "right now"},
  {"tmr",  "tomorrow"},
  {"bc",   "because"},
  {"hw",   "homework"},
  {"gm",   "good morning"},
  {"gn",   "good night"},
  {"sec",  "one second please"}
};

// Remembers the last expansion so that a delete right afterwards can undo it.
struct ExpansionUndo {
  bool valid;
  int start;       // where the expanded phrase begins
  int end;         // cursor position just after the phrase and its space
  String phrase;   // the expanded phrase plus the space after it
  String restore;  // what to put back in its place
};
ExpansionUndo expUndo = {false, 0, 0, "", ""};

// Samples CLK and DT of one knob. A new level is only accepted once it has stayed the
// same for ENC_STABLE_SAMPLES samples in a row, which filters out contact bounce.
void pollEncoder(EncoderReader& e) {
  static const int8_t T[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

  uint8_t raw = (digitalRead(e.clkPin) << 1) | digitalRead(e.dtPin);
  if (raw != e.lastRaw) {
    e.lastRaw = raw;
    e.stableCount = 0;
    return;
  }
  if (e.stableCount < ENC_STABLE_SAMPLES) e.stableCount++;
  if (e.stableCount < ENC_STABLE_SAMPLES || raw == e.state) return;  // not stable yet, or no change

  e.acc = e.acc + T[(e.state << 2) | raw];
  e.state = raw;
  if ((ENC_REST_MASK >> raw) & 1) {            // back at rest: the click is finished
    if (e.acc >= 2) e.clicks = e.clicks + 1;   // at least half a click of travel counts
    else if (e.acc <= -2) e.clicks = e.clicks - 1;
    e.acc = 0;
  }
}

// Runs every millisecond on its own timer, so screen updates can't make it miss a click.
void encoderPoll(void* arg) {
  pollEncoder(cursorEnc);
  pollEncoder(wheelEnc);
}
bool deleteArmed = false; // true once the left button has been held long enough to delete
bool sendArmed = false; // true once the button has been held long enough to confirm (serial entry)
bool confirmHoldCue = false;  // sound the 5-second "release to confirm" tone (serial entry only)

// --- Send = press both knobs together ---
const long SEND_CHORD_MS = 250;   // hold the left AND right knob down this long to send
bool chordTiming = false;         // both knobs are down right now
unsigned long chordStart = 0;     // when both went down
bool chordFired = false;          // the send already happened for this press
bool ignoreLeftRelease = false;   // the next left-knob release belonged to a send

// --- Double press of the left knob = delete ---
const long DOUBLE_PRESS_MS = 350;   // two presses closer together than this count as a double press
bool leftPending = false;           // one press happened, waiting to see if a second follows
unsigned long leftPendingAt = 0;
bool wheelIgnoreRelease = false;  // the next right-knob release belonged to a send
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET      -1
#if USE_SH1106
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#else
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#endif

std::map<std::string, String> morseMap;
String currentMorse = "";
String translatedMessage = "";

// --- Timing ---
unsigned long buttonPressTime = 0;
unsigned long lastReleaseTime = 0;
unsigned long lastDebounceTime = 0;
const long debounceTime = 50;
const long dashThreshold = 250;     // 0.25s
const long letterTimeout = 1000;    // 1s
const long wordTimeout = 2000;      // 2s (for sending messages)
const long resetTimeout = 5000;     // 5s (for clearing message)
const long deleteThreshold = 2000;  // 2s (for password delete)
const long submitThreshold = 5000;  // 5s (for password submit)

// --- MQTT Reconnect Timing ---
long lastReconnectAttempt = 0;

int buttonState = HIGH;
int lastButtonState = HIGH;
bool letterCommitted = false;
bool wordCommitted = false;

// --- Sounds ---
const int buzzerFrequency = 800;
const int notifyFrequency = 1200;

// --- State ---
String receivedMessage = "";
volatile bool newMessage = false;
bool messageIsDisplaying = false;
unsigned long messageDisplayStartTime = 0;
const long messageDisplayTime = 5000;

// --- Blind Mode ---
bool isBlindMode = false;
std::map<char, std::string> reverseMorseMap;
const int dotDuration = 250; 
const int dashDuration = dotDuration * 3;
const int interElementPause = dotDuration;
const int interLetterPause = dotDuration * 3;
const int interWordPause = dotDuration * 7;

// --- WiFi Password Entry ---
bool isEnteringPassword = false;
String passwordEntry = "";
String selectedSSID = "";

// --- Case Toggle ---
bool isUppercase = true;


// --- Function Declarations ---
void initializeMorseMap();
void translateAndClear();
void updateDisplay();
bool sendMessage();
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void reconnect_mqtt(); 
void playWinkAnimation();
void playHeartAnimation();
void playLaughAnimation();
void playAngryAnimation();
void playSurprisedAnimation();
void playSkullAnimation();
void initializeReverseMorseMap();
void playMorseCode(std::string morseCode);
void playMorseString(String message);
bool connect_to_wifi(String ssid, String password);
bool interactive_wifi_setup();
int unitLenAt(int i);
int prevUnitStart(int pos);
void moveCursor(int dir);
void sendCurrentMessage();
void handleEncoder();
String toDisplayText(String s);
char wheelChar(int i);
void insertAtCursor(const String& s);
void insertSpace();
void deleteBeforeCursor();
void handleWheel();
void handleWheelButton();
void checkSendChord();
String suggestionAtCursor();
bool acceptSuggestion();
bool undoExpansion();
String recipientLabel();
void chooseDefaultRecipient();
void addToInbox(const String& fromSerial, const String& fromName, const String& text, bool announcement);
int unreadCount();
void switchScreen(Screen next);
void enterPicker();
void drawAnnouncement();
void drawInbox();
void drawPicker();
void startAnnouncement();
void notifyNewMessage();
void alertBeeps();
void handleAnnouncementDismiss();
void handleComposeScreen();
void handleInboxScreen();
void handlePickScreen();
void handleMenuScreen();
void openMenu();
void runMenuItem(int item);
void drawMenu();
void replyToSelectedMessage();
void deleteSelectedMessage();
LeftEvent pollLeftButton();
LeftEvent pollLeftGesture();
bool isValidSerial(const String& s);
bool isTeacherSerial(const String& s);
String nameFor(const String& serial);
String labelFor(const String& serial, const String& name);
void ensureSerial(bool forceNew);
String askForSerial();
String askForPin();
bool isValidPin(const String& s);
void handleBadLogin();
String askForEntry(const char* title, const char* chars, int maxLen, bool masked, bool (*valid)(const String&));
void buzzInit();
void buzzOn(int freq);
void buzzOff();
void buzzFor(int freq, int ms);
NavEvent pollNavButton();


/**
 * @brief Tries to connect to a specific WiFi network.
 * @return true on success, false on failure.
 */
bool connect_to_wifi(String ssid, String password) {
  delay(10);
  Serial.println();
  Serial.print("Connecting to '");
  Serial.print(ssid);
  Serial.print("' with password '");
  Serial.print(password);
  Serial.println("'");

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("Connecting to WiFi");
  display.println(ssid);
  if (password.length() > 0) {
    display.println("with password...");
  }
  display.display();
  
  // Start the connection
  WiFi.begin(ssid.c_str(), password.c_str());

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.print(".");
    display.display();
    
    // Check for timeout
    if (retries++ > 20) {
      Serial.println("\nFailed to connect!");
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("WIFI FAILED!");
      display.println("Check password.");
      display.display();
      delay(2000); // Show message
      return false; // Return failure
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // --- NEW: SAVE CREDENTIALS ON SUCCESS ---
  Serial.println("Saving WiFi credentials...");
  preferences.begin("morse-wifi", false); // Open in R/W mode
  preferences.putString("ssid", ssid);
  preferences.putString("pass", password);
  preferences.end();
  Serial.println("Credentials saved.");
  // --- END NEW ---

  return true; // Return success
}

/**
 * @brief Scans for networks and lets the user select one.
 * @return true if connection was successful *immediately* (i.e., open network), 
 * false if it needs password entry or failed.
 */
bool interactive_wifi_setup() {
    std::vector<String> availableSSIDs;
    std::vector<wifi_auth_mode_t> availableEncryptions;

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Scanning for WiFi...");
    display.display();
    Serial.println("Scanning for WiFi...");

    // Scan for networks
    int n = WiFi.scanNetworks();
    Serial.println("Scan done.");

    if (n == 0) {
        Serial.println("No networks found.");
        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("No networks found!");
        display.display();
        delay(2000);
        return false; // Critical failure
    }

    Serial.print(n);
    Serial.println(" networks found.");

    // Store all found networks
    for (int i = 0; i < n; ++i) {
        availableSSIDs.push_back(WiFi.SSID(i));
        availableEncryptions.push_back(WiFi.encryptionType(i));
    }

    // --- Selection UI ---
    int selectedIndex = 0;
    bool isSelected = false;

    while (!isSelected) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("Select WiFi:");
        display.println("(.=Next, -=Select)");
        display.drawFastHLine(0, 18, 128, OLED_WHITE);

        int listY = 22;
        int maxLines = 5; 
        int startIdx = 0;

        if (selectedIndex >= maxLines) {
            startIdx = selectedIndex - maxLines + 1;
        }

        for (int i = 0; i < availableSSIDs.size() && (i - startIdx) < maxLines; i++) {
            display.setCursor(0, listY + (i - startIdx) * 8);
            if (i == selectedIndex) {
                display.print("> ");
            } else {
                display.print("  ");
            }
            // Add a lock icon for secured networks
            String lock = (availableEncryptions[i] == WIFI_AUTH_OPEN) ? " " : "*";
            
            // Truncate long SSIDs
            String displayName = availableSSIDs[i];
            if (displayName.length() > 16) {
                displayName = displayName.substring(0, 16);
            }
            display.print(displayName + lock);
        }
        display.display();

        // Wait for button press (blocking)
        while (digitalRead(MORSE_BUTTON_PIN) == HIGH) {
            delay(10); // Wait for a press
        }

        // --- Button press logic ---
        unsigned long pressTime = millis();
        buzzOn(buzzerFrequency); // Audio feedback
        while (digitalRead(MORSE_BUTTON_PIN) == LOW) {
            delay(10); // small delay
        }
        buzzOff();
        unsigned long pressDuration = millis() - pressTime;

        if (pressDuration >= dashThreshold) {
            // DASH - Select
            isSelected = true;
            Serial.print("Selected: ");
            Serial.println(availableSSIDs[selectedIndex]);
        } else if (pressDuration > debounceTime) {
            // DOT - Next
            selectedIndex = (selectedIndex + 1) % availableSSIDs.size(); // Wrap around
            Serial.print("Moved to: ");
            Serial.println(availableSSIDs[selectedIndex]);
        }
        // else it was just noise, ignore and loop
    }

    // --- Process Selection ---
    selectedSSID = availableSSIDs[selectedIndex]; // Store SSID globally
    wifi_auth_mode_t selectedEncryption = availableEncryptions[selectedIndex];

    if (selectedEncryption == WIFI_AUTH_OPEN) {
        Serial.println("Network is open. Connecting...");
        return connect_to_wifi(selectedSSID, ""); // Connect with no password
    } else {
        // --- THIS IS THE NEW PART ---
        Serial.println("Network is secured. Entering password mode.");
        isEnteringPassword = true; // Set the global flag
        passwordEntry = ""; // Clear any old password
        currentMorse = "";
        isUppercase = true; // Default to uppercase for passwords
        updateDisplay(); // Show the password entry screen
        return false; // Return false to indicate connection is NOT complete
    }
}


void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String body;
  body.reserve(length);
  for (unsigned int i = 0; i < length; i++) body += (char)payload[i];
  Serial.print("Received on "); Serial.print(topic); Serial.print(": "); Serial.println(body);

  // The broker only lets a user publish on topics that end in its OWN serial, so the sender is read from the topic:
  //   ROOT/dm/<me>/from/<sender>     a private message
  //   ROOT/announce/<teacher>        an announcement
  // The payload is just the text.
  String t = String(topic);
  bool isAnnouncement = t.startsWith(String(TOPIC_ROOT) + "/announce/");
  int slash = t.lastIndexOf('/');
  String fromSerial = (slash >= 0) ? t.substring(slash + 1) : String("");
  if (!isValidSerial(fromSerial)) {
    Serial.println("Ignoring message with no valid sender in the topic.");
    return;
  }
  if (fromSerial == mySerial) {
    Serial.println("Ignoring my own message.");
    return;
  }
  String text = body;
  text.trim();
  if ((int)text.length() > MAX_MESSAGE_LEN) text = text.substring(0, MAX_MESSAGE_LEN);
  if (text.length() == 0) return;
  String fromName = nameFor(fromSerial);
  if (isAnnouncement && !isTeacherSerial(fromSerial)) {
    Serial.println("Ignoring announcement from a non-teacher.");
    return;
  }

  addToInbox(fromSerial, fromName, text, isAnnouncement);
  if (isAnnouncement) {
    announceFrom = labelFor(fromSerial, fromName);
    announceText = text;
    pendingAnnouncement = true;
  } else {
    lastDmFrom = labelFor(fromSerial, fromName);
    lastDmText = text;
    pendingDM = true;
  }
}

/**
 * @brief Non-blocking MQTT reconnect function.
 * This is called by the main loop and will not trap the program.
 */
void reconnect_mqtt() {
  Serial.print("Attempting MQTT connection...");
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  display.println("Connecting to MQTT");
  display.println(MQTT_BROKER);
  display.display();
  
  if (mqttClient.connect(clientID.c_str(), mySerial.c_str(), myPin.c_str())) {
    Serial.println("CONNECTED!");
    display.println("CONNECTED!");
    display.display();
    delay(500); // Short delay is ok to show "CONNECTED"
    
    Serial.print("Subscribing to: "); Serial.println(topicMine);
    Serial.print("Subscribing to: "); Serial.println(topicAnnounce);
    bool subMine = mqttClient.subscribe(topicMine.c_str());
    bool subAll = mqttClient.subscribe(topicAnnounce.c_str());
    if (subMine && subAll) {
      Serial.println("Subscription successful!");
    } else {
      Serial.println("Subscription FAILED!");
    }
    
    updateDisplay(); // Go back to main screen
  } else {
    Serial.print("FAILED, rc=");
    Serial.print(mqttClient.state());
    Serial.println(" try again in 5 seconds");
    
    display.println("FAILED!");
    display.display();
    delay(1000); // Short delay to show "FAILED"
    if (mqttClient.state() == 4 || mqttClient.state() == 5) {   // bad user name / password, or not allowed
      handleBadLogin();
    }
    updateDisplay(); // Go back to main screen
  }
}


void setup() {
  Serial.begin(115200);

  pinMode(MORSE_BUTTON_PIN, INPUT_PULLUP);
  buzzInit();
  pinMode(NAV_PIN, INPUT_PULLUP);

  pinMode(ENC_CLK_PIN, INPUT_PULLUP);
  pinMode(ENC_DT_PIN, INPUT_PULLUP);
  pinMode(WHEEL_CLK_PIN, INPUT_PULLUP);
  pinMode(WHEEL_DT_PIN, INPUT_PULLUP);
  pinMode(WHEEL_SW_PIN, INPUT_PULLUP);
  cursorEnc.state = (digitalRead(ENC_CLK_PIN) << 1) | digitalRead(ENC_DT_PIN);
  wheelEnc.state = (digitalRead(WHEEL_CLK_PIN) << 1) | digitalRead(WHEEL_DT_PIN);
  esp_timer_create_args_t encTimerArgs = {};
  encTimerArgs.callback = &encoderPoll;
  encTimerArgs.name = "encoder";
  esp_timer_handle_t encTimer;
  esp_timer_create(&encTimerArgs, &encTimer);
  esp_timer_start_periodic(encTimer, 1000);   // every 1000 microseconds
  lastEncoderCount = cursorEnc.clicks;
  lastWheelCount = wheelEnc.clicks;

#if USE_SH1106
  if(!display.begin(OLED_I2C_ADDRESS, true)) {
    Serial.println(F("SH1106 init failed"));
    for(;;);
  }
#else
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
#endif
  delay(100); // Give display a moment to init

  // --- NEW: CLEAR SAVED WIFI PROMPT (IMPROVED) ---
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("Hold button");
  display.println("for 3 sec");
  display.println("to change WiFi...");
  display.println("NAV 3s: new serial");
  display.display();

  unsigned long startTime = millis();
  bool clearWiFi = false;
  bool clearSerial = false;
  bool dotState = false;
  long lastDotToggle = 0;
  
  // Wait for 5 seconds now
  while (millis() - startTime < 5000) {
      
      // --- Blinking dot logic ---
      if (millis() - lastDotToggle > 500) {
          dotState = !dotState;
          lastDotToggle = millis();
          display.setCursor(0, 40);
          display.setTextSize(2);
          if (dotState) display.print(".");
          else display.print(" ");
          display.display();
      }
      // --- End blinking dot ---

      if (digitalRead(MORSE_BUTTON_PIN) == LOW) {
          // Button is held
          unsigned long holdStart = millis();
          buzzOn(buzzerFrequency);
          while (digitalRead(MORSE_BUTTON_PIN) == LOW) {
              if (millis() - holdStart > 3000) {
                  clearWiFi = true;
                  break;
              }
          }
          buzzOff();
          if (clearWiFi) break; // Exit the 5-second loop
      }

      if (digitalRead(NAV_PIN) == LOW) {
          // NAV button is held: ask for the serial number again
          unsigned long holdStart = millis();
          while (digitalRead(NAV_PIN) == LOW) {
              if (millis() - holdStart > 3000) {
                  clearSerial = true;
                  break;
              }
          }
          if (clearSerial) break;
      }
  }

  if (clearWiFi) {
      Serial.println("Button held. Clearing saved WiFi.");
      display.clearDisplay();
      display.setCursor(0,0);
      display.setTextSize(1);
      display.println("WiFi Cleared!");
      display.display();
      preferences.begin("morse-wifi", false);
      preferences.clear();
      preferences.end();
      delay(1000);
  }
  // --- END CLEAR WIFI ---

  clientID += String(random(0xffff), HEX);
  Serial.print("My client ID is: ");
  Serial.println(clientID);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(OLED_WHITE);

  // --- NEW: MODE SELECTION ---
  display.setCursor(0,0);
  display.println("SELECT MODE:");
  display.println("");
  display.println("Click . (DOT)");
  display.println("for NORMAL");
  display.println("");
  display.println("Hold - (DASH)");
  display.println("for BLIND");
  display.display();

  bool modeSelected = false;
  while (!modeSelected) {
    int reading = digitalRead(MORSE_BUTTON_PIN);
    
    if (reading == LOW) { // Button is pressed
      unsigned long pressTime = millis();
      buzzOn(buzzerFrequency); // Audio feedback
      
      // Wait for the button to be released
      while (digitalRead(MORSE_BUTTON_PIN) == LOW) {
        delay(10); // small delay
      }
      buzzOff();
      
      unsigned long pressDuration = millis() - pressTime;
      
      // Check if it was a dash (long press)
      if (pressDuration >= dashThreshold) {
        isBlindMode = true;
        modeSelected = true;
        Serial.println("BLIND Mode Selected.");
      } 
      // Check if it was a dot (short press)
      else if (pressDuration > debounceTime) {
        isBlindMode = false;
        modeSelected = true;
        Serial.println("NORMAL Mode Selected.");
      }
    }
  }

  // Show confirmation on display
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Mode Selected:");
  display.setTextSize(2);
  if (isBlindMode) {
    display.println("BLIND");
  } else {
    display.println("NORMAL");
  }
  display.display();
  delay(1500); // Show for 1.5 seconds
  
  // --- END MODE SELECTION ---


  // --- SERIAL NUMBER: asked once, then remembered ---
  ensureSerial(clearSerial);
  lastEncoderCount = cursorEnc.clicks;   // forget any turns made while typing the serial
  lastWheelCount = wheelEnc.clicks;
  String idLine = mySerial;
  if (myName.length() > 0) idLine += " " + myName;

  // --- Original setup continues... ---
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(OLED_WHITE);
  display.setCursor(0,0);
  display.println("Morse Transceiver");
  display.println("ID " + idLine);
  display.display();
  delay(1000);

  // --- NEW: AUTO-CONNECT WIFI ---
  bool wifiSuccess = false;
  preferences.begin("morse-wifi", true); // Start R-Only
  String savedSSID = preferences.getString("ssid", "");
  String savedPass = preferences.getString("pass", "");
  preferences.end();

  if (savedSSID.length() > 0) {
      Serial.println("Found saved WiFi. Trying...");
      display.clearDisplay();
      display.setCursor(0,0);
      display.setTextSize(1);
      display.println("Trying saved WiFi:");
      display.println(savedSSID);
      display.display();
      
      wifiSuccess = connect_to_wifi(savedSSID, savedPass);
      
      if (wifiSuccess) {
          display.println("Connected!");
          display.display();
          delay(1000);
      } else {
          Serial.println("Saved WiFi failed. Clearing.");
          display.println("Failed. Clearing.");
          display.display();
          delay(1000);
          // Clear the bad credentials
          preferences.begin("morse-wifi", false);
          preferences.clear();
          preferences.end();
      }
  }
  // --- END AUTO-CONNECT ---

  // --- MODIFIED WIFI STARTUP ---
  if (!wifiSuccess) {
      Serial.println("Starting interactive WiFi setup...");
      wifiSuccess = interactive_wifi_setup(); // This will try to connect if open

      if (!wifiSuccess && !isEnteringPassword) {
          // This means interactive setup failed and we're not entering a password
          Serial.println("Could not connect to WiFi. Halting.");
          display.clearDisplay();
          display.setCursor(0,0);
          display.println("WIFI FAILED!");
          display.println("Restart to try again.");
          display.display();
          while(true) { delay(100); } // Halt
      }
  }
  // --- END MODIFIED WIFI PART ---
  
  // --- MQTT connection now only happens AFTER WiFi is set up ---
  espClient.setInsecure();   // TODO: pin the broker's root certificate instead (setCACert) - this encrypts but does not verify who answers
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqtt_callback);
  mqttClient.setBufferSize(512);
  clientID = "lumini-" + mySerial;
  topicMine = String(TOPIC_ROOT) + "/dm/" + mySerial + "/from/+";
  topicAnnounce = String(TOPIC_ROOT) + "/announce/+";
  chooseDefaultRecipient();

  initializeMorseMap();
  initializeReverseMorseMap(); 
  
  // Only update to main display if we're not entering a password
  if (!isEnteringPassword) {
      updateDisplay();
  }
  lastReleaseTime = millis();
}

void loop() {
  // --- NEW: Handle WiFi Password Connection ---
  if (isEnteringPassword) {
    
    // --- REMOVED MQTT logic from here ---
    // The device isn't on WiFi yet, so it can't
    // connect to MQTT.

    // This is the new, simplified password loop
    int reading = digitalRead(MORSE_BUTTON_PIN);
    if (reading != lastButtonState) {
      lastDebounceTime = millis();
    }
    lastButtonState = reading;

    if ((millis() - lastDebounceTime) > debounceTime) {
      if (reading != buttonState) { // State change detected
        buttonState = reading;
        
        if (buttonState == LOW) { // --- JUST PRESSED ---
          buttonPressTime = millis();
          letterCommitted = false;
          buzzOn(buzzerFrequency);

        } else { // --- JUST RELEASED ---
          buzzOff();
          unsigned long pressDuration = millis() - buttonPressTime;
          lastReleaseTime = millis(); // Set release time now

          // --- NEW "ON-RELEASE" LOGIC ---
          if (pressDuration >= submitThreshold) { // 5s+ = SUBMIT
            Serial.println("SUBMIT action triggered");
            buzzFor(notifyFrequency, 150); // High beep
            
            if (currentMorse.length() > 0) {
                translateAndClear(); // Commit last letter
            }
            Serial.println("Password entry complete. Attempting to connect...");
            
            bool success = connect_to_wifi(selectedSSID, passwordEntry);
            
            if (success) {
                Serial.println("Password OK! Continuing to main app.");
                isEnteringPassword = false; // Exit password mode!
                updateDisplay(); // Show the main message screen
            } else {
                Serial.println("Password failed. Resetting...");
                display.clearDisplay();
                display.setCursor(0,0);
                display.println("PASSWORD FAILED!");
                display.println("Restarting WiFi...");
                display.display();
                delay(2000);
                ESP.restart(); // Easiest way to retry
            }

          } else if (pressDuration >= deleteThreshold) { // 2s-5s = DELETE
            Serial.println("DELETE action triggered");
            buzzFor(notifyFrequency / 2, 100); // Low beep

            if (currentMorse.length() > 0) {
                currentMorse = ""; // Clear current morse
            } else if (passwordEntry.length() > 0) {
                passwordEntry.remove(passwordEntry.length() - 1); // Delete last char
            }
            updateDisplay();

          } else if (pressDuration >= dashThreshold) { // 0.25s-2s = DASH
            currentMorse += "-";
            updateDisplay();

          } else if (pressDuration > debounceTime) { // 0.05s-0.25s = DOT
            currentMorse += ".";
            updateDisplay(); 
          }
          // else (press < 50ms) was a bounce, ignore.
        }
      }
    }

    // Check for timeouts (only for letter commit)
    if (buttonState == HIGH) { // Button is not pressed
      unsigned long timeSinceRelease = millis() - lastReleaseTime;
      
      // Letter Timeout
      if (timeSinceRelease > letterTimeout && !letterCommitted) {
        if (currentMorse.length() > 0) {
          Serial.println("Letter Timeout (Password)");
          translateAndClear();
          updateDisplay();
        }
        letterCommitted = true;
      }
    }
    
    // We are in password mode, so skip the rest of the main loop
    return; 
  }
  // --- END OF PASSWORD ENTRY LOGIC ---


  // --- Regular Main Loop (if not entering password) ---

  // --- NEW NON-BLOCKING RECONNECT ---
  if (!mqttClient.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) { // Try only every 5 seconds
        lastReconnectAttempt = now;
        reconnect_mqtt();
    }
  }
  // --- END NEW ---
  
  mqttClient.loop(); // <-- THIS IS WHAT RECEIVES MESSAGES
  
  // --- New messages (already stored in the inbox by mqtt_callback) ---
  if (pendingAnnouncement) {
    pendingAnnouncement = false;
    startAnnouncement();
  }
  if (pendingDM) {
    pendingDM = false;
    notifyNewMessage();
  }

  // An announcement takes over the screen until a button is pressed.
  if (announcementShowing) {
    handleAnnouncementDismiss();
    return;
  }

  switch (currentScreen) {
    case SCREEN_COMPOSE: handleComposeScreen(); break;
    case SCREEN_INBOX:   handleInboxScreen();   break;
    case SCREEN_PICK:    handlePickScreen();    break;
    case SCREEN_MENU:    handleMenuScreen();    break;
  }
} // --- END OF loop() ---



void playWinkAnimation() {

  int y = 20;
  // Centered Y
  
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
    display.fillCircle(58, 28, 5, OLED_WHITE); // Left lobe
    display.fillCircle(70, 28, 5, OLED_WHITE);
    // Right lobe
    display.fillTriangle(53, 30, 75, 30, 64, 42, OLED_WHITE); // Point
    display.display();
    delay(250);
    // --- Frame 2: Big Heart ---
    display.clearDisplay();
    display.fillCircle(55, 25, 8, OLED_WHITE);
    // Left lobe
    display.fillCircle(73, 25, 8, OLED_WHITE);
    // Right lobe
    display.fillTriangle(47, 30, 81, 30, 64, 48, OLED_WHITE); // Point
    display.display();
    delay(250);
  }
}

// --- NEW ANIMATION FUNCTIONS ---

void playLaughAnimation() {
  // A simple text-based laughing animation
  int y = 20;
  // Centered Y
  
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
    display.setCursor(33, y);
    // Centered X
    display.print("(v v)");
    display.display();
    delay(200);
  }
  
  delay(500);
  // Hold last frame
}

void playAngryAnimation() {
  // A simple text-based vibrating angry face
  int y = 20;
  // Centered Y
  
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
    display.setCursor(30, y+2);
    // Vibrate down
    display.print("(>O<)");
    display.display();
    delay(150); // <-- This was the typo (was 1G50)
  }
  
  delay(500);
  // Hold last frame
}

void playSurprisedAnimation() {
  // A simple text-based surprised face
  int y = 20;
  // Centered Y
  
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
    display.setCursor(30, y);
    // Centered X
    display.print("(O_O)");
    display.display();
    delay(300);
  }
  
  delay(500);
  // Hold last frame
}

void playSkullAnimation() {
  // A shape-based chattering skull
  
  // Play the animation 5 times
  for (int i = 0; i < 5; i++) {
    // --- Frame 1: Mouth Closed ---
    display.clearDisplay();
    display.fillRoundRect(50, 15, 28, 22, 6, OLED_WHITE); // Cranium
    display.fillRect(52, 37, 24, 8, OLED_WHITE);
    // Jaw
    display.fillCircle(57, 26, 5, OLED_BLACK);           // Left eye
    display.fillCircle(71, 26, 5, OLED_BLACK);
    // Right eye
    display.fillTriangle(62, 32, 66, 32, 64, 37, OLED_BLACK); // Nose
    display.display();
    delay(200);
    // --- Frame 2: Mouth Open ---
    display.clearDisplay();
    display.fillRoundRect(50, 15, 28, 22, 6, OLED_WHITE);
    // Cranium
    display.fillRect(52, 42, 24, 8, OLED_WHITE);
    // Jaw (moved down)
    display.fillCircle(57, 26, 5, OLED_BLACK);
    // Left eye
    display.fillCircle(71, 26, 5, OLED_BLACK);
    // Right eye
    display.fillTriangle(62, 32, 66, 32, 64, 37, OLED_BLACK); // Nose
    display.display();
    delay(200);
  }
}

// ---
// --- NEW BLIND MODE AUDIO FUNCTIONS ---
// ---

/**
 * Populates the reverse map for text-to-morse translation.
 */
void initializeReverseMorseMap() {
    reverseMorseMap['A'] = ".-";   reverseMorseMap['B'] = "-...";
    reverseMorseMap['C'] = "-.-."; reverseMorseMap['D'] = "-..";
    reverseMorseMap['E'] = ".";    reverseMorseMap['F'] = "..-.";
    reverseMorseMap['G'] = "--.";   reverseMorseMap['H'] = "....";
    reverseMorseMap['I'] = "..";   reverseMorseMap['J'] = ".---";
    reverseMorseMap['K'] = "-.-";   reverseMorseMap['L'] = ".-..";
    reverseMorseMap['M'] = "--";   reverseMorseMap['N'] = "-.";
    reverseMorseMap['O'] = "---";   reverseMorseMap['P'] = ".--.";
    reverseMorseMap['Q'] = "--.-"; reverseMorseMap['R'] = ".-.";
    reverseMorseMap['S'] = "...";   reverseMorseMap['T'] = "-";
    reverseMorseMap['U'] = "..-";   reverseMorseMap['V'] = "...-";
    reverseMorseMap['W'] = ".--";   reverseMorseMap['X'] = "-..-";
    reverseMorseMap['Y'] = "-.--"; reverseMorseMap['Z'] = "--..";
    reverseMorseMap['0'] = "-----"; reverseMorseMap['1'] = ".----";
    reverseMorseMap['2'] = "..---"; reverseMorseMap['3'] = "...--";
    reverseMorseMap['4'] = "....-"; reverseMorseMap['5'] = ".....";
    reverseMorseMap['6'] = "-...."; reverseMorseMap['7'] = "--...";
    reverseMorseMap['8'] = "---.."; reverseMorseMap['9'] = "----.";

    // --- NEWLY ADDED ---
    reverseMorseMap['@'] = ".--.-.";
    reverseMorseMap['!'] = "-.-.--";
    reverseMorseMap['?'] = "..--..";
    // NOTE: Lowercase 'a'-'z' are not needed here,
    // we just look up their uppercase version.
}

/**
 * Plays a single morse code sequence (e.g., ".-") on the buzzer.
 */
void playMorseCode(std::string morseCode) {
  Serial.print("Playing: ");
  Serial.println(morseCode.c_str());
  
  for (char c : morseCode) {
    // Play the tone for the dot or dash duration
    if (c == '.') {
      buzzOn(buzzerFrequency);
      delay(dotDuration);
    } else if (c == '-') {
      buzzOn(buzzerFrequency);
      delay(dashDuration);
    }
    buzzOff(); // Stop the tone
    
    // Wait for the pause between elements
    delay(interElementPause);
  }
}

/**
 * Translates a text string into morse audio.
 */
void playMorseString(String message) {
  // message.toUpperCase(); // <-- REMOVED to support lowercase
  
  for (int i = 0; i < message.length(); i++) {
    char c = message.charAt(i);

    if (c == ' ') {
      // It's a space, wait for the word pause
      delay(interWordPause - interLetterPause);
      continue; // Skip to next character
    }
    
    // Special handling for @ and !
    if (c == '@' || c == '!') {
      if (reverseMorseMap.count(c)) {
        playMorseCode(reverseMorseMap[c]);
        delay(interLetterPause - interElementPause);
      }
      continue; // Skip to next character
    }
    
    // Convert to uppercase for map lookup
    // This handles both 'a' and 'A' as ".-"
    char lookupChar = toupper(c);

    if (reverseMorseMap.count(lookupChar)) {
      // It's a letter or number, find it and play it
      playMorseCode(reverseMorseMap[lookupChar]);
      
      // Wait for the pause between letters
      delay(interLetterPause - interElementPause);
    }
    // Any other character not in the map is ignored.
  }
}

// --- END NEW BLIND MODE FUNCTIONS ---


// ---
// --- Morse & Display Functions (Unchanged)
// ---

bool sendMessage() {
  String text = translatedMessage;
  text.trim();
  if (text.length() == 0) {
    Serial.println("Message is empty, not sending.");
    return false;
  }
  if (recipientSerial.length() == 0) {
    Serial.println("No recipient chosen, not sending.");
    return false;
  }
  if (recipientSerial == SEND_TO_ALL && !isTeacherSerial(mySerial)) {
    Serial.println("Only teachers can send announcements.");
    return false;
  }

  String topic = (recipientSerial == SEND_TO_ALL)
                   ? String(TOPIC_ROOT) + "/announce/" + mySerial
                   : String(TOPIC_ROOT) + "/dm/" + recipientSerial + "/from/" + mySerial;
  String payload = text;   // the broker checks that the topic ends in my serial; the receiver reads the sender from the topic

  Serial.print("Publishing to "); Serial.print(topic);
  Serial.print(": "); Serial.println(payload);
  if (mqttClient.publish(topic.c_str(), payload.c_str())) {
    Serial.println("Publish SUCCESS");
    return true;
  } else {
    Serial.println("Publish FAILED!");
    return false;
  }
}

void initializeMorseMap() {
    // Note: This map MUST output uppercase A-Z
    // The case conversion is handled in translateAndClear()
    morseMap[".-"] = "A"; morseMap["-..."] = "B"; morseMap["-.-."] = "C";
    morseMap["-.."] = "D";
    morseMap["."] = "E"; morseMap["..-."] = "F";
    morseMap["--."] = "G"; morseMap["...."] = "H"; morseMap[".."] = "I";
    morseMap[".---"] = "J";
    morseMap["-.-"] = "K"; morseMap[".-.."] = "L";
    morseMap["--"] = "M"; morseMap["-."] = "N"; morseMap["---"] = "O";
    morseMap[".--."] = "P";
    morseMap["--.-"] = "Q"; morseMap[".-."] = "R";
    morseMap["..."] = "S"; morseMap["-"] = "T"; morseMap["..-"] = "U";
    morseMap["...-"] = "V";
    morseMap[".--"] = "W"; morseMap["-..-"] = "X";
    morseMap["-.--"] = "Y"; morseMap["--.."] = "Z";
    morseMap["-----"] = "0"; morseMap[".----"] = "1";
    morseMap["..---"] = "2";
    morseMap["...--"] = "3"; morseMap["....-"] = "4"; 
    morseMap["....."] = "5";
    morseMap["-...."] = "6"; morseMap["--..."] = "7";
    morseMap["---.."] = "8";
    morseMap["----."] = "9";

    morseMap[".--.-."] = "@";
    morseMap["-.-.--"] = "!";
    
    // --- NEW: CASE TOGGLE ---
    morseMap["..--"] = "TOGGLE_CASE"; // "UM"
    
    morseMap["-.-.-"] = "OK"; morseMap["----"] = "YES";
    morseMap["...-."] = "NO"; morseMap[".--.--"] = "HELP";
    morseMap["..--.-"] = "WAIT";
    morseMap["-...--"] = "DONE"; morseMap["---..."] = "COME"; morseMap["--...-"] = "GO";
    morseMap[".-..--"] = "READY";
    // ---
    // --- EMOJI KEYWORDS (NOW WITH MORE EMOJIS) ---
    // ---
    Serial.println("Loading custom emoji codes...");
    morseMap[".-.-."] = "EMOJI_WINK";  // (A, R)
    morseMap[".-..-."] = "EMOJI_HEART";
    // (A, L, F)
    
    // --- NEW EMOJI CODES ---
    morseMap["...-.."] = "EMOJI_LAUGH";
    // (S, L)
    morseMap["-..-"] = "EMOJI_ANGRY";     // (X)
    morseMap["---..."] = "EMOJI_SURPRISED";
    // (O, S)
    morseMap["...-.-"] = "EMOJI_SKULL";     // (S, K)
    
}

/**
 * @brief MODIFIED to handle password entry and case toggling.
 * Translates the current morse buffer and adds it to the
 * correct string (passwordEntry or translatedMessage).
 */
void translateAndClear() {
  if (currentMorse.length() > 0) {
    std::string currentMorseStd = currentMorse.c_str();
    String translatedChar = "?"; // Default to ?
    
    if (morseMap.count(currentMorseStd)) {
      translatedChar = morseMap[currentMorseStd];
    }

    // --- NEW CASE LOGIC ---
    if (translatedChar == "TOGGLE_CASE") {
      isUppercase = !isUppercase;
      Serial.print("Case toggled. isUppercase = "); Serial.println(isUppercase);
      currentMorse = "";
      return; // Don't add any text
    }

    // Check if it's a letter (A-Z) and convert case if needed
    if (translatedChar.length() == 1 && isalpha(translatedChar.charAt(0))) {
      if (!isUppercase) {
        translatedChar.toLowerCase();
      }
    }
    // --- END NEW CASE LOGIC ---


    // --- NEW LOGIC ---
    if (isEnteringPassword) {
      // Don't add Emojis or special commands to password
      if (translatedChar.length() == 1) { // This will now include "A", "a", "1", "@", "!"
        passwordEntry += translatedChar;
      } else {
        passwordEntry += "?"; // Can't add "OK" or "EMOJI" to pw
      }
    } else {
      // This is the original logic
      translatedMessage = translatedMessage.substring(0, cursorPos) + translatedChar + translatedMessage.substring(cursorPos);
      cursorPos += translatedChar.length();
    }
    // --- END NEW LOGIC ---

    currentMorse = "";
  }
}

/**
 * @brief MODIFIED to show the password entry screen and case state.
 */
void updateDisplay() {
  display.clearDisplay();

  // The inbox, the "send to" list and announcements have their own screens.
  if (!isEnteringPassword) {
    if (announcementShowing)               { drawAnnouncement(); display.display(); return; }
    if (currentScreen == SCREEN_INBOX)     { drawInbox();        display.display(); return; }
    if (currentScreen == SCREEN_PICK)      { drawPicker();       display.display(); return; }
    if (currentScreen == SCREEN_MENU)      { drawMenu();         display.display(); return; }
  }

  display.setTextSize(1);
  display.setCursor(0, 0);
  if (isEnteringPassword) {
    display.print(isUppercase ? "CASE: UPPER" : "CASE: lower");
  } else {
    // Header: who the message goes to, case, and how many unread messages
    String to = "To:" + recipientLabel();
    if (to.length() > 11) to = to.substring(0, 11);
    display.print(to);
    display.setCursor(74, 0);
    display.print(isUppercase ? "UPPER" : "lower");
    int unread = unreadCount();
    if (unread > 0) {
      display.setCursor(104, 0);
      display.print("IN:");
      display.print(unread > 9 ? 9 : unread);
    }
  }


  // --- NEW: Show Password Screen if active ---
  if (isEnteringPassword) {
    display.setCursor(0, 10); // Move down
    display.println("Enter Password for:");
    display.println(selectedSSID.substring(0, 20)); // Show SSID
    display.drawFastHLine(0, 28, 128, OLED_WHITE);

    display.setCursor(0, 32);
    display.setTextSize(2);
    display.print(">");
    display.println(currentMorse); // Show current morse (e.g., ".-")

    // Show asterisks for the password
    String asterisks = "";
    for (int i = 0; i < passwordEntry.length(); i++) {
        asterisks += "*";
    }
    
    display.setTextSize(1);
    display.setCursor(0, 48);
    display.print("Pass:");
    display.setTextSize(2);
    //display.setTextWrap(true); // Don't wrap password
    display.print(asterisks);

  } else {
    // --- This is the ORIGINAL display logic ---
    display.setCursor(0, 10); // Move down
    display.setTextSize(2);
    // Letter wheel: previous letter, [selected letter], next letter
    display.setCursor(22, 10);
    display.print(wheelChar(wheelIndex - 1));
    display.print(" [");
    display.print(wheelChar(wheelIndex));
    display.print("] ");
    display.print(wheelChar(wheelIndex + 1));

    display.setTextSize(1);
    String sug = suggestionAtCursor();   // an abbreviation was typed: offer the full phrase
    if (sug.length() > 0) {
      if (sug.length() > 19) sug = sug.substring(0, 17) + "..";
      display.setCursor(0, 26);
      display.print("> ");
      display.print(sug);
    }
    display.println("");
    display.setCursor(0, 34); // Move down
    display.print("Message:");
    if (translatedMessage.length() > 0) display.print(" both=SEND");
    
    // One line, scrolled so the cursor is always on screen (10 characters fit at size 2).
    String before = toDisplayText(translatedMessage.substring(0, cursorPos));
    String shown  = before + toDisplayText(translatedMessage.substring(cursorPos));
    int cur = before.length();
    const int VISIBLE = 10;
    if (cur < viewStart) viewStart = cur;
    if (cur > viewStart + VISIBLE) viewStart = cur - VISIBLE;

    display.setTextSize(2);
    display.setTextWrap(false);
    display.setCursor(0, 44);
    display.print(shown.substring(viewStart, viewStart + VISIBLE));
    display.setTextWrap(true);

    int cx = (cur - viewStart) * 12 - 1;     // cursor bar sits between two letters
    if (cx < 0) cx = 0;
    if (cx > SCREEN_WIDTH - 1) cx = SCREEN_WIDTH - 1;
    display.drawFastVLine(cx, 43, 17, OLED_WHITE);
  }
  // --- END NEW LOGIC ---

  display.display();
}

// ---
// --- Rotary encoder / cursor
// ---

// An emoji keyword (e.g. "EMOJI_WINK") is stored as one long word. The cursor treats
// each one as a single letter so it never stops in the middle of it.
const char* EMOJI_WORDS[] = {"EMOJI_WINK", "EMOJI_HEART", "EMOJI_LAUGH",
                             "EMOJI_ANGRY", "EMOJI_SURPRISED", "EMOJI_SKULL"};

int unitLenAt(int i) {
  for (const char* w : EMOJI_WORDS) {
    if (translatedMessage.startsWith(w, i)) return strlen(w);
  }
  return 1;
}

int prevUnitStart(int pos) {
  int i = 0, prev = 0;
  while (i < pos) {
    prev = i;
    i += unitLenAt(i);
  }
  return prev;
}

void moveCursor(int dir) {
  int len = translatedMessage.length();
  if (dir < 0 && cursorPos > 0) {
    cursorPos = prevUnitStart(cursorPos);
  } else if (dir > 0 && cursorPos < len) {
    cursorPos += unitLenAt(cursorPos);
  }
  if (cursorPos > len) cursorPos = len;
}

void handleEncoder() {
  long count = cursorEnc.clicks;
  long steps = count - lastEncoderCount;
  if (steps == 0) return;
  lastEncoderCount = count;
  if (ENC_REVERSE) steps = -steps;

  if (currentMorse.length() > 0) {
    translateAndClear();      // finish the letter being keyed before moving
  }

  int oldPos = cursorPos;
  int dir = (steps > 0) ? 1 : -1;
  for (long n = labs(steps); n > 0; n--) {
    moveCursor(dir);
  }

  lastReleaseTime = millis(); // turning counts as activity
  letterCommitted = true;
  Serial.print("Encoder steps: "); Serial.print(steps);
  Serial.print("  cursor: "); Serial.print(cursorPos);
  Serial.print(" / "); Serial.println(translatedMessage.length());
  if (cursorPos != oldPos) updateDisplay();
}

// Replaces emoji keywords with short labels for the screen.
String toDisplayText(String s) {
  s.replace("EMOJI_WINK", "[WINK]");
  s.replace("EMOJI_HEART", "[<3]");
  s.replace("EMOJI_LAUGH", "[HAHA]");
  s.replace("EMOJI_ANGRY", "[ANGRY]");
  s.replace("EMOJI_SURPRISED", "[SURPRISED]");
  s.replace("EMOJI_SKULL", "[SKULL]");
  return s;
}

// Sends the whole message. Called when the button is held for 5 seconds.
void sendCurrentMessage() {
  if (currentMorse.length() > 0) {
    translateAndClear();   // finish the letter being keyed
  }
  if (translatedMessage.length() == 0) {
    buzzFor(notifyFrequency / 2, 100);   // low beep: nothing to send
    updateDisplay();
    return;
  }

  // Add a space ONLY if there's a message and it doesn't end with a space/special char
  if (!translatedMessage.endsWith(" ") && !translatedMessage.endsWith(String(char(0)))) {
    translatedMessage += " ";
    cursorPos = translatedMessage.length();
  }

  Serial.println("Held 5s... SENDING");
  bool sentOK = sendMessage();

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print(sentOK ? "SENT!" : "FAILED!");
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.print("To: ");
  display.print(recipientLabel());
  display.display();
  buzzFor(notifyFrequency, 150);
  delay(700);

  if (sentOK) {
    translatedMessage = "";
    cursorPos = 0;
    viewStart = 0;
  }
  currentMorse = "";
  letterCommitted = true;
  wordCommitted = true;
  lastReleaseTime = millis();
  updateDisplay();
}

// ---
// --- Typing wheel (right knob) and editing (left knob button)
// ---

// The letter at wheel position i (wraps around), in the current case.
char wheelChar(int i) {
  char c = WHEEL_CHARS[((i % WHEEL_LEN) + WHEEL_LEN) % WHEEL_LEN];
  if (!isUppercase && isalpha(c)) c = tolower(c);
  return c;
}

void insertAtCursor(const String& s) {
  if ((int)translatedMessage.length() + (int)s.length() > MAX_MESSAGE_LEN) {
    buzzFor(notifyFrequency / 2, 100);   // low beep: message is full
    return;
  }
  translatedMessage = translatedMessage.substring(0, cursorPos) + s + translatedMessage.substring(cursorPos);
  cursorPos += s.length();
  updateDisplay();
}

void insertSpace() {
  if (acceptSuggestion()) return;                              // abbreviation typed: space expands it
  if (cursorPos == 0) return;                                  // no leading space
  if (translatedMessage.charAt(cursorPos - 1) == ' ') return;  // no double space
  insertAtCursor(" ");
}

// Removes the letter just before the cursor.
void deleteBeforeCursor() {
  if (undoExpansion()) return;                  // right after an expansion: give the abbreviation back
  buzzFor(notifyFrequency / 2, 100);
  if (cursorPos <= 0) return;
  int start = prevUnitStart(cursorPos);
  translatedMessage.remove(start, cursorPos - start);
  cursorPos = start;
  updateDisplay();
}

// Right knob turned: move to the next or previous letter on the wheel.
void handleWheel() {
  long count = wheelEnc.clicks;
  long steps = count - lastWheelCount;
  if (steps == 0) return;
  lastWheelCount = count;
  if (WHEEL_REVERSE) steps = -steps;

  wheelIndex = (int)(((wheelIndex + steps) % WHEEL_LEN + WHEEL_LEN) % WHEEL_LEN);
  Serial.print("Wheel: "); Serial.println(wheelChar(wheelIndex));
  updateDisplay();
}

// Right knob pressed: short = insert the letter shown, long = switch case.
void handleWheelButton() {
  static int lastReading = HIGH;
  static int stableState = HIGH;
  static unsigned long lastChange = 0;
  static unsigned long pressedAt = 0;

  int reading = digitalRead(WHEEL_SW_PIN);
  if (reading != lastReading) {
    lastChange = millis();
    lastReading = reading;
  }
  if ((millis() - lastChange) > debounceTime && reading != stableState) {
    stableState = reading;
    if (stableState == LOW) {
      pressedAt = millis();
    } else if (wheelIgnoreRelease) {
      wheelIgnoreRelease = false;               // this release ended a "send" chord
    } else {
      if (millis() - pressedAt >= WHEEL_LONG_PRESS_MS) {
        isUppercase = !isUppercase;
        buzzFor(1500, 60);
        updateDisplay();
      } else {
        buzzFor(1000, 30);
        insertAtCursor(String(wheelChar(wheelIndex)));
      }
    }
  }
}

// ---
// --- Text shortcuts
// ---

// The word just before the cursor, if the cursor sits at the end of it
// (end of the message, or the next character is a space).
bool wordBeforeCursor(int& start, String& word) {
  int len = translatedMessage.length();
  if (cursorPos < len && translatedMessage.charAt(cursorPos) != ' ') return false;
  start = cursorPos;
  while (start > 0 && translatedMessage.charAt(start - 1) != ' ') start--;
  word = translatedMessage.substring(start, cursorPos);
  return word.length() > 0;
}

// The full phrase for an abbreviation, in the case the abbreviation was typed in
// (WYD gives WHAT ARE YOU DOING?, Wyd gives What are you doing?, wyd gives what are you doing?).
// Returns "" if the word is not a shortcut.
String phraseFor(const String& word) {
  for (const Shortcut& sc : SHORTCUTS) {
    if (word.equalsIgnoreCase(sc.key)) {
      String phrase = sc.text;
      bool allUpper = true;
      for (unsigned int i = 0; i < word.length(); i++) {
        if (islower(word.charAt(i))) allUpper = false;
      }
      if (allUpper) {
        phrase.toUpperCase();
      } else if (isupper(word.charAt(0))) {
        phrase.setCharAt(0, toupper(phrase.charAt(0)));
      }
      return phrase;
    }
  }
  return "";
}

String suggestionAtCursor() {
  int start;
  String word;
  if (!wordBeforeCursor(start, word)) return "";
  return phraseFor(word);
}

// Replaces the abbreviation before the cursor with its phrase and a space.
// Returns false if there is nothing to expand.
bool acceptSuggestion() {
  int start;
  String word;
  if (!wordBeforeCursor(start, word)) return false;
  String phrase = phraseFor(word);
  if (phrase.length() == 0) return false;

  int newLen = (int)translatedMessage.length() - (int)word.length() + (int)phrase.length() + 1;
  if (newLen > MAX_MESSAGE_LEN) {
    buzzFor(notifyFrequency / 2, 100);   // too long to fit: low beep
    return true;                                  // and don't add a space
  }

  String head = translatedMessage.substring(0, start);
  String tail = translatedMessage.substring(cursorPos);
  bool addSpace = !(tail.length() > 0 && tail.charAt(0) == ' ');
  translatedMessage = head + phrase + (addSpace ? " " : "") + tail;
  cursorPos = start + phrase.length() + 1;

  expUndo.valid = true;
  expUndo.start = start;
  expUndo.end = cursorPos;
  expUndo.phrase = phrase + " ";
  expUndo.restore = word + (addSpace ? "" : " ");

  buzzFor(1200, 80);
  updateDisplay();
  return true;
}

// Puts the abbreviation back, but only if nothing has changed since the expansion.
bool undoExpansion() {
  if (!expUndo.valid) return false;
  expUndo.valid = false;
  if (cursorPos != expUndo.end) return false;
  if (translatedMessage.substring(expUndo.start, expUndo.end) != expUndo.phrase) return false;

  translatedMessage = translatedMessage.substring(0, expUndo.start) + expUndo.restore
                      + translatedMessage.substring(expUndo.end);
  cursorPos = expUndo.start + expUndo.restore.length();
  if (cursorPos > 0 && translatedMessage.charAt(cursorPos - 1) == ' ') cursorPos--;  // sit after the word
  buzzFor(notifyFrequency / 2, 100);
  updateDisplay();
  return true;
}

// ---
// --- Who we are talking to
// ---

String recipientLabel() {
  if (recipientSerial == SEND_TO_ALL) return "ALL";
  if (recipientSerial.length() == 0) return "?";
  return labelFor(recipientSerial, recipientName);
}

// The first contact that is not this device.
void chooseDefaultRecipient() {
  recipientSerial = "";
  recipientName = "";
  for (int i = 0; i < DIRECTORY_SIZE; i++) {
    if (mySerial != DIRECTORY[i].serial) {
      recipientSerial = DIRECTORY[i].serial;
      recipientName = DIRECTORY[i].name;
      break;
    }
  }
}

// ---
// --- Inbox
// ---

void addToInbox(const String& fromSerial, const String& fromName, const String& text, bool announcement) {
  for (int i = INBOX_MAX - 1; i > 0; i--) {   // shift older messages down, the oldest falls off
    inbox[i] = inbox[i - 1];
  }
  inbox[0].fromSerial = fromSerial;
  inbox[0].fromName = fromName;
  inbox[0].text = text;
  inbox[0].unread = true;
  inbox[0].announcement = announcement;
  if (inboxCount < INBOX_MAX) inboxCount++;

  // If the inbox is open, keep looking at the same message.
  if (currentScreen == SCREEN_INBOX && inboxCount > 1 && inboxIndex < INBOX_MAX - 1) inboxIndex++;
  if (inboxIndex >= inboxCount) inboxIndex = inboxCount - 1;
}

int unreadCount() {
  int n = 0;
  for (int i = 0; i < inboxCount; i++) {
    if (inbox[i].unread) n++;
  }
  return n;
}

void replyToSelectedMessage() {
  if (inboxCount == 0) return;
  recipientSerial = inbox[inboxIndex].fromSerial;
  recipientName = inbox[inboxIndex].fromName.length() > 0 ? inbox[inboxIndex].fromName : nameFor(recipientSerial);
  switchScreen(SCREEN_COMPOSE);
}

void deleteSelectedMessage() {
  if (inboxCount == 0) return;
  for (int i = inboxIndex; i < inboxCount - 1; i++) {
    inbox[i] = inbox[i + 1];
  }
  inboxCount--;
  if (inboxIndex >= inboxCount) inboxIndex = inboxCount - 1;
  if (inboxIndex < 0) inboxIndex = 0;
  if (inboxCount > 0) inbox[inboxIndex].unread = false;
  buzzFor(notifyFrequency / 2, 100);
  updateDisplay();
}

// A new direct message arrived: two beeps and a refreshed unread count. It is not shown until the inbox is opened.
void notifyNewMessage() {
  buzzFor(notifyFrequency, 100);
  delay(150);
  buzzFor(notifyFrequency, 100);
  if (isBlindMode) {   // no screen to read: say who it is from, then the message, in Morse
    delay(300);
    playMorseString(lastDmFrom.substring(0, lastDmFrom.indexOf(' ')));
    delay(interWordPause);
    playMorseString(lastDmText);
  }
  updateDisplay();
}

// ---
// --- Announcements (from teachers, shown at once on every device)
// ---

void alertBeeps() {
  for (int i = 0; i < 3; i++) {
    buzzFor(1800, 150);
    delay(220);
  }
  lastAlertBeep = millis();
}

void startAnnouncement() {
  announcementShowing = true;
  announcementStart = millis();
  if (inboxCount > 0) inbox[0].unread = false;   // it is being shown now
  alertBeeps();
  updateDisplay();
  if (isBlindMode) playMorseString(announceText);
}

// Any button dismisses the announcement. It beeps again every 10 seconds until then.
void handleAnnouncementDismiss() {
  lastEncoderCount = cursorEnc.clicks;   // ignore knob turns
  lastWheelCount = wheelEnc.clicks;

  if (millis() - lastAlertBeep > 10000) {
    alertBeeps();
  }
  if (millis() - announcementStart < 500) return;   // ignore an accidental press at the start

  bool pressed = digitalRead(MORSE_BUTTON_PIN) == LOW || digitalRead(WHEEL_SW_PIN) == LOW
                 || digitalRead(NAV_PIN) == LOW;
  if (!pressed) return;

  unsigned long waitStart = millis();   // wait for release so this press is not used again
  while ((digitalRead(MORSE_BUTTON_PIN) == LOW || digitalRead(WHEEL_SW_PIN) == LOW
          || digitalRead(NAV_PIN) == LOW) && millis() - waitStart < 3000) {
    delay(10);
  }
  buttonState = HIGH;
  lastButtonState = HIGH;
  announcementShowing = false;
  updateDisplay();
}

// ---
// --- Buttons
// ---

// Left knob push switch (raw). Reports a press when the button is released, and sounds the
// high "release now to confirm" tone at 5 s on the serial entry screen. Use pollLeftGesture()
// to also get double presses.
LeftEvent pollLeftButton() {
  LeftEvent ev = LEFT_NONE;
  int reading = digitalRead(MORSE_BUTTON_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  lastButtonState = reading;
  if ((millis() - lastDebounceTime) > debounceTime && reading != buttonState) {
    buttonState = reading;
    if (buttonState == LOW) {
      buttonPressTime = millis();
      sendArmed = false;
      deleteArmed = false;
      buzzOn(buzzerFrequency);
    } else {
      buzzOff();
      unsigned long held = millis() - buttonPressTime;
      if (held >= submitThreshold) ev = LEFT_HOLD5;
      else if (held >= deleteThreshold) ev = LEFT_HOLD2;
      else ev = LEFT_SHORT;
    }
  }
  if (confirmHoldCue && buttonState == LOW && !sendArmed && (millis() - buttonPressTime) >= submitThreshold) {
    sendArmed = true;
    buzzOn(notifyFrequency);
  }
  return ev;
}

// Left knob with double-press detection:
//   LEFT_SHORT  = one press (reported after DOUBLE_PRESS_MS, once it is clear no second press follows)
//   LEFT_DOUBLE = two quick presses
//   LEFT_HOLD5  = held for 5 seconds (only the serial entry screen uses it, to confirm)
LeftEvent pollLeftGesture() {
  LeftEvent raw = pollLeftButton();
  if (ignoreLeftRelease && raw != LEFT_NONE) {   // this release ended a "send" chord
    ignoreLeftRelease = false;
    leftPending = false;
    return LEFT_NONE;
  }
  unsigned long now = millis();
  if (raw == LEFT_HOLD5) {
    leftPending = false;
    return LEFT_HOLD5;
  }
  if (raw == LEFT_SHORT || raw == LEFT_HOLD2) {   // a press just ended
    if (leftPending && now - leftPendingAt <= DOUBLE_PRESS_MS) {
      leftPending = false;
      return LEFT_DOUBLE;
    }
    leftPending = true;
    leftPendingAt = now;
    return LEFT_NONE;
  }
  if (leftPending && now - leftPendingAt > DOUBLE_PRESS_MS) {
    leftPending = false;
    return LEFT_SHORT;
  }
  return LEFT_NONE;
}

// NAV button: short or long press, reported on release.
NavEvent pollNavButton() {
  static int lastReading = HIGH;
  static int stableState = HIGH;
  static unsigned long lastChange = 0;
  static unsigned long pressedAt = 0;
  NavEvent ev = NAV_NONE;

  int reading = digitalRead(NAV_PIN);
  if (reading != lastReading) {
    lastChange = millis();
    lastReading = reading;
  }
  if ((millis() - lastChange) > debounceTime && reading != stableState) {
    stableState = reading;
    if (stableState == LOW) {
      pressedAt = millis();
    } else {
      ev = (millis() - pressedAt >= NAV_LONG_PRESS_MS) ? NAV_LONG : NAV_SHORT;
    }
  }
  return ev;
}

// ---
// --- Screens
// ---

void switchScreen(Screen next) {
  currentScreen = next;
  leftPending = false;
  lastEncoderCount = cursorEnc.clicks;   // forget any turns made on the old screen
  lastWheelCount = wheelEnc.clicks;
  if (next == SCREEN_INBOX) {
    if (inboxIndex >= inboxCount) inboxIndex = inboxCount - 1;
    if (inboxIndex < 0) inboxIndex = 0;
    if (inboxCount > 0) inbox[inboxIndex].unread = false;
  }
  updateDisplay();
}

void enterPicker() {
  pickSerials.clear();
  pickNames.clear();
  if (isTeacherSerial(mySerial)) {   // teachers can also announce to everyone
    pickSerials.push_back(SEND_TO_ALL);
    pickNames.push_back("ALL (announce)");
  }
  for (int i = 0; i < DIRECTORY_SIZE; i++) {
    if (mySerial == DIRECTORY[i].serial) continue;
    pickSerials.push_back(String(DIRECTORY[i].serial));
    pickNames.push_back(labelFor(DIRECTORY[i].serial, DIRECTORY[i].name));
  }
  pickIndex = 0;
  for (int i = 0; i < (int)pickSerials.size(); i++) {
    if (pickSerials[i] == recipientSerial) pickIndex = i;
  }
  switchScreen(SCREEN_PICK);
}

void handleComposeScreen() {
  if (pollNavButton() != NAV_NONE) { openMenu(); return; }   // NAV opens the menu

  checkSendChord();        // both knobs pressed together = send
  handleEncoder();         // left knob: move the cursor
  handleWheel();           // right knob: scroll letters
  handleWheelButton();     // right knob press: insert letter / switch case

  LeftEvent ev = pollLeftGesture();
  if (ev == LEFT_DOUBLE) deleteBeforeCursor();   // double press = delete the letter before the cursor
  else if (ev == LEFT_SHORT) insertSpace();      // single press = space (or accept a shortcut)
}

void handleInboxScreen() {
  NavEvent nav = pollNavButton();
  if (nav != NAV_NONE) { openMenu(); return; }

  lastWheelCount = wheelEnc.clicks;   // right knob is not used here

  long count = cursorEnc.clicks;
  long steps = count - lastEncoderCount;
  if (steps != 0) {
    lastEncoderCount = count;
    if (ENC_REVERSE) steps = -steps;
    int target = inboxIndex + (int)steps;       // turn right = older messages
    if (target > inboxCount - 1) target = inboxCount - 1;
    if (target < 0) target = 0;
    inboxIndex = target;
    if (inboxCount > 0) inbox[inboxIndex].unread = false;
    updateDisplay();
  }

  LeftEvent ev = pollLeftGesture();
  if (ev == LEFT_SHORT) replyToSelectedMessage();        // press = reply to the sender
  else if (ev == LEFT_DOUBLE) deleteSelectedMessage();   // double press = delete this message
}

void handlePickScreen() {
  NavEvent nav = pollNavButton();
  if (nav != NAV_NONE) { openMenu(); return; }

  lastWheelCount = wheelEnc.clicks;   // right knob is not used here

  int n = pickSerials.size();
  long count = cursorEnc.clicks;
  long steps = count - lastEncoderCount;
  if (steps != 0) {
    lastEncoderCount = count;
    if (ENC_REVERSE) steps = -steps;
    if (n > 0) {
      pickIndex = (int)(((pickIndex + steps) % n + n) % n);
      updateDisplay();
    }
  }

  LeftEvent ev = pollLeftGesture();
  if (ev == LEFT_SHORT && n > 0) {      // short press = choose this contact
    recipientSerial = pickSerials[pickIndex];
    if (recipientSerial == SEND_TO_ALL) {
      recipientName = "ALL";
    } else {
      recipientName = nameFor(recipientSerial);
    }
    switchScreen(SCREEN_COMPOSE);
  }
}

// ---
// --- Drawing the other screens
// ---

void drawAnnouncement() {
  display.setTextSize(1);
  display.setTextWrap(true);
  display.fillRect(0, 0, SCREEN_WIDTH, 11, OLED_WHITE);
  display.setTextColor(OLED_BLACK);
  display.setCursor(2, 2);
  display.print("ANNOUNCEMENT");
  display.setTextColor(OLED_WHITE);
  display.setCursor(0, 13);
  display.print("From ");
  display.print(announceFrom);
  display.setCursor(0, 24);
  display.print(announceText);
}

void drawInbox() {
  display.setTextSize(1);
  display.setTextWrap(true);
  display.setCursor(0, 0);
  display.print("INBOX");
  if (inboxCount == 0) {
    display.setCursor(0, 28);
    display.print("No messages yet.");
    display.setCursor(0, 56);
    display.print("NAV = menu");
    return;
  }
  display.print(" ");
  display.print(inboxIndex + 1);
  display.print("/");
  display.print(inboxCount);
  display.drawFastHLine(0, 9, SCREEN_WIDTH, OLED_WHITE);

  InboxItem& m = inbox[inboxIndex];
  display.setCursor(0, 12);
  display.print(m.announcement ? "ANNOUNCE " : "From ");
  display.print(labelFor(m.fromSerial, m.fromName));

  String text = m.text;
  if (text.length() > 84) text = text.substring(0, 82) + "..";   // four lines fit
  display.setCursor(0, 22);
  display.print(text);

  display.setCursor(0, 56);
  display.print("click=reply 2x=del");
}

void drawPicker() {
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setCursor(0, 0);
  display.print("Send to:");
  display.drawFastHLine(0, 9, SCREEN_WIDTH, OLED_WHITE);

  const int maxLines = 5;
  int n = pickSerials.size();
  if (n == 0) {
    display.setCursor(0, 24);
    display.print("No contacts yet.");
  }
  int startIdx = (pickIndex >= maxLines) ? pickIndex - maxLines + 1 : 0;
  for (int i = startIdx; i < n && (i - startIdx) < maxLines; i++) {
    display.setCursor(0, 12 + (i - startIdx) * 9);
    display.print(i == pickIndex ? "> " : "  ");
    display.print(pickNames[i]);
  }
  display.setCursor(0, 56);
  display.print("click=pick NAV=menu");
  display.setTextWrap(true);
}

// ---
// --- Buzzer: PWM instead of tone(), so the volume can be lowered with BUZZER_VOLUME
// ---

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define BUZZ_NEW_LEDC 1
#else
  #define BUZZ_NEW_LEDC 0
  const int BUZZ_CHANNEL = 4;   // spare LEDC channel (older ESP32 Arduino core)
#endif
esp_timer_handle_t buzzTimer;

void buzzStopCallback(void* arg) {
  buzzOff();
}

void buzzInit() {
#if BUZZ_NEW_LEDC
  ledcAttach(BUZZER_PIN, 1000, 8);
  ledcWrite(BUZZER_PIN, 0);
#else
  ledcSetup(BUZZ_CHANNEL, 1000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZ_CHANNEL);
  ledcWrite(BUZZ_CHANNEL, 0);
#endif
  esp_timer_create_args_t args = {};
  args.callback = &buzzStopCallback;
  args.name = "buzz";
  esp_timer_create(&args, &buzzTimer);
}

// Starts a tone that keeps going until buzzOff().
void buzzOn(int freq) {
  esp_timer_stop(buzzTimer);                      // cancel any pending stop
  uint32_t duty = (128UL * BUZZER_VOLUME) / 100;  // 128 of 255 (50%) is the loudest
#if BUZZ_NEW_LEDC
  ledcChangeFrequency(BUZZER_PIN, freq, 8);
  ledcWrite(BUZZER_PIN, duty);
#else
  ledcWriteTone(BUZZ_CHANNEL, freq);
  ledcWrite(BUZZ_CHANNEL, duty);
#endif
}

void buzzOff() {
#if BUZZ_NEW_LEDC
  ledcWrite(BUZZER_PIN, 0);
#else
  ledcWrite(BUZZ_CHANNEL, 0);
#endif
}

// A tone that stops by itself after ms milliseconds, without blocking.
void buzzFor(int freq, int ms) {
  buzzOn(freq);
  esp_timer_start_once(buzzTimer, (uint64_t)ms * 1000);
}

// ---
// --- Serial numbers
// ---

// Students: digits (47).  Teachers: T and digits (T33).  Up to 6 characters.
bool isValidSerial(const String& s) {
  int n = s.length();
  if (n < 1 || n > SERIAL_MAX_LEN) return false;
  int start = (s.charAt(0) == 'T') ? 1 : 0;
  if (start >= n) return false;                    // "T" on its own is not a serial
  for (int i = start; i < n; i++) {
    if (!isDigit(s.charAt(i))) return false;
  }
  return true;
}

bool isTeacherSerial(const String& s) {
  return s.length() > 1 && s.charAt(0) == 'T';
}

// The initials listed for a serial in DIRECTORY, or "" if it is not listed.
String nameFor(const String& serial) {
  for (int i = 0; i < DIRECTORY_SIZE; i++) {
    if (serial == DIRECTORY[i].serial) return DIRECTORY[i].name;
  }
  return "";
}

// "47 TJD", or just "47" when there are no initials.
String labelFor(const String& serial, const String& name) {
  if (name.length() == 0) return serial;
  return serial + " " + name;
}

// Loads the saved serial. If there is none (or forceNew), asks for it and saves it.
void ensureSerial(bool forceNew) {
  preferences.begin("lumini", false);
  String saved = preferences.getString("serial", "");
  preferences.end();

  preferences.begin("lumini", false);
  String savedPin = preferences.getString("pin", "");
  preferences.end();

  if (!forceNew && isValidSerial(saved)) {
    mySerial = saved;
    myPin = savedPin;
    if (!isValidPin(myPin)) myPin = askForPin();        // no PIN yet (for example after an update)
  } else {
    mySerial = askForSerial();
    myPin = askForPin();
  }
  preferences.begin("lumini", false);
  preferences.putString("serial", mySerial);
  preferences.putString("pin", myPin);
  preferences.end();
  myName = nameFor(mySerial);
  Serial.print("This device is serial "); Serial.println(mySerial);
}

// Serial entry screen. Right knob: turn to pick a character, press to add it.
// Left knob: hold 2 seconds to delete the last one, hold 5 seconds to confirm.
String askForEntry(const char* title, const char* chars, int maxLen, bool masked, bool (*valid)(const String&)) {
  String entry = "";
  String message = "";
  int idx = 0;
  const int len = strlen(chars);
  long lastCount = wheelEnc.clicks;
  bool redraw = true;
  int lastReading = HIGH;
  int stableState = HIGH;
  unsigned long lastChange = 0;
  buttonState = HIGH;
  lastButtonState = HIGH;
  confirmHoldCue = true;

  while (true) {
    long steps = wheelEnc.clicks - lastCount;
    if (steps != 0) {
      lastCount = wheelEnc.clicks;
      if (WHEEL_REVERSE) steps = -steps;
      idx = (int)(((idx + steps) % len + len) % len);
      message = "";
      redraw = true;
    }

    int reading = digitalRead(WHEEL_SW_PIN);
    if (reading != lastReading) {
      lastChange = millis();
      lastReading = reading;
    }
    if ((millis() - lastChange) > debounceTime && reading != stableState) {
      stableState = reading;
      if (stableState == HIGH) {                    // released: counts as one press
        if ((int)entry.length() < maxLen) {
          entry += chars[idx];
          buzzFor(1000, 30);
        }
        message = "";
        redraw = true;
      }
    }

    LeftEvent ev = pollLeftGesture();
    if (ev == LEFT_DOUBLE && entry.length() > 0) {
      entry.remove(entry.length() - 1);
      message = "";
      redraw = true;
    } else if (ev == LEFT_HOLD5) {
      if (valid(entry)) {
        confirmHoldCue = false;
        return entry;
      }
      message = masked ? "PIN: 4 to 6 digits" : "Not a valid serial";
      buzzFor(notifyFrequency / 2, 150);
      redraw = true;
    }

    if (redraw) {
      redraw = false;
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextWrap(false);
      display.setCursor(0, 0);
      display.print(title);
      display.drawFastHLine(0, 9, SCREEN_WIDTH, OLED_WHITE);

      display.setTextSize(2);
      display.setCursor(22, 12);
      display.print(chars[((idx - 1) % len + len) % len]);
      display.print(" [");
      display.print(chars[idx]);
      display.print("] ");
      display.print(chars[(idx + 1) % len]);

      display.setCursor(0, 34);
      if (masked) { for (unsigned int k = 0; k < entry.length(); k++) display.print(k + 1 == entry.length() ? entry[k] : '*'); }   // last digit visible, the rest hidden
      else display.print(entry);
      display.print("_");

      display.setTextSize(1);
      display.setCursor(0, 56);
      display.print(message.length() > 0 ? message : String("L 2x=del hold5s=OK"));
      display.setTextWrap(true);
      display.display();
    }
    delay(5);
  }
}

String askForSerial() { return askForEntry("Enter your serial", SERIAL_CHARS, SERIAL_MAX_LEN, false, isValidSerial); }
String askForPin()    { return askForEntry("Enter your PIN", PIN_CHARS, PIN_MAX_LEN, true, isValidPin); }

bool isValidPin(const String& p) {
  if ((int)p.length() < PIN_MIN_LEN || (int)p.length() > PIN_MAX_LEN) return false;
  for (unsigned int k = 0; k < p.length(); k++) if (p[k] < '0' || p[k] > '9') return false;
  return true;
}

// The broker refused the login (wrong PIN, or an admin reset it): ask for the PIN again and remember it.
void handleBadLogin() {
  mqttClient.disconnect();
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Login refused");
  display.println("Wrong PIN, or it was");
  display.println("reset by your teacher.");
  display.display();
  buzzFor(notifyFrequency / 2, 300);
  delay(2500);
  myPin = askForPin();
  preferences.begin("lumini", false);
  preferences.putString("pin", myPin);
  preferences.end();
}

// ---
// --- Menu (opened with the NAV button)
// ---

void openMenu() {
  menuIndex = 0;
  switchScreen(SCREEN_MENU);
}

// Turn the left knob to move, click it to choose. NAV closes the menu.
void handleMenuScreen() {
  NavEvent nav = pollNavButton();
  if (nav != NAV_NONE) { switchScreen(SCREEN_COMPOSE); return; }

  // Either knob scrolls the menu (left knob, or the right knob as a backup).
  long count = cursorEnc.clicks;
  long wcount = wheelEnc.clicks;
  long steps = count - lastEncoderCount;
  if (ENC_REVERSE) steps = -steps;
  steps += wcount - lastWheelCount;
  lastEncoderCount = count;
  lastWheelCount = wcount;
  if (steps != 0) {
    menuIndex = (int)(((menuIndex + steps) % MENU_ITEMS + MENU_ITEMS) % MENU_ITEMS);
    Serial.print("Menu: steps ");
    Serial.print(steps);
    Serial.print(" -> item ");
    Serial.println(menuIndex);
    updateDisplay();
  }

  if (pollLeftGesture() == LEFT_SHORT) runMenuItem(menuIndex);
}

void runMenuItem(int item) {
  if (item == 0) {                       // Compose
    switchScreen(SCREEN_COMPOSE);
  } else if (item == 1) {                // Inbox
    switchScreen(SCREEN_INBOX);
  } else if (item == 2) {                // Send to
    enterPicker();
  } else if (item == 3) {                // Clear message
    translatedMessage = "";
    cursorPos = 0;
    viewStart = 0;
    buzzFor(notifyFrequency / 2, 100);
    switchScreen(SCREEN_COMPOSE);
  }
}

void drawMenu() {
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setCursor(0, 0);
  display.print("MENU");

  String id = mySerial;                  // who this device is, top right
  if (myName.length() > 0) id += " " + myName;
  if (id.length() > 12) id = id.substring(0, 12);
  display.setCursor(SCREEN_WIDTH - 6 * id.length(), 0);
  display.print(id);
  display.drawFastHLine(0, 9, SCREEN_WIDTH, OLED_WHITE);

  String items[MENU_ITEMS];
  items[0] = "Compose";
  String inboxLabel = "Inbox";
  if (inboxCount > 0) {
    inboxLabel += " (" + String(inboxCount);
    int unread = unreadCount();
    if (unread > 0) inboxLabel += ", " + String(unread) + " new";
    inboxLabel += ")";
  }
  items[1] = inboxLabel;
  items[2] = "Send to: " + recipientLabel();
  items[3] = "Clear message";

  for (int i = 0; i < MENU_ITEMS; i++) {
    display.setCursor(0, 13 + i * 10);
    display.print(i == menuIndex ? "> " : "  ");
    String line = items[i];
    if (line.length() > 19) line = line.substring(0, 19);
    display.print(line);
  }

  display.setCursor(0, 56);
  display.print("click=choose NAV=back");
  display.setTextWrap(true);
}

// ---
// --- Send: press the left and right knob together
// ---

// Holding both knob buttons down for SEND_CHORD_MS sends the message. The releases that
// follow are ignored, so a send never also types a space or a letter. A quick accidental
// overlap while typing is shorter than SEND_CHORD_MS and is treated as two normal presses.
void checkSendChord() {
  bool both = digitalRead(MORSE_BUTTON_PIN) == LOW && digitalRead(WHEEL_SW_PIN) == LOW;
  if (!both) {
    chordTiming = false;
    chordFired = false;
    return;
  }
  if (!chordTiming) {
    chordTiming = true;
    chordStart = millis();
  }
  if (!chordFired && millis() - chordStart >= SEND_CHORD_MS) {
    chordFired = true;
    ignoreLeftRelease = true;
    wheelIgnoreRelease = true;
    sendCurrentMessage();
  }
}
