// ESP_B_RECEIVER_WEB_GRAPH_BUZZER

#include <WiFi.h>
#include <esp_now.h>

#define BUZZER_PIN 27

typedef struct __attribute__((packed)) {
  uint32_t seq;
  float tempC;
  float pressurePa;
} SensorPacket;

SensorPacket data;

void onRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  memcpy(&data, incomingData, sizeof(data));

  Serial.print("Temp: ");
  Serial.println(data.tempC);

  if (data.tempC > 35) {
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Fehler bei ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(onRecv);
}

void loop() {
}
