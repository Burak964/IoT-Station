// ESP_A_SENDER_BMP280_WIFIMANAGER_ESPNOW.ino
// ESP A: WiFiManager + BMP280, sendet per ESP-NOW an ESP B

#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>

Adafruit_BMP280 bmp;

typedef struct __attribute__((packed)) {
  uint32_t seq;
  float tempC;
  float pressurePa;
} SensorPacket;

uint32_t seqNo = 0;

// MAC von ESP B
uint8_t receiverMac[6] = {0x00, 0x70, 0x07, 0x84, 0x9F, 0x4C};

static void setWiFiChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

static void initEspNow(uint8_t channel) {
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init fehlgeschlagen");
    while (true) delay(1000);
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverMac, 6);
  peer.channel = channel;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Peer add fehlgeschlagen");
    while (true) delay(1000);
  }

  Serial.println("ESP-NOW Peer hinzugefügt.");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(21, 22); // SDA = 21, SCL = 22

  if (!bmp.begin(0x76)) {
    Serial.println("BMP280 nicht auf 0x76 gefunden, teste 0x77...");

    if (!bmp.begin(0x77)) {
      Serial.println("BMP280 nicht gefunden. Verkabelung prüfen!");
      while (true) delay(1000);
    }
  }

  Serial.println("BMP280 gefunden!");

  WiFi.mode(WIFI_STA);

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);

  bool ok = wm.autoConnect("ESP-A-Setup");

  if (!ok) {
    Serial.println("WiFiManager nicht verbunden.");
  } else {
    Serial.println("WLAN verbunden.");
  }

  Serial.print("ESP A MAC: ");
  Serial.println(WiFi.macAddress());

  uint8_t ch = WiFi.channel();
  if (ch == 0) ch = 6;

  Serial.print("WiFi Kanal: ");
  Serial.println(ch);

  setWiFiChannel(ch);
  initEspNow(ch);

  Serial.println("ESP A bereit. Sende Daten...");
}

void loop() {
  SensorPacket p;
  p.seq = ++seqNo;
  p.tempC = bmp.readTemperature();
  p.pressurePa = bmp.readPressure();

  esp_err_t r = esp_now_send(receiverMac, (uint8_t*)&p, sizeof(p));

  Serial.print("Sende Paket ");
  Serial.print(p.seq);
  Serial.print(" | Temp: ");
  Serial.print(p.tempC);
  Serial.print(" C | Druck: ");
  Serial.print(p.pressurePa);
  Serial.print(" Pa | Status: ");

  if (r == ESP_OK) {
    Serial.println("OK");
  } else {
    Serial.print("Fehler ");
    Serial.println((int)r);
  }

  delay(2000);
}      
