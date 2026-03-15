#include <WiFi.h>

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASS";

// ESP32 -> CD74HCT4067
// Seed Studio ESP32C6 Dev Board
const int PIN_S0 = 0; // D0
const int PIN_S1 = 1; // D1
const int PIN_S2 = 2; // D2
const int PIN_S3 = 21; // D3 - S3 pin je na ESP32 pripojený cez 10k pull-up rezistor, takže ho môžeme použiť aj ako výstup
const int PIN_EN = 22; // D4  // active LOW

// dĺžka pulzu na zvolenie stupňa
const unsigned long PULSE_MS = 10000;

// čas medzi zmenami stupňa
const unsigned long STAGE_HOLD_MS = 10000;

void setMuxChannel(int channel) {
  digitalWrite(PIN_S0, (channel >> 0) & 0x01);
  digitalWrite(PIN_S1, (channel >> 1) & 0x01);
  digitalWrite(PIN_S2, (channel >> 2) & 0x01);
  digitalWrite(PIN_S3, (channel >> 3) & 0x01);
}

void muxDisable() {
  digitalWrite(PIN_EN, HIGH);
}

void muxEnable() {
  digitalWrite(PIN_EN, LOW);
}

void pulseStage(int stage) {
  Serial.print("Selecting stage: ");
  Serial.println(stage);

  muxDisable();
  delay(20);

  setMuxChannel(stage);
  delay(20);

  muxEnable();
  delay(PULSE_MS);

  muxDisable();
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected, IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_S0, OUTPUT);
  pinMode(PIN_S1, OUTPUT);
  pinMode(PIN_S2, OUTPUT);
  pinMode(PIN_S3, OUTPUT);
  pinMode(PIN_EN, OUTPUT);

  muxDisable();
  setMuxChannel(0);

  connectWifi();

  Serial.println("Init stage 0...");
  pulseStage(0);
  delay(1000);
}

void loop() {
  for (int stage = 1; stage <= 11; stage++) {
    pulseStage(stage);
    delay(STAGE_HOLD_MS);
  }

  Serial.println("Back to stage 0...");
  pulseStage(0);
  delay(STAGE_HOLD_MS);
}