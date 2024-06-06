#include <env.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

#include <ESP8266WIFI.h>

#include <FirebaseESP32.h>
#include <addons/TokenHelper.h>

#define button D0

#define ultraSonicPing D1
#define ultraSonicIn D2

#define stepPulse D3
#define stepDir D4

#define motorPin1 D5
#define motorPin2 D6
#define motorPin3 D7
#define motorPin4 D8

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool signupOK = false;
bool lastState = false;
const int stepsPerRevolution = 2048; // todo
int foodLevelData = 0;               // from sensor
bool foodStatus = false;             // for enable solenoid
String username = USERNAME;
int food_level_limit = 30; // in centimeters
int food_lowest_limit = 1; // in percentage

// declere function
void processing();
void controlling();
int readFood();
void food_state();
void setDirect(String);
String split(String, char, int);
int getFirebaseInt(String);
String getFirebaseString(String);
bool getFirebaseBool(String);
void setFirebaseBool(String, bool);
void setFirebaseInt(String, int);
String getTimeText();
void ctrlFood(bool);
void sanitizeString(String &input);
void stepMoterMove();

// enable time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

void IRAM_ATTR IO_INT_ISR() {
  if (foodStatus == false) {
    Serial.println("force Food control : on");
    foodStatus = true;
    food_state();
  }
}

void setup() {
  Serial.begin(9600);
  // set sensor
  attachInterrupt(button, IO_INT_ISR, RISING);
  pinMode(ultraSonicPing, OUTPUT);
  pinMode(ultraSonicIn, INPUT);
  pinMode(stepPulse, OUTPUT);
  pinMode(stepDir, OUTPUT);

  pinMode(motorPin1, OUTPUT);
  pinMode(motorPin2, OUTPUT);
  pinMode(motorPin3, OUTPUT);
  pinMode(motorPin4, OUTPUT);

  // set firebase
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
    // todo : force_control
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;
  Firebase.reconnectNetwork(true);
  fbdo.setBSSLBufferSize(4096 /* Rx buffer size in bytes from 512 - 16384 */,
                         1024 /* Tx buffer size in bytes from 512 - 16384 */);
  while (!signupOK) {
    Serial.print("Sign up new user... ");
    if (Firebase.signUp(&config, &auth, "", "")) {
      Serial.println("ok");
      signupOK = true;
    } else {
      Serial.printf("%s\n", config.signer.signupError.message.c_str());
    }
    delay(300);
    // todo : force_control
  }
  config.token_status_callback =
      tokenStatusCallback; // see addons/TokenHelper.h
  Firebase.begin(&config, &auth);

  // set time
  timeClient.begin();
  int timezone = 3600;
  int th_timezone = 7;
  timeClient.setTimeOffset(timezone * th_timezone);
}

void loop() {
  timeClient.update();
  processing();
  controlling();
}

void processing() {
  delay(1500);
  sanitizeString(username);
  String basePath = username + "/controller/";
  int food_setting = getFirebaseInt(basePath + "food_control/setting");
  if (readFood() > food_setting) {
    String schedule1 =
        getFirebaseString(basePath + "food_control/schedule/case1");
    String hours1 = split(schedule1, ':', 0);
    String minutes1 = split(schedule1, ':', 1);
    String schedule2 =
        getFirebaseString(basePath + "food_control/schedule/case2");
    String hours2 = split(schedule2, ':', 0);
    String minutes2 = split(schedule2, ':', 1);
    if ((timeClient.getHours() == hours1.toInt() &&
         timeClient.getMinutes() == minutes1.toInt()) ||
        (timeClient.getHours() == hours2.toInt() &&
         timeClient.getMinutes() == minutes2.toInt())) {
      setFirebaseBool(basePath + "food_control/control", true);
    } else {
      Serial.println("Time now -->" + String(timeClient.getHours()) + ":" +
                     String(timeClient.getMinutes()));
      Serial.println("schedule 1->" + schedule1);
      Serial.println("schedule 2->" + schedule2);
      Serial.println("Set Firebase No Schedule");
      setFirebaseBool(basePath + "food_control/control", false);
    }
  } else {
    Serial.println("Set Firebase Not Enought Food");
    setFirebaseBool(basePath + "food_control/control", false);
  }
  setFirebaseInt(basePath + "food_control/sensor",
                 readFood() > 0 ? readFood() : 0);

  Serial.println("food Level : " + String(readFood()) + " %");
  Serial.println("food Level Setting : " + String(food_setting) + " %");
}

void controlling() {
  sanitizeString(username);
  String basePath = username + "/controller/";
  int food_control = getFirebaseBool(basePath + "food_control/control");
  if (lastState != food_control && lastState == false) {
    setFirebaseInt(basePath + "report/" + getTimeText() + "/food_report/" +
                       String(timeClient.getEpochTime()),
                   1);
    lastState = true;
  }
  if (food_control == false) {
    lastState = false;
  }
  ctrlFood(food_control);
  food_state();
}

