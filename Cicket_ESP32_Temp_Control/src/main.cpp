#include <LiquidCrystal_I2C.h> //LCD03 by Ben Arblaster@1.1.2
#include <Wire.h>

#include <env.h>

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

#include <ESP8266WIFI.h>

#include <FirebaseESP32.h>
#include <addons/TokenHelper.h>

int lcdColumns = 16;
int lcdRows = 2;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);

bool signupOK = false;
bool lastState = false;
#define button1Temp D0
#define button2Hum D3
#define DHTPIN D5
#define DHTTYPE DHT11 // DHT 11
#define FanTempPIN D7
#define FanHumidityPIN D6

int tempData = 0;               // from sensor
int humidityData = 0;           // from sensor
int tempSettingData = 0;        // for check limit from firebase
int humiditySettingData = 0;    // for check limit from firebase
bool FanTempStatus = false;     // for enable solenoid
bool FanHumidityStatus = false; // for enable solenoid
int temp_high_limit = 27; // ถ้าสูงกว่า ต้องเปิดเพื่อลดอุณหภูมิ
int humidity_high_limit = 70; // ถ้ามากกว่า ให้เปิด เพื่อลดความชื้น
String username = USERNAME;
// declere function
void processing();
void controlling();
int readTemp();
int readHumidity();
String setText();
void setMonitor();

DHT_Unified dht(DHTPIN, DHTTYPE);
// enable time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

void IRAM_ATTR IO_INT_ISR() {
  Serial.println("FORCE FAN TEMP : " + String(!digitalRead(FanTempPIN)));
  digitalWrite(FanTempPIN, !digitalRead(FanTempPIN));
}

void IRAM_ATTR IO_INT_ISR_HUM() {
  Serial.println("FORCE FAN HUMIDITY : " +
                 String(!digitalRead(FanHumidityPIN)));
  digitalWrite(FanHumidityPIN, !digitalRead(FanHumidityPIN));
}

void setup() {
  Serial.begin(9600);

  // initialize LCD
  lcd.init();
  // turn on LCD backlight
  lcd.backlight();

  // set sensor

  attachInterrupt(button1Temp, IO_INT_ISR, RISING);
  attachInterrupt(button2Hum, IO_INT_ISR_HUM, RISING);
  pinMode(FanTempPIN, OUTPUT);
  pinMode(DHTPIN, INPUT);
  pinMode(FanHumidityPIN, OUTPUT);
  setMonitor();
  // set firebase
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
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
  }
  config.token_status_callback =
      tokenStatusCallback; // see addons/TokenHelper.h
  Firebase.begin(&config, &auth);

  // set time
  timeClient.begin();
}

void loop() {
  timeClient.update();
  setMonitor();
  processing();
  controlling();
}

//---------------control fan---------------------
void ctrlTemp(bool value) { digitalWrite(FanTempPIN, value); }
void ctrlHumidity(bool value) { digitalWrite(FanHumidityPIN, value); }

// get data sensor
int readTemp() {
  sensors_event_t event;
  float temperature = 0;
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature)) {
    temperature = 0;
  } else {
    temperature = event.temperature;
  }
  if (temperature > 100)
    temperature = 100;
  return (int)temperature;
}

int readHumidity() {
  sensors_event_t event;
  float humidity = 0;
  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity)) {
    humidity = 0;
  } else {
    humidity = event.relative_humidity;
  }
  if (humidity > 100)
    humidity = 100;
  return (int)humidity;
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

void monitor_Temp(String basePath, int temp_setting) {
  if (readTemp() < temp_high_limit) {
    setFirebaseBool(basePath + "temperature/control",
                    readTemp() < temp_setting);
  } else {
    setFirebaseBool(basePath + "temperature/control", false);
  }
  setFirebaseInt(basePath + "temperature/sensor",
                 readTemp() > 0 ? readTemp() : 0);

  Serial.print("Temp : " + String(readTemp()) + " ℃  |  ");
  Serial.println("Temp Setting : " + String(temp_setting) + " ℃");
}

void monitor_Humidity(String basePath, int humidity_setting) {
  if (readHumidity() < humidity_setting) {
    setFirebaseBool(basePath + "humidity/control",
                    readHumidity() < humidity_setting);
  } else {
    setFirebaseBool(basePath + "humidity/control", false);
  }
  setFirebaseInt(basePath + "humidity/sensor",
                 readHumidity() > 0 ? readHumidity() : 0);

  Serial.print("Humidity : " + String(readHumidity()) + " %  |  ");
  Serial.println("Humidity Setting : " + String(humidity_setting) + " %");
}
// Controller
void processing() { /*มีหน้าที่ ดึงข้อมูลมาแล้วเช็ค*/
  delay(2500);
  sanitizeString(username);
  String basePath = username + "/controller/";
  int temp_setting = getFirebaseInt(basePath + "temperature/setting");
  int humidity_setting = getFirebaseInt(basePath + "humidity/setting");
  setFirebaseBool(basePath + "temperature/control", temp_setting <= readTemp());
  setFirebaseBool(basePath + "humidity/control",
                  humidity_setting <= readHumidity());
  monitor_Temp(basePath, temp_setting);
  monitor_Humidity(basePath, humidity_setting);
}

void controlling() { /*มีหน้าที่ดึงข้อมูลมาแล้ววสั่งงาน*/
  sanitizeString(username);
  String basePath = username + "/controller/";
  int temp_control = getFirebaseBool(basePath + "temperature/control");
  int humidity_control = getFirebaseBool(basePath + "humidity/control");
  // report temp
  if (lastState != temp_control && lastState == false) {
    setFirebaseInt(basePath + "report/" + getTimeText() + "/temp_report/" +
                       String(timeClient.getEpochTime()),
                   1);
    lastState = true;
  }
  if (temp_control == false) {
    lastState = false;
  }
  // report hum
  if (lastState != humidity_control && lastState == false) {
    setFirebaseInt(basePath + "report/" + getTimeText() + "/humidity_report/" +
                       String(timeClient.getEpochTime()),
                   1);
    lastState = true;
  }
  if (humidity_control == false) {
    lastState = false;
  }

  ctrlTemp(temp_control);
  ctrlHumidity(humidity_control);
}

String setText() {
  String Text = "TEMP:";
  Text += String(readTemp()) + "C";
  int text_length = Text.length();
  // first - 16 - 5 = loop circle
  int loopCircle = 16 - text_length - 6;
  int loopCount = 0;
  while (loopCount < loopCircle) {
    Text += " ";
    loopCount += 1;
  }
  Text += "RTDB:";
  Text += signupOK ? "/" : "X";
  Text += "HUMIDITY:";
  Text += String(readHumidity()) + "%";
  return Text;
}

void setMonitor() {
  String text = setText();
  Serial.print(text);
  Serial.println("");
  lcd.clear();
  lcd.setCursor(0, 0);

  if (text.length() <= 16) {
    lcd.print(text);
  } else {
    String firstLine = text.substring(0, 16);
    String secondLine = text.substring(16);

    lcd.print(firstLine);
    lcd.setCursor(0, 1);
    lcd.print(secondLine);
  }
}