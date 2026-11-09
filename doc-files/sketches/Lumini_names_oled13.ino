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
#include <PubSubClient.h>
#include <Preferences.h> // For remembering WiFi
#include "esp_timer.h"   // Samples the rotary encoder every millisecond

// --- Globals ---
const char* TOPIC_ROOT = "morse/tj-project-final";   // change this to something unique to your school
const char* MQTT_BROKER = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
String clientID = "MorseESP32-";
WiFiClient espClient;
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
String myName = "";                     // initials, looked up in DIRECTORY below (can be empty)
const char* SEND_TO_ALL = "ALL";        // recipient value meaning "announce to every device"
const int   SERIAL_MAX_LEN = 6;
const char  SERIAL_CHARS[] = "0123456789T";   // what the wheel offers when typing a serial

// --- Who this device can send to: every student from batches 2018 to 2026 ---
// serial = badge number + 2-digit year (badge 56 in 2023 -> 5623). The year of a serial is its last two digits.
// Teachers are not listed yet: any serial that starts with T (T33) still works, it just shows no name.
struct Contact { const char* serial; const char* name; };
const Contact DIRECTORY[] = {
  {"118", "Dechen Lhendup"},
  {"218", "Dechen Wangmo"},
  {"318", "Deki Wangmo"},
  {"418", "Drupthob Jampa Goenpo Wangdi"},
  {"518", "Jigme Rinzin"},
  {"618", "Karma Yeshey Choden"},
  {"718", "Karma Gyeltshen"},
  {"818", "Karma Pelden"},
  {"918", "Kencho Wangdi"},
  {"1018", "Kezang Dechen"},
  {"1118", "Kinga Norbu"},
  {"1218", "Kinley Pem"},
  {"1318", "Kinley Wangdi"},
  {"1418", "Pemba Tshering"},
  {"1518", "Phuntsho Wangmo"},
  {"1618", "Riwang choki Tshering"},
  {"1718", "Sapna Bhujel"},
  {"1818", "Serthup Pelbar"},
  {"1918", "Sonam Choki"},
  {"2018", "Sonam Tshering Dumjan"},
  {"2118", "Sonam Norbu"},
  {"2218", "Sonam Tshomo Tshering"},
  {"2318", "Sonam Wangchuk"},
  {"2418", "Tandin Gyeltshen"},
  {"2518", "Tashi Lhundup"},
  {"2618", "Tempa Wangdi"},
  {"2718", "Thinley Norbu Jamtsho"},
  {"2818", "Tshendu Dorji"},
  {"2918", "Tshering Yangden"},
  {"3018", "Tshokey Dorji"},
  {"3118", "Yonten Chophel"},
  {"3218", "Yonten Chogyal"},
  {"119", "Choni Samdup"},
  {"219", "Jambay Dema"},
  {"319", "Jigme Dechen"},
  {"419", "Karma Jamtsho"},
  {"519", "Karma Zam"},
  {"619", "Kinley Yangdon"},
  {"719", "Kinley Zangmo"},
  {"819", "Kinzang Thinley"},
  {"919", "Kinzang Wangchuk"},
  {"1019", "Kunzang Choden"},
  {"1119", "Nima Dorji"},
  {"1219", "Nima Yoezer"},
  {"1319", "Pema Choden"},
  {"1419", "Pema Rinzin Deolkar"},
  {"1519", "Pema Yudon"},
  {"1619", "Sonam Zangmo"},
  {"1719", "Tandin Tobgay"},
  {"1819", "Tenzin Rabgay"},
  {"1919", "Tshering Lhaden"},
  {"2019", "Tshering Nidup"},
  {"2119", "Ugyen Dema"},
  {"2219", "Ugyen Jigme Samphel"},
  {"2319", "Ugyen Lhamo"},
  {"2419", "Ugyen Wangmo"},
  {"2519", "Usha Kiran Limboo"},
  {"2619", "Younten Thinley"},
  {"2719", "Zhenphen Norbu"},
  {"120", "Chencho Wangdi"},
  {"220", "Cheying Yeshi Dema"},
  {"320", "Chimi Zam"},
  {"420", "Choki Wangmo"},
  {"520", "Damzang Chimi Seldon"},
  {"620", "Deki Lhazom"},
  {"720", "Deki Seldon"},
  {"820", "Dhan Bahadur Rana Mongar"},
  {"920", "Dhan Bdr Rai"},
  {"1020", "Jigme Chogyel"},
  {"1120", "Jigme Seldon"},
  {"1220", "Karma Deki"},
  {"1320", "Khenrab Tenzin Dhendup"},
  {"1420", "Kinley Sonam"},
  {"1520", "Kinley Wangmo"},
  {"1620", "Kinley Yangchen"},
  {"1720", "Leki Wangmo"},
  {"1820", "Ngawang Pemo Dukpa"},
  {"1920", "Ogyen Choden"},
  {"2020", "Parnita Ghalley"},
  {"2120", "Pema Ugyen Dendup"},
  {"2220", "Pema Yangchen"},
  {"2320", "Phub Tshering"},
  {"2420", "Premika Rai"},
  {"2520", "Purna Bahadur Subedi"},
  {"2620", "Rashmika Chhetri"},
  {"2720", "Rigsel Dema"},
  {"2820", "Rinchen Namgay"},
  {"2920", "Rinchen Yoedsel Pelzom"},
  {"3020", "Rinzin Wangmo"},
  {"3120", "Sangay Dema"},
  {"3220", "Sangay Wangdi"},
  {"3320", "Sonam Choki"},
  {"3420", "Sonam Pelmo"},
  {"3520", "Sonam Tshomo"},
  {"3620", "Sonam Wangchuk"},
  {"3720", "Sonam Zangmo"},
  {"3820", "Sumjay"},
  {"3920", "Tandin Dema"},
  {"4020", "Tandin Tashi Wangmo"},
  {"4120", "Tandin Wangmo"},
  {"4220", "Tenzin Rabgay Zangmo"},
  {"4320", "Thinley Wozer Dorji"},
  {"4420", "Tshering Choden"},
  {"4520", "Tshering Choden Wangmo"},
  {"4620", "Tshering Wangmo"},
  {"4720", "Ugyen Tshering"},
  {"4820", "Yangdon"},
  {"4920", "Yeshey Dolma Tshering"},
  {"5020", "Yeshey Dorji"},
  {"5120", "Yeshey Lhamo"},
  {"121", "Alka Subba"},
  {"221", "Amir Rai"},
  {"321", "Anuskha Rai"},
  {"421", "Babita Ghalley"},
  {"521", "Chencho Tshering Chokgyel"},
  {"621", "Chimi Yoesel Phuntsho"},
  {"721", "Damcho Zam"},
  {"821", "Damchoe Yedzhin Norbu"},
  {"921", "Desum Tendhag"},
  {"1021", "Drimed Kuenden"},
  {"1121", "Dorji Yangzom"},
  {"1221", "Dorji Yangki"},
  {"1321", "Dorji Ngawang Choden Namgyal"},
  {"1421", "Jamyang Lhendup"},
  {"1521", "Jetsuen Lhazay Pema"},
  {"1621", "Jigme Thinley Namgyel"},
  {"1721", "Jigme Yoezer"},
  {"1821", "Jigme Tobgyal"},
  {"1921", "Jitsun Dolkar"},
  {"2021", "Junu Ghalley"},
  {"2121", "Karma Tshechi"},
  {"2221", "Karma Thinley Tobden"},
  {"2321", "Kezang Nima"},
  {"2421", "Kinley Dorji"},
  {"2521", "Kuenam Yoesel Wangmo"},
  {"2621", "Kuenzang Kuenphel Norbu"},
  {"2721", "Mon Limbu"},
  {"2821", "Namgay Lham"},
  {"2921", "Namgay Lhamo"},
  {"3021", "Namgay Wangmo"},
  {"3121", "Rigzin Choden"},
  {"3221", "Rigzin Kinzang Thinley"},
  {"3321", "Rinzin Wangchuk"},
  {"3421", "Sangay Wangchuk"},
  {"3521", "Serchey Wangmo Lhazom"},
  {"3621", "Shyine Kundareaka"},
  {"3721", "Singye Sherab"},
  {"3821", "Sita Mongar"},
  {"3921", "Sonam Tenzin"},
  {"4021", "Sonam Choden"},
  {"4121", "Sonam Jamtsho (S)"},
  {"4221", "Sonam Jamtsho (W)"},
  {"4321", "Tandin Dorji Tenzin"},
  {"4421", "Tashi Cheodron"},
  {"4521", "Tenzin Thinley Lhamo"},
  {"4621", "Tenzin Yoedchenma Dorjee"},
  {"4721", "Thinley Phuntsho"},
  {"4821", "Thinley Yoezer"},
  {"4921", "Thinley Wangdi"},
  {"5021", "Tshelthrim Dolkar"},
  {"5121", "Tsheltrim Lhamo"},
  {"5221", "Tshering Choki"},
  {"5321", "Tshering Dolkar Lhanzee"},
  {"5421", "Tshewang Norbu"},
  {"5521", "Tsheyang Palki Tshewang"},
  {"5621", "Wangchuk Namgyel"},
  {"5721", "Yeshey Tshogyal Wangda"},
  {"5821", "Yonten Thinley"},
  {"122", "Ashman Pakhring"},
  {"222", "Dago Penjor"},
  {"322", "Damcho Wangchuk"},
  {"422", "Dawa Nangsel Seldon"},
  {"522", "Deki Lhamo"},
  {"622", "Dema Tamang"},
  {"722", "Dorji Gyeltshen"},
  {"822", "Dorji Yangzom"},
  {"922", "Gephel Tsheltrim Namdhak"},
  {"1022", "Jalsha Powdyel"},
  {"1122", "Jamgyal Singye Wangchuck"},
  {"1222", "Jamyang Singye Wangchuck"},
  {"1322", "Jigme Phuntsho Wangchuk"},
  {"1422", "Karma Pema Dolkar"},
  {"1522", "Karma Zangmo"},
  {"1622", "Khendenma Dechen Wangchuk"},
  {"1722", "Kinley Tsheyang Seldon"},
  {"1822", "Kinzang Choden"},
  {"1922", "Kinzang Jigme"},
  {"2022", "Kuenga Eupel Wangmo"},
  {"2122", "Kuenzang Dolma"},
  {"2222", "Kusum Choden Lhendup"},
  {"2322", "Lawti Subba"},
  {"2422", "Leela Maya Jogi"},
  {"2522", "Lekso Dechen Wangdi"},
  {"2622", "Lhachey Dolma"},
  {"2722", "Lhaki Sonam"},
  {"2822", "Namkha Lham"},
  {"2922", "Nawang Tshesang Metho Gyeltshen"},
  {"3022", "Pema Jatsho"},
  {"3122", "Pema Yangki"},
  {"3222", "Pema Yoezer"},
  {"3322", "Sangay Lhamo Yangsel"},
  {"3422", "Sherab Zangmo (T)"},
  {"3522", "Sherab Lhamo"},
  {"3622", "Sherab Zangmo"},
  {"3722", "Somwang Tshering Palden"},
  {"3822", "Sonam Dema"},
  {"3922", "Sonam Deywa Jamtsho"},
  {"4022", "Sonam Jamtsho"},
  {"4122", "Sonam Lakhi Dema"},
  {"4222", "Sonam Lhaki"},
  {"4322", "Sonam P Choki"},
  {"4422", "Sonam Pema Yangchen"},
  {"4522", "Sonam Tshering"},
  {"4622", "Sonam Yangki"},
  {"4722", "Sonam Zangmo"},
  {"4822", "Tandin Wangmo"},
  {"4922", "Tenzin Nima Wangchuk"},
  {"5022", "Tshering Choden"},
  {"5122", "Tshering Lham"},
  {"5222", "Tshering Yangdon"},
  {"5322", "Tsheyang Lhaki Dolma"},
  {"5422", "Ugyen Gurung"},
  {"5522", "Ugyen Kuenjung Dorji Wangchuk"},
  {"5622", "Ugyen Pelden Dorji"},
  {"5722", "Ugyen Tenzin Norbu"},
  {"5822", "Yangchen Atisha Dorji"},
  {"5922", "Yeewong Yangchen Lhamo"},
  {"6022", "Yonten Thai"},
  {"6122", "Zenisha Peyzom Choki"},
  {"6222", "Yangtshel Wangyel"},
  {"123", "Aishna Chhetri"},
  {"223", "Anushika Rai"},
  {"323", "Chandan Kafley"},
  {"423", "Chimmi Dorji"},
  {"523", "Damchoe Wangmo"},
  {"623", "Deki Choden"},
  {"723", "Dorji Dradull"},
  {"823", "Droduel Jigme Lodey"},
  {"923", "Geeta Dungyel"},
  {"1023", "Jamyang Yuden Dorji"},
  {"1123", "Jangsem Dolkar"},
  {"1223", "Jigme Tshering"},
  {"1323", "Jigme Wangmo"},
  {"1423", "Karma Lhachoe"},
  {"1523", "Kezang Choden"},
  {"1623", "Kinley Pelden (Z)"},
  {"1723", "Kinley Pelden (C)"},
  {"1823", "Kinley Wangmo"},
  {"1923", "Kinzang Choden"},
  {"2023", "Kuenga Yul Jigme"},
  {"2123", "Kuenlay Tshering Dolkar"},
  {"2223", "Kuenzang Tobgyal Tenzin"},
  {"2323", "Nima Choden"},
  {"2423", "Nima Pelden"},
  {"2523", "Nima Yoezer"},
  {"2623", "Nima Zangmo Tamang"},
  {"2723", "Nisha Rizal"},
  {"2823", "Palden Gadhen Nima"},
  {"2923", "Parina Rai"},
  {"3023", "Pema Tshering"},
  {"3123", "Pema Yangdon"},
  {"3223", "Rada Rigsel Wangchuk"},
  {"3323", "Rigden Nima"},
  {"3423", "Sangay Choden"},
  {"3523", "Sangay Tharchen"},
  {"3623", "Sonam Rigzang Seday"},
  {"3723", "Sonam Wangchuk Ghaley"},
  {"3823", "Tandin Wangyal"},
  {"3923", "Tapasya Giri"},
  {"4023", "Tashi Rangdrup"},
  {"4123", "Tashi Tshering"},
  {"4223", "Tency Dorjee"},
  {"4323", "Tenzin Chodron"},
  {"4423", "Tenzin Tshering Drukar"},
  {"4523", "Tenzing Jangsel Dorjee"},
  {"4623", "Thinlay Paldhen Yoezer"},
  {"4723", "Thukten Dorji"},
  {"4823", "Tshering Chuki Yangzom"},
  {"4923", "Tshering Namgyel Dorji"},
  {"5023", "Tshering Yangtshok Lhazen"},
  {"5123", "Tsheyang Tshogyal"},
  {"5223", "Ugyen Rigpa Yeshey"},
  {"5323", "Yeewang Pinda Lhazin"},
  {"5423", "Yuelha Yeshey Tshogyal"},
  {"5523", "Yuthra Yuphel Lhamo"},
  {"5623", "Zdrimet Kuenga"},
  {"124", "Alejandro Esteves"},
  {"224", "Babita Ghalle"},
  {"324", "Bal Bahadur Subba"},
  {"424", "Chencho Norbu"},
  {"524", "Chimi Rinzin Wangmo"},
  {"624", "Dhanuka Homagai"},
  {"724", "Dorji Jatsho"},
  {"824", "Jampel Dorji"},
  {"924", "Jamyang Choedra"},
  {"1024", "Jamyang Dorji"},
  {"1124", "Jigdral Namgyal"},
  {"1224", "Jigme Chogyel"},
  {"1324", "Jigme Rigsel Dorji Tenzing"},
  {"1424", "Karma Jangchuk Yeshi"},
  {"1524", "Karma Tendel Dorji"},
  {"1624", "Karma Tshewang Lhamo"},
  {"1724", "Kezang Nima"},
  {"1824", "Khandro Karmo"},
  {"1924", "Kinzang Dema"},
  {"2024", "Kuenga Yangdon"},
  {"2124", "Muskan Karki"},
  {"2224", "Ngawang Sonam Dolkar"},
  {"2324", "Ngawang Tshogyal Phuentshok"},
  {"2424", "Nyizer Khandu"},
  {"2524", "Pelden Wangmo Dorji"},
  {"2624", "Pema Lhaki"},
  {"2724", "Pema Samdrup"},
  {"2824", "Pema Thinley"},
  {"2924", "Pema Yoeser Thinley"},
  {"3024", "Pema Zangmo"},
  {"3124", "Phuntsho Tenzin Wangmo"},
  {"3224", "Phuntshok Namgyel Dhendrup"},
  {"3324", "Rada Om"},
  {"3424", "Riggzin Jigme Jatsho"},
  {"3524", "Ritisha Thara"},
  {"3624", "Rohan Adhikari"},
  {"3724", "Rudra Mini Rai"},
  {"3824", "Sangay Lhazeen Dolma"},
  {"3924", "Sangay Yuden"},
  {"4024", "Sangsung Mongar"},
  {"4124", "Singye Lekzin Dolma"},
  {"4224", "Singye Wangmo"},
  {"4324", "Sonam Lhaden"},
  {"4424", "Sonam Lhazen Tobgay"},
  {"4524", "Sonam Pelkeath Lhazin"},
  {"4624", "Sonam Pelyang Zomkey"},
  {"4724", "Sonam Penjo Dorji"},
  {"4824", "Sonam Rigsel"},
  {"4924", "Sonam Yangden Wangmo"},
  {"5024", "Sonam Zangmo (SJ)"},
  {"5124", "Sonam Zangmo (W)"},
  {"5224", "Tandin Karma Lhazen"},
  {"5324", "Tenzin Tendel Yangzom"},
  {"5424", "Tenzin Yesel Choden"},
  {"5524", "Tenzin Yoesel Dorji"},
  {"5624", "Tshering Rigden Lhamo"},
  {"5724", "Tshering Yangdon"},
  {"5824", "Tsheten Dema"},
  {"5924", "Tshoki Lhamo"},
  {"6024", "Tshoki Tshomo"},
  {"6124", "Ugyen Om"},
  {"6224", "Yeshey Lhamo"},
  {"6324", "Yoesul Yangdon"},
  {"125", "Ahawash Ghalley"},
  {"225", "Arabella Esteves"},
  {"325", "Ashizsh Rai"},
  {"425", "Dawa Tshering"},
  {"525", "Dradrul Zoepa"},
  {"625", "Drishti Samal"},
  {"725", "Jangsem Tshangyang Lhamo"},
  {"825", "Jebika Adhikari"},
  {"925", "Jigme Choden"},
  {"1025", "Jigme Kinga Gyeltshen"},
  {"1125", "Jigme Namgyel Thinley"},
  {"1225", "Jigme Palden Singye"},
  {"1325", "Jigme Phuntsho Wangdue"},
  {"1425", "Jigmme Yeshey Choden"},
  {"1525", "Jigsel Palmo Dakini"},
  {"1625", "Jigten Gyembo"},
  {"1725", "Jurmey Rigsel"},
  {"1825", "Karma Cheden"},
  {"1925", "Karma Dechen Gyelmo"},
  {"2025", "Khushboo"},
  {"2125", "Kinley Dorji"},
  {"2225", "Kinzang Choden"},
  {"2325", "Kuenlek Druksum Tenzin"},
  {"2425", "Kuenzang Lhamo"},
  {"2525", "Lhekden Kuenchap"},
  {"2625", "Lhendup Thaye Tashi"},
  {"2725", "Longsel Yeshey"},
  {"2825", "Namdren Nidup Tsheyang"},
  {"2925", "Namgay Wangmo Tamang"},
  {"3025", "Ngawang Lhaden"},
  {"3125", "Nim Deki Sherpa"},
  {"3225", "Ngawang Zoepa Lhamo"},
  {"3325", "Pelden Jigme Dorji"},
  {"3425", "Pema Dorje Gyatso"},
  {"3525", "Pema Yoesel"},
  {"3625", "Prashna Darjee"},
  {"3725", "Rabgay Norbu Tshering"},
  {"3825", "Rachina Chettri"},
  {"3925", "Rig Yang Selsum"},
  {"4025", "Rigsel Lhaki Lhazin"},
  {"4125", "Rigsel Rigden Wangmo"},
  {"4225", "Rigzang Wangmo"},
  {"4325", "Sherab Wangchuk"},
  {"4425", "Sonam Chimi"},
  {"4525", "Sonam Kunzang Tshomo"},
  {"4625", "Sonam Lhamo"},
  {"4725", "Sonam Rinchen"},
  {"4825", "Sudipa Chhetri"},
  {"4925", "Tandin Namgyal Dorji"},
  {"5025", "Tandin Zam"},
  {"5125", "Tashi Dema"},
  {"5225", "Tenzin Dasel"},
  {"5325", "Tenzin Selden"},
  {"5425", "Tshering Aeingchok Choden"},
  {"5525", "Tshering Dekar"},
  {"5625", "Tshering Tashi Yangzom"},
  {"5725", "Tshewang Gyeltshen"},
  {"5825", "Tujje Kinley Pelden"},
  {"5925", "Ugyen Jamyang"},
  {"6025", "Yonten Choki Gyeltshen"},
  {"126", "Ashika Thapa"},
  {"226", "Aungeeta Rai"},
  {"326", "Chencho Zhomke Lham"},
  {"426", "Chetish Rai"},
  {"526", "Dechen Tshering Yangchen"},
  {"626", "Dechen Tshogyel Yangzom"},
  {"726", "Deki Tshomo Yangzom"},
  {"826", "Dendup Jingpa Wangchu"},
  {"926", "Drukten Lodae Wangda"},
  {"1026", "Jampel Ngawang"},
  {"1126", "Jamyang Dorji"},
  {"1226", "Jamyang Lhamo"},
  {"1326", "Jangchub Lhamo"},
  {"1426", "Jigme Chozen Wangmo"},
  {"1526", "Jigme Tenzin Choden"},
  {"1626", "Jigmed Yoser Dorje"},
  {"1726", "Jigmi Wangchen Namgyal"},
  {"1826", "Jigyel Dorji"},
  {"1926", "Karma Tenzin"},
  {"2026", "Kintsho Dolkar Thinley"},
  {"2126", "Kuenga Penjor"},
  {"2226", "Kuenzang Youden"},
  {"2326", "Lungtok Gyaltsho Wangdi"},
  {"2426", "Mindrup Sherab Gyaldon"},
  {"2526", "Mingma Tshering Sherpa"},
  {"2626", "Namkha Sonam Dolma"},
  {"2726", "Norbu Lhaden"},
  {"2826", "Pema Yangki"},
  {"2926", "Pema Yewong"},
  {"3026", "Pema Zuk Yangsyel"},
  {"3126", "Penjor Nima"},
  {"3226", "Phuensum Lhazin Tamang"},
  {"3326", "Phuntsho choden"},
  {"3426", "Priyasha Gurung"},
  {"3526", "Reena Rana"},
  {"3626", "Riggyal Yoezer Rai"},
  {"3726", "Rigyal Pema Jamtsho"},
  {"3826", "Rigzin Chonni"},
  {"3926", "Sangay Yuden Dorji"},
  {"4026", "Sangay Zangmo"},
  {"4126", "Sonam Choki (L)"},
  {"4226", "Sonam Choki (P)"},
  {"4326", "Sonam Choki (T)"},
  {"4426", "Sonam Namsel Lhamo"},
  {"4526", "Sonam Utshog Lhamo"},
  {"4626", "Sonam Yangchen"},
  {"4726", "Sonam Yangkey"},
  {"4826", "Sonam Yeshi"},
  {"4926", "Subham Bhandari"},
  {"5026", "Tandin Om"},
  {"5126", "Tashi Karsel Lhamo"},
  {"5226", "Tashi Tenzin"},
  {"5326", "Thinley Wangmo"},
  {"5426", "Tshering Choden Dorji"},
  {"5526", "Tshering Tshomo"},
  {"5626", "Tshering Yangchen"},
  {"5726", "Tshewang Tashi Tshering"},
  {"5826", "Ugyen Jamyang Choden"},
  {"5926", "Yeedzhin Keldhen"},
  {"6026", "Zoya Rai"}
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
String topicMine;                       // this device's own topic
String topicAnnounce;                   // topic every device listens to for announcements
std::vector<String> pickSerials;           // the "send to" list while choosing
std::vector<String> pickNames;
int pickIndex = 0;
int pickLevel = 0;                      // 0 = choose a batch (or ALL / type a serial), 1 = choose a person in that batch
int pickYear = 0;
const int MENU_ITEMS = 5;               // Compose, Inbox, Send to, Clear message, Change WiFi
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
void changeWifiFromMenu();
void drawMenu();
void replyToSelectedMessage();
void deleteSelectedMessage();
LeftEvent pollLeftButton();
LeftEvent pollLeftGesture();
bool isValidSerial(const String& s);
bool isTeacherSerial(const String& s);
String nameFor(const String& serial);
String labelFor(const String& serial, const String& name);
String shortName(const String& full);
int yearOfSerial(const String& serial);
void buildGroupList();
void buildPeopleList(int year);
void showWelcome();
long listSteps();
void ensureSerial(bool forceNew);
String askForSerial();
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

  // Payload format:  fromSerial|fromName|text
  int p1 = body.indexOf('|');
  int p2 = (p1 < 0) ? -1 : body.indexOf('|', p1 + 1);
  if (p1 < 1 || p2 < 0) {
    Serial.println("Ignoring malformed message.");
    return;
  }

  String fromSerial = body.substring(0, p1);
  fromSerial.trim();
  String fromName = body.substring(p1 + 1, p2);
  String text = body.substring(p2 + 1);
  if (!isValidSerial(fromSerial)) {
    Serial.println("Ignoring message with no valid serial.");
    return;
  }
  if (fromSerial == mySerial) {
    Serial.println("Ignoring my own message.");
    return;
  }
  String dirName = nameFor(fromSerial);
  if (dirName.length() > 0) fromName = dirName;                 // the directory knows everyone: show the real name
  else if (fromName.length() > 8) fromName = fromName.substring(0, 8);
  if ((int)text.length() > MAX_MESSAGE_LEN) text = text.substring(0, MAX_MESSAGE_LEN);

  bool isAnnouncement = (String(topic) == topicAnnounce);
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
  
  if (mqttClient.connect(clientID.c_str())) {
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
  display.println("NAV 3s: new serial");
  display.display();

  bool modeSelected = false;
  unsigned long navDownAt = 0;
  while (!modeSelected) {
    // Hold NAV for 3 seconds on this page = ask for a new serial number (then carry on choosing the mode)
    if (digitalRead(NAV_PIN) == LOW) {
      if (navDownAt == 0) navDownAt = millis();
      else if (!clearSerial && millis() - navDownAt >= 3000) {
        clearSerial = true;
        buzzFor(notifyFrequency, 200);
        display.fillRect(0, 56, SCREEN_WIDTH, 8, OLED_BLACK);
        display.setCursor(0, 56);
        display.print("New serial: YES");
        display.display();
      }
    } else {
      navDownAt = 0;
    }

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
  if (myName.length() > 0) idLine += " " + shortName(myName);

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
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqtt_callback);
  mqttClient.setBufferSize(512);
  topicMine = String(TOPIC_ROOT) + "/dm/" + mySerial;
  topicAnnounce = String(TOPIC_ROOT) + "/announce";
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
                   ? topicAnnounce
                   : String(TOPIC_ROOT) + "/dm/" + recipientSerial;
  String sendName = shortName(myName);
  if (sendName.length() > 8) sendName = sendName.substring(0, 8);
  String payload = mySerial + "|" + sendName + "|" + text;   // fromSerial|fromName|text

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

// Nobody is chosen at start-up: with hundreds of students the user picks the person (Send to).
void chooseDefaultRecipient() {
  recipientSerial = "";
  recipientName = "";
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
  if (isBlindMode) {   // no screen to read: play only the words of the message in Morse (not the sender, not the serial)
    delay(300);
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

// Level 0 list: [ALL for teachers], [Type a serial], then the batches (your own batch first).
void buildGroupList() {
  pickSerials.clear();
  pickNames.clear();
  if (isTeacherSerial(mySerial)) {
    pickSerials.push_back(SEND_TO_ALL);
    pickNames.push_back("ALL (announce)");
  }
  pickSerials.push_back("#TYPE");
  pickNames.push_back("Type a serial");
  int mine = yearOfSerial(mySerial);
  for (int pass = 0; pass < 2; pass++) {
    for (int y = 2026; y >= 2018; y--) {
      bool isMine = (y == mine);
      if ((pass == 0) != isMine) continue;                         // own batch first, the others after it
      int count = 0;
      for (int k = 0; k < DIRECTORY_SIZE; k++) {
        if (yearOfSerial(String(DIRECTORY[k].serial)) == y && mySerial != DIRECTORY[k].serial) count++;
      }
      if (count == 0) continue;
      pickSerials.push_back("#Y" + String(y));
      pickNames.push_back(String(y) + (isMine ? " (yours) " : " ") + "(" + String(count) + ")");
    }
  }
  pickIndex = 0;
}

// Level 1 list: everyone in one batch, in the order of the school's list.
void buildPeopleList(int year) {
  pickSerials.clear();
  pickNames.clear();
  for (int k = 0; k < DIRECTORY_SIZE; k++) {
    String sr = String(DIRECTORY[k].serial);
    if (sr == mySerial || yearOfSerial(sr) != year) continue;
    pickSerials.push_back(sr);
    pickNames.push_back(labelFor(sr, DIRECTORY[k].name));
  }
  pickIndex = 0;
}

void enterPicker() {
  pickLevel = 0;
  buildGroupList();
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

  long steps = listSteps();
  if (steps != 0) {
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

  int n = pickSerials.size();
  long steps = listSteps();
  if (steps != 0) {
    if (n > 0) {
      pickIndex = (int)(((pickIndex + steps) % n + n) % n);
      updateDisplay();
    }
  }

  LeftEvent ev = pollLeftGesture();
  if (ev == LEFT_DOUBLE && pickLevel == 1) {         // double press = back to the batches
    pickLevel = 0;
    buildGroupList();
    for (int k = 0; k < (int)pickSerials.size(); k++) if (pickSerials[k] == "#Y" + String(pickYear)) pickIndex = k;
    updateDisplay();
    return;
  }
  if (ev == LEFT_SHORT && n > 0) {                   // short press = open / choose
    String item = pickSerials[pickIndex];
    if (pickLevel == 0 && item.startsWith("#Y")) {   // open a batch
      pickYear = item.substring(2).toInt();
      pickLevel = 1;
      buildPeopleList(pickYear);
      updateDisplay();
      return;
    }
    if (item == "#TYPE") {                           // type any serial
      String typed = askForSerial();
      recipientSerial = typed;
      recipientName = nameFor(typed);
      switchScreen(SCREEN_COMPOSE);
      return;
    }
    recipientSerial = item;
    recipientName = (item == SEND_TO_ALL) ? String("ALL") : nameFor(item);
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
  display.print(pickLevel == 0 ? String("Send to:") : String("Batch ") + String(pickYear) + ":");
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
    String t = pickNames[i];
    if (t.length() > 19) t = t.substring(0, 19);
    display.print(t);
  }
  display.setCursor(0, 56);
  display.print(pickLevel == 0 ? "click=open NAV=menu" : "click=pick 2x=back");
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

// Scrolling a list (menu, inbox, "send to"): turning EITHER knob counts, so one knob that does not respond cannot block the list.
// Returns how many clicks were turned since the last call (positive = down the list).
long listSteps() {
  long c = cursorEnc.clicks, w = wheelEnc.clicks;
  long s = c - lastEncoderCount;
  if (ENC_REVERSE) s = -s;
  long ws = w - lastWheelCount;
  if (WHEEL_REVERSE) ws = -ws;
  lastEncoderCount = c;
  lastWheelCount = w;
  long total = s + ws;
  if (total != 0) {
    Serial.print("List: left "); Serial.print(s); Serial.print(", right "); Serial.println(ws);
  }
  return total;
}

// "Zdrimet Kuenga" -> "Zdrimet K."  (first name + initial of the last name; a suffix like (L) is kept). Short enough for the screen.
String shortName(const String& full) {
  String n = full;
  n.trim();
  String suffix = "";
  int p = n.indexOf('(');
  if (p > 0) { suffix = n.substring(p); n = n.substring(0, p); n.trim(); }
  int sp = n.indexOf(' ');
  if (sp > 0) {
    String first = n.substring(0, sp);
    int lastSp = n.lastIndexOf(' ');
    String last = n.substring(lastSp + 1);
    if (last.length() > 0 && last.charAt(last.length() - 1) != '.') n = first + " " + last.charAt(0) + ".";
    else n = first + " " + last;
  }
  if (suffix.length() > 0) n += suffix;
  if (n.length() > 13) n = n.substring(0, 13);
  return n;
}

// "5623 Zdrimet K.", or just "5623" when the serial is not in the directory.
String labelFor(const String& serial, const String& name) {
  if (name.length() == 0) return serial;
  return serial + " " + shortName(name);
}

// The batch year of a student serial (last two digits), or 0 for teachers and anything else.
int yearOfSerial(const String& serial) {
  int n = serial.length();
  if (n < 3 || !isDigit(serial.charAt(0))) return 0;
  return 2000 + serial.substring(n - 2).toInt();
}

// Loads the saved serial. If there is none (or forceNew), asks for it and saves it.
void ensureSerial(bool forceNew) {
  bool asked = false;
  preferences.begin("lumini", false);
  String saved = preferences.getString("serial", "");
  preferences.end();

  if (!forceNew && isValidSerial(saved)) {
    mySerial = saved;
  } else {
    mySerial = askForSerial();
    preferences.begin("lumini", false);
    preferences.putString("serial", mySerial);
    preferences.end();
    asked = true;
  }
  myName = nameFor(mySerial);
  if (asked) showWelcome();
  Serial.print("This device is serial "); Serial.println(mySerial);
}

void showWelcome() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextWrap(true);
  display.setCursor(0, 0);
  display.println("Welcome!");
  display.println(mySerial);
  display.println();
  if (myName.length() > 0) display.println(myName);
  else display.println("Serial not in the list. It still works.");
  display.display();
  delay(2200);
}

// Serial entry screen. Right knob: turn to pick a character, press to add it.
// Left knob: hold 2 seconds to delete the last one, hold 5 seconds to confirm.
String askForSerial() {
  String entry = "";
  String message = "";
  int idx = 0;
  const int len = sizeof(SERIAL_CHARS) - 1;
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
        if ((int)entry.length() < SERIAL_MAX_LEN) {
          entry += SERIAL_CHARS[idx];
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
      if (isValidSerial(entry)) {
        confirmHoldCue = false;
        return entry;
      }
      message = "Not a valid serial";
      buzzFor(notifyFrequency / 2, 150);
      redraw = true;
    }

    if (redraw) {
      redraw = false;
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextWrap(false);
      display.setCursor(0, 0);
      display.print("Enter your serial");
      display.drawFastHLine(0, 9, SCREEN_WIDTH, OLED_WHITE);

      display.setTextSize(2);
      display.setCursor(22, 12);
      display.print(SERIAL_CHARS[((idx - 1) % len + len) % len]);
      display.print(" [");
      display.print(SERIAL_CHARS[idx]);
      display.print("] ");
      display.print(SERIAL_CHARS[(idx + 1) % len]);

      display.setCursor(0, 34);
      display.print(entry);
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

  long steps = listSteps();          // either knob scrolls the menu
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
  } else if (item == 4) {                // Change WiFi
    changeWifiFromMenu();
  }
}

// Menu > Change WiFi: forget the saved network and restart. After the restart the device scans for networks and lets you pick one
// (the same screens as the first start). The serial number stays; unread messages are lost because they only live in memory.
void changeWifiFromMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setCursor(0, 0);
  display.println("Change WiFi?");
  display.println("");
  display.println("The device restarts");
  display.println("and shows the list of");
  display.println("networks to choose.");
  display.println("");
  display.println("Left knob = YES");
  display.print("NAV = no");
  display.display();
  while (true) {
    if (pollNavButton() != NAV_NONE) { switchScreen(SCREEN_MENU); return; }
    if (pollLeftGesture() == LEFT_SHORT) break;
    delay(5);
  }
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Forgetting WiFi...");
  display.println("Restarting.");
  display.display();
  preferences.begin("morse-wifi", false);
  preferences.clear();
  preferences.end();
  delay(900);
  ESP.restart();
}

void drawMenu() {
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setCursor(0, 0);
  display.print("MENU");

  String id = mySerial;                  // who this device is, top right
  if (myName.length() > 0) id += " " + shortName(myName);
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
  items[4] = "Change WiFi";

  for (int i = 0; i < MENU_ITEMS; i++) {
    display.setCursor(0, 11 + i * 9);
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