void food_state() {
  if (foodStatus) {
    if (readFood() > food_lowest_limit) {
      //--- server give food control here ↓
      int pulse = 16000; // todo edit here
      Serial.println("");
      Serial.print("Step Driver Moving");
      for (int round = 0; round < pulse; round++) {
        if (round % 100 == 0) {
          Serial.print(".");
        }
        if (round % 1000 == 0) {
          Serial.print(String(round));
        }
        stepMoterMove();
      }
      //--- server give food control here ↑
      delay(1000);
      //--- motor rail food control here ↓
      int limit_round = 40; // todo edit here
      limit_round *= 200;
      setDirect("left");
      Serial.println("");
      Serial.print("Rail Motor Moving : OUT");
      for (int round = 0; round < limit_round; round++) {
        if (round % 100 == 0) {
          Serial.print(".");
        }
        if (round % 1000 == 0) {
          Serial.print(String(round));
        }
        digitalWrite(stepPulse, HIGH);
        delay(1);
        digitalWrite(stepPulse, LOW);
      }
      setDirect("right");
      Serial.println("");
      Serial.print("Rail Motor Moving : BACK");
      for (int round = 0; round < limit_round; round++) {
        if (round % 100 == 0) {
          Serial.print(".");
        }
        if (round % 1000 == 0) {
          Serial.print(String(round));
        }
        digitalWrite(stepPulse, HIGH);
        delay(1);
        digitalWrite(stepPulse, LOW);
      }
      Serial.println("");
      //--- motor rail food control here ↑
    } else {
      foodStatus = false;
      Serial.println("Food is over!!! can't give food");
      delay(1000);
    }
  }
}

//----------↓↓↓function↓↓↓------------
void ctrlFood(bool value) { foodStatus = value; }

int readFood() {
  long duration, percentage = 100;
  digitalWrite(ultraSonicPing, LOW);
  delayMicroseconds(2);
  digitalWrite(ultraSonicPing, HIGH);
  delayMicroseconds(5);
  digitalWrite(ultraSonicPing, LOW);
  duration = pulseIn(ultraSonicIn, HIGH);
  duration = duration / 29 / 2; // in centimeters
  return 100 - int((duration * percentage) /
                   food_level_limit); // return to percentage
}

bool getFirebaseBool(String path) {
  if (Firebase.getBool(fbdo, path)) {
    return fbdo.to<bool>();
  }
  return false; // ค่ากลับเมื่อไม่สามารถรับค่าได้
}

int getFirebaseInt(String path) {
  if (Firebase.getInt(fbdo, path)) {
    return fbdo.to<int>();
  }
  return -1; // ค่ากลับเมื่อไม่สามารถรับค่าได้
}

String getFirebaseString(String path) {
  if (Firebase.getString(fbdo, path)) {
    return fbdo.to<const char *>();
  }
  return ""; // ค่ากลับเมื่อไม่สามารถรับค่าได้
}

void setFirebaseBool(String path, bool value) {
  if (Firebase.setBool(fbdo, path.c_str(), value)) {
    Serial.println("Set OK : " + String(value) + " -> " + path);
  } else {
    Serial.print("Error: ");
    Serial.println(fbdo.errorReason().c_str());
  }
}

void setFirebaseInt(String path, int value) {
  if (Firebase.setInt(fbdo, path.c_str(), value)) {
    Serial.println("Set OK : " + String(value) + " -> " + path);
  } else {
    Serial.print("Error: ");
    Serial.println(fbdo.errorReason().c_str());
  }
}

void sanitizeString(String &input) {
  const String disallowedChars = ".#$[]";
  for (unsigned int i = 0; i < disallowedChars.length(); i++) {
    char ch = disallowedChars[i];
    input.replace(String(ch), "");
  }
}

String getTimeText() {
  if (timeClient.isTimeSet()) {
    time_t epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime((time_t *)&epochTime);
    int monthDay = ptm->tm_mday;
    int currentMonth = ptm->tm_mon + 1;
    int currentYear = ptm->tm_year + 1900;
    String currentDate = String(currentYear) + "/" + String(currentMonth) +
                         "/" + String(monthDay);
    return currentDate;
  }
  return "0/0/0";
}

void setDirect(String dir) {
  if (dir == "left") {
    digitalWrite(stepDir, LOW); // todo
  } else {
    digitalWrite(stepDir, HIGH); // todo
  }
}

String split(String str, char delimiter, int index) {
  int startIndex = 0;
  int endIndex = 0;
  int delimiterCount = 0;

  while (delimiterCount <= index) {
    endIndex = str.indexOf(delimiter, startIndex);
    if (delimiterCount == index) {
      return str.substring(startIndex,
                           (endIndex == -1) ? str.length() : endIndex);
    }
    startIndex = endIndex + 1;
    delimiterCount++;
  }
  return "";
}

void stepMoterMove() {
  digitalWrite(motorPin1, HIGH);
  digitalWrite(motorPin2, HIGH);
  digitalWrite(motorPin3, LOW);
  digitalWrite(motorPin4, LOW);
  delay(2);
  digitalWrite(motorPin3, HIGH);
  digitalWrite(motorPin1, LOW);
  delay(2);
  digitalWrite(motorPin4, HIGH);
  digitalWrite(motorPin2, LOW);
  delay(2);
  digitalWrite(motorPin1, HIGH);
  digitalWrite(motorPin3, LOW);
  delay(2);
}