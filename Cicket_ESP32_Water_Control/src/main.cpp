#include <env.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

#include <ESP8266WIFI.h>

#include <FirebaseESP32.h>
#include <addons/TokenHelper.h>

#include <Servo.h>

Servo servo360;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;
bool lastState = false;
#define WaterSensorPIN A0
#define SolenoidPIN D2
#define button D0
#define servoMoterPIN D5

int waterlevelData = 0;      // from sensor
int solenoidsettingData = 0; // for check limit from firebase
bool solenoidStatus = false; // for enable solenoid
String username = USERNAME;
int water_level_limit = 1000;
// declere function
void processing();
void controlling();
int readWater();
void controllingFood();
// enable time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

void IRAM_ATTR IO_INT_ISR() {
  Serial.println("force solenoid : " + String(!digitalRead(SolenoidPIN)));
  digitalWrite(SolenoidPIN, !digitalRead(SolenoidPIN));
}

void setup() {
  Serial.begin(9600);

  // set motor for food board
  servo360.attach(servoMoterPIN);

  // set sensor
  attachInterrupt(button, IO_INT_ISR, RISING);
  pinMode(SolenoidPIN, OUTPUT);

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
  if (water_level_limit > readWater()) {
    controlling();
  } else {
    Serial.println("Water Level is over!!!");
    delay(1000);
  }
  controllingFood();
}

//---------------control water---------------------
void ctrlWater(bool value) { digitalWrite(SolenoidPIN, value); }

// get data sensor
int readWater() {
  float water = analogRead(WaterSensorPIN);
  return (int)water;
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
void processing() { /*มีหน้าที่ ดึงข้อมูลมาแล้วเช็ค*/
  delay(1500);
  sanitizeString(username);
  String basePath = username + "/controller/";
  int water_setting = getFirebaseInt(basePath + "water_control/setting");
  if (readWater() < water_level_limit) {
    setFirebaseBool(basePath + "water_control/control",
                    readWater() < water_setting);

  } else {
    setFirebaseBool(basePath + "water_control/sensor", false);
  }
  setFirebaseInt(basePath + "water_control/sensor",
                 readWater() > 0 ? readWater() : 0);

  Serial.print("Water Level : ");
  Serial.println(readWater());
  Serial.print("Water Level Setting : ");
  Serial.println(water_setting);
}

void controlling() { /*มีหน้าที่ดึงข้อมูลมาแล้ววสั่งงาน*/
  sanitizeString(username);
  String basePath = username + "/controller/";
  int water_control = getFirebaseBool(basePath + "water_control/control");
  if (lastState != water_control && lastState == false) {
    setFirebaseInt(basePath + "report/" + getTimeText() + "/water_report/" +
                       String(timeClient.getEpochTime()),
                   1);
    lastState = true;
  }
  if (water_control == false) {
    lastState = false;
  }
  ctrlWater(water_control);
}

void controllingFood() {
  sanitizeString(username);
  String basePath = username + "/controller/";
  int food_wheel_control = getFirebaseBool(basePath + "food_control/control");
  if (food_wheel_control) {
    servo360.writeMicroseconds(2000);
    delay(8000); // TODO: อาจไม่ใช้ ถ้าไม่ใช้ให้ลย หรือ ลดจำนวนลง
    servo360.writeMicroseconds(1500);
    delay(3000); // TODO: อาจไม่ใช้ ถ้าไม่ใช้ให้ลย หรือ ลดจำนวนลง
  }
}