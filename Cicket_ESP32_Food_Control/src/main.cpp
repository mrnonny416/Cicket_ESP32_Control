#include <env.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

#include <ESP8266WIFI.h>

#include <FirebaseESP32.h>
#include <addons/TokenHelper.h>

#include <Servo.h>

#define button D0

#define ultraSonicPing D1
#define ultraSonicIn D2

#define stepPulse D3
#define stepDir D4

#define servoPin 14 // digital pin d5

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool signupOK = false;
bool lastState = false;
const int stepsPerRevolution = 2048; // todo
int foodLevelData = 0;               // from sensor
bool foodStatus = false;             // for enable solenoid
String username = USERNAME;
int food_level_limit = 1;
int food_lowest_limit = 10; // in centimeters

// declere function
void processing();
void controlling();
int readFood();
void food_state();
void setDirect(String);
String split(String, char, int);

// enable time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// servo motor declear
Servo myservo;

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
  myservo.attach(servoPin);
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
}

void loop() {
  timeClient.update();
  processing();
  if (food_level_limit < readFood()) {
    controlling();
  } else {
    Serial.println("Food is over!!!");
    delay(1000);
  }
}

//---------------control water---------------------
void ctrlFood(bool value) { foodStatus = value; }

// get data sensor
int readFood() {
  long duration, percentage = 100;
  digitalWrite(ultraSonicPing, LOW);
  delayMicroseconds(2);
  digitalWrite(ultraSonicPing, HIGH);
  delayMicroseconds(5);
  digitalWrite(ultraSonicPing, LOW);
  duration = pulseIn(ultraSonicIn, HIGH);
  duration = duration / 29 / 2; // in centimeters
  return int((duration * food_lowest_limit) /
             percentage); // return to percentage
}

//-----------------read firebase---------------------
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

//--------------------set firebase-------------------
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
  const String disallowedChars = ".#$[]"; // อักขระที่ไม่อนุญาต

  // ลูปผ่านแต่ละอักขระที่ไม่อนุญาต
  for (int i = 0; i < disallowedChars.length(); i++) {
    char ch = disallowedChars[i]; // ดึงอักขระที่ต้องการ
    input.replace(String(ch), ""); // แทนที่อักขระที่ไม่อนุญาตด้วยสตริงว่าง
  }
}

// Time
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

// Controller
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
      setFirebaseBool(basePath + "food_control/control", false);
    }
  } else {
    setFirebaseBool(basePath + "food_control/control", false);
  }
  setFirebaseInt(basePath + "food_control/sensor",
                 readFood() > 0 ? readFood() : 0);

  Serial.println("food Level : " + String(readFood()) + " %");
  Serial.println("food Level Setting : " + String(food_setting) + " %");
}

void controlling() { /*มีหน้าที่ดึงข้อมูลมาแล้ววสั่งงาน*/
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
    if (readFood() > food_level_limit) {
      for (int pos = 0; pos <= 380; pos++) {
        myservo.write(pos);
        delay(1);
      }
      delay(1000);
      int limit_round = 3;
      limit_round *= 400;
      setDirect("left");
      for (int round = 0; round < limit_round; round++) {
        digitalWrite(stepPulse, HIGH);
        delay(1);
        digitalWrite(stepPulse, LOW);
      }
      setDirect("right");
      for (int round = 0; round < limit_round; round++) {
        digitalWrite(stepPulse, HIGH);
        delay(1);
        digitalWrite(stepPulse, LOW);
      }
    } else {
      foodStatus = false;
      Serial.println("Food is over!!!");
      delay(1000);
    }
  }
}

void setDirect(String dir) {
  if (dir == "left") {
    digitalWrite(stepDir, HIGH); // todo
  } else {
    digitalWrite(stepDir, LOW); // todo
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