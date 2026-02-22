// -------- PINS --------
#define TRIG_FRONT D1
#define ECHO_FRONT D2
#define TRIG_DOWN  D5
#define ECHO_DOWN  D8
#define IR_LEFT  D0
#define IR_RIGHT D4
#define BUZZER D3
#define MOTOR  D7
#define LED_PIN 3     // RX
#define SOS_BUTTON D6

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ---------------- BACKEND URL ----------------
String backendURL = "http://10.184.46.160:3000/settings"; // Change to your PC IP

// ---------------- WIFI ----------------
String wifiSSID = "Subrat";
String wifiPassword = "6371365429";

// ---------------- TELEGRAM ----------------
String botToken = "8153986672:AAH0fc5NKPpUBG1_rCGwXxxmpg-g8PyhqVk";
String chatID = "6890635081";

// ---------------- SENSOR THRESHOLDS ----------------
int frontThreshold = 60;
int downThreshold  = 100;

// ---------------- FLAGS ----------------
unsigned long lastSettingsFetch = 0;
unsigned long lastWifiCheck = 0;
bool wifiConnected = false;

// ====================== FUNCTIONS ======================
long getDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW); delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  return (duration * 0.034 / 2);
}

void connectWiFi() {
  Serial.print("Connecting to WiFi: "); Serial.println(wifiSSID);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500); Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());
    wifiConnected = true;
  } else {
    Serial.println("\nWiFi Failed!");
    wifiConnected = false;
  }
}

void fetchSettings() {
  if (!wifiConnected) return;

  WiFiClient client;
  HTTPClient http;

  http.begin(client, backendURL);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    StaticJsonDocument<500> doc;
    deserializeJson(doc, payload);

    // Update thresholds & credentials from backend
    frontThreshold = doc["frontThreshold"] | 60;
    downThreshold  = doc["downThreshold"] | 100;
    botToken = String(doc["botToken"].as<const char*>());
    chatID   = String(doc["chatID"].as<const char*>());
    wifiSSID = String(doc["wifiSSID"].as<const char*>());
    wifiPassword = String(doc["wifiPassword"].as<const char*>());

    Serial.println("--- Settings updated ---");
    Serial.println(payload);
  } else {
    Serial.print("Error fetching settings, HTTP code: "); Serial.println(code);
  }
  http.end();
}

void sendLiveData(long front, long down, int leftIR, int rightIR) {
  if (!wifiConnected) return;

  WiFiClient client;
  HTTPClient http;
  String url = backendURL;
  url.replace("/settings", "/live");

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"frontDistance\":" + String(front) + ",";
  json += "\"downDistance\":" + String(down) + ",";
  json += "\"leftIR\":" + String(leftIR) + ",";
  json += "\"rightIR\":" + String(rightIR) + ",";
  json += "\"wifiStatus\":\"Connected\"}";
  
  http.POST(json);
  http.end();
}

// ---------------- TELEGRAM SOS MESSAGE ----------------
void sendSOSMessage() {
  if (!wifiConnected || botToken == "" || chatID == "") return;

  WiFiClientSecure client;
  client.setInsecure(); // ignore SSL certs

  HTTPClient https;
  String url = "https://api.telegram.org/bot" + botToken + "/sendMessage?chat_id=" + chatID + "&text=🚨 SOS ALERT!\n https://www.google.com/maps?q=19.049828,83.833692";

  https.begin(client, url);
  int httpCode = https.GET();
  if (httpCode > 0) {
    Serial.print("SOS Message sent, HTTP code: "); Serial.println(httpCode);
  } else {
    Serial.print("Error sending SOS: "); Serial.println(https.errorToString(httpCode).c_str());
  }
  https.end();
}

// ====================== SETUP ======================
void setup() {
  Serial.begin(9600);

  pinMode(SOS_BUTTON, INPUT_PULLUP);
  pinMode(TRIG_FRONT, OUTPUT); pinMode(ECHO_FRONT, INPUT);
  pinMode(TRIG_DOWN, OUTPUT);  pinMode(ECHO_DOWN, INPUT);
  pinMode(IR_LEFT, INPUT); pinMode(IR_RIGHT, INPUT);
  pinMode(BUZZER, OUTPUT); pinMode(MOTOR, OUTPUT); pinMode(LED_PIN, OUTPUT);
  digitalWrite(BUZZER, LOW); digitalWrite(MOTOR, LOW); digitalWrite(LED_PIN, LOW);

  connectWiFi();
  fetchSettings();
}

// ====================== LOOP ======================
void loop() {
  // --- WIFI reconnect ---
  if (millis() - lastWifiCheck > 5000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
  }

  // --- Fetch settings from backend every 10 seconds ---
  if (millis() - lastSettingsFetch > 10000) {
    lastSettingsFetch = millis();
    fetchSettings();
  }

  // --- Read sensors ---
  long frontDist = getDistance(TRIG_FRONT, ECHO_FRONT);
  long downDist  = getDistance(TRIG_DOWN, ECHO_DOWN);
  int leftIR = digitalRead(IR_LEFT);
  int rightIR = digitalRead(IR_RIGHT);

  // --- Obstacle & Pit detection with dynamic beep speed ---
  bool obstacle = (frontDist > 0 && frontDist < frontThreshold) || leftIR == LOW || rightIR == LOW;
  bool pit = downDist > downThreshold;

  if (pit || obstacle) {
    long closestDist = pit ? downDist : frontDist;  // choose the distance that triggered
    int beepDelay = 500; // default

    // Make beep faster as object gets closer
    if (closestDist < frontThreshold) {
      beepDelay = map(closestDist, 0, frontThreshold, 100, 500); // 100ms = fast, 500ms = slow
    }

    tone(BUZZER, 1000);          // buzzer ON
    digitalWrite(MOTOR, HIGH);   // motor ON
    digitalWrite(LED_PIN, HIGH); // LED ON
    delay(beepDelay);
    noTone(BUZZER);              // buzzer OFF
  } 
  else {
    noTone(BUZZER);
    digitalWrite(MOTOR, LOW);
    digitalWrite(LED_PIN, LOW);
  }

  // --- Send live data ---
  sendLiveData(frontDist, downDist, leftIR, rightIR);

  // --- SOS BUTTON CHECK ---
  if (digitalRead(SOS_BUTTON) == LOW) { // button pressed (active low)
    Serial.println("SOS Button Pressed!");
    sendSOSMessage();
    delay(1000); // debounce & avoid multiple sends
  }
}
