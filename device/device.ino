#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "time.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <Arduino.h>
#include "HX711.h"
#include "soc/rtc.h"
#include <Adafruit_SSD1306.h>
#define OLED_Address 0x3C
Adafruit_SSD1306 display(128, 64);

#define WIFI_SSID "55"
#define WIFI_PASSWORD "12345678"
#define API_KEY "AIzaSyDb-O2CwNs-66SOvNA6LTr8V-VK1UE6PuM"
#define USER_EMAIL "user@gmail.com"
#define USER_PASSWORD "User@123"
#define DATABASE_URL "https://saline-156ee-default-rtdb.firebaseio.com"

// Define Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig configg;
FirebaseJson json;

const int LOADCELL_DOUT_PIN = 17;
const int LOADCELL_SCK_PIN = 16;
int qty1, reading, lastReading, read1, sta1, btn, sta2;
const char* ntpServer = "pool.ntp.org";
String uid, message, historyPath, timestamp;
struct tm timeinfo;
bool detection;
#define buzzer 23
#define SENSOR  13
#define buttonemg 19
#define led1 32
#define led2 33
#define led3 25

long currentMillis = 0;
long previousMillis = 0;
int interval = 1000;
boolean ledState = LOW;
float calibrationFactor = 4.5;
volatile byte pulseCount;
byte pulse1Sec = 0;
float flowRate;
unsigned int flowMilliLitres;
unsigned long totalMilliLitres;

//REPLACE WITH YOUR CALIBRATION FACTOR
#define CALIBRATION_FACTOR 2144.98

unsigned long lastTime = 0;
unsigned long timerDelay = 60000;

HX711 scale;

void IRAM_ATTR pulseCounter() {
  pulseCount++;
}
void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
  Serial.println();
}
unsigned long getTime() {
  time_t now;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return (0);
  }
  time(&now);
  return now;
}
void displayWeight1(int weight) {
  Serial.print("Weight 1 : ");
  Serial.print(weight);
  Serial.print(" ");
  Serial.println("g");
  Firebase.RTDB.setInt(&fbdo, "/live_data/bottle-gram", weight);
}
void setup() {
  Serial.begin(9600);
  pinMode(SENSOR, INPUT_PULLUP);
  pinMode(buzzer, OUTPUT);
  pinMode(led1, OUTPUT);
  pinMode(led2, OUTPUT);
  pinMode(led3, OUTPUT);
  pinMode(buttonemg, INPUT_PULLUP);
  digitalWrite(buzzer, LOW);
  digitalWrite(led1, LOW);
  digitalWrite(led2, LOW);
  digitalWrite(led3, LOW);

  rtc_cpu_freq_config_t config;
  rtc_clk_cpu_freq_get_config(&config);
  rtc_clk_cpu_freq_to_config(RTC_CPU_FREQ_80M, &config);
  rtc_clk_cpu_freq_set_config_fast(&config);
  Serial.println("Initializing the scale");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(CALIBRATION_FACTOR);
  scale.tare();

  pulseCount = 0;
  flowRate = 0.0;
  flowMilliLitres = 0;
  totalMilliLitres = 0;
  previousMillis = 0;

  attachInterrupt(digitalPinToInterrupt(SENSOR), pulseCounter, FALLING);

  display.begin(SSD1306_SWITCHCAPVCC, OLED_Address);
  display.clearDisplay();

  initWiFi();
  configTime(0, 0, ntpServer);

  // Assign the api key (required)
  configg.api_key = API_KEY;

  // Assign the user sign in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  // Assign the RTDB URL (required)
  configg.database_url = DATABASE_URL;

  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);

  // Assign the callback function for the long running token generation task */
  configg.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h

  // Assign the maximum retry of token generation
  configg.max_token_generation_retry = 5;

  // Initialize the library with the Firebase authen and config
  Firebase.begin(&configg, &auth);

  // Getting the user UID might take a few seconds
  Serial.println("Getting User UID");
  while ((auth.token.uid) == "") {
    Serial.print('.');
    delay(1000);
  }
  // Print user UID
  uid = auth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.println(uid);
  historyPath = "/history";
}

void loop() {
  timestamp = getTime();
  cell1Read();
  flowread();
  Display();

  if ((millis() - lastTime) > timerDelay) {
    history();
    lastTime = millis();
  }

  btn = digitalRead(buttonemg);
  if (btn == LOW && sta2 == 0) {
    sta2 = 1;
    digitalWrite(buzzer, HIGH);
    delay(1000);
    digitalWrite(buzzer, LOW);
    detection = true;
    Firebase.RTDB.setBool(&fbdo, "/live_data/alart", true);
    message = "Patient Emergency";
    notify();
    history();
  } else if (btn == HIGH && sta2 == 1) {
    delay(5000);
    sta2 = 0;
  }

}
void cell1Read() {
  if (scale.wait_ready_timeout(200)) {
    reading = round(scale.get_units());
    if (reading != lastReading) {
      displayWeight1(reading);
      read1 = map(reading, 0, 500, 0, 100);
      Firebase.RTDB.setInt(&fbdo, "/live_data/bottle-level", read1);
      if (read1 < 10 && sta1 == 0) {
        sta1 = 1;
        message = "Bottle Level Low";
        notify();
      }
      if (read1 > 50) {
        digitalWrite(led3, LOW);
        digitalWrite(led2, LOW);
        digitalWrite(led1, HIGH);
        
      } else if (read1 < 50 && read1 > 20) {
        digitalWrite(led3, LOW);
        digitalWrite(led1, LOW);
        digitalWrite(led2, HIGH);
      } else {
        digitalWrite(led3, HIGH);
        digitalWrite(led2, LOW);
        digitalWrite(led1, LOW);
      }
    }
    lastReading = reading;
  }
  else {
    Serial.println("HX711 1 not found.");
  }
}
void flowread() {

  currentMillis = millis();
  if (currentMillis - previousMillis > interval) {

    pulse1Sec = pulseCount;
    pulseCount = 0;
    flowRate = ((1000.0 / (millis() - previousMillis)) * pulse1Sec) / calibrationFactor;
    previousMillis = millis();
    flowMilliLitres = (flowRate / 60) * 1000;
    totalMilliLitres += flowMilliLitres;

    Serial.print("Flow rate: ");
    Serial.print(int(flowRate));
    Serial.print("L/min");
    Serial.print("\t");

    Serial.print("Output Liquid Quantity: ");
    Serial.print(totalMilliLitres);
    Serial.print("mL / ");
    Serial.print(totalMilliLitres / 1000);
    Serial.println("L");

    Firebase.RTDB.setInt(&fbdo, "/live_data/flow-rate", int(flowRate));
    Firebase.RTDB.setInt(&fbdo, "/live_data/flow-usage", totalMilliLitres);
  }
}
void notify() {
  Firebase.RTDB.setString(&fbdo, "/notification/message", message);
  delay(200);
  Firebase.RTDB.setBool(&fbdo, "/notification/istrue", true);
  delay(200);
}
void history() {
  String histry = historyPath + "/" + String(timestamp) + "000";
  json.set("/alart", detection);
  json.set("/bottle-gram", reading);
  json.set("/bottle-level", read1);
  json.set("/flow-rate", int(flowRate));
  json.set("/flow-usage", totalMilliLitres);
  json.set("/timestamp", timestamp.toInt());
  Serial.printf("Set json... %s\n", Firebase.RTDB.setJSON(&fbdo, histry.c_str(), &json) ? "ok" : fbdo.errorReason().c_str());
}
void Display() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(25, 0);
  display.println("Drip Watch");
  display.setCursor(0, 20);
  display.print("Alart : ");
  display.println(detection);
  display.print("Bottle-Level : ");
  display.print(read1);
  display.println(" %");
  display.print("Flow-Rate : ");
  display.print(int(flowRate));
  display.println(" L/min");
  display.print("Flow-Usage : ");
  display.print(totalMilliLitres);
  display.println(" mL");
  display.display();
}
