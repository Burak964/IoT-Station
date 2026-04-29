# IoT-Station
SYT ITP Projekt – ESP32 IoT Station mit BMP280, ESP-NOW und Webserver

##  Fach
Systemtechnik (SYT)

## Gruppe
- Burak Topcu
- Parsa Risherihzade

---

# Projektziel

Ziel dieses Projekts war es, zwei ESP32 Mikrocontroller zu einer IoT-Station zu entwickeln.

Ein ESP32 (Sender) misst Temperatur und Luftdruck mit einem **BMP280 Sensor** und überträgt die Daten mittels **ESP-NOW** an einen zweiten ESP32 (Receiver).Es stellt auch WIFI-Manager zur verfügung.

Der Receiver:
- visualisiert die Daten in einem Webinterface
- speichert historische Durchschnittswerte
- steuert einen Buzzer als Aktor

---

# Systemarchitektur

BMP280 → ESP32 A (Sender) → ESP-NOW → ESP32 B (Receiver) → Webserver + Graph + API + Buzzer

---

# Verwendete Komponenten

| Komponente | Anzahl | Beschreibung |
|------------|--------|--------------|
| ESP32 Dev Board | 2 | Sender & Receiver |
| BMP280 | 1 | Temperatur & Drucksensor |
| Allnet Buzzer | 1 | Akustischer Aktor |
| Breadboard | 1 | Steckbrett |
| Jumper Kabel | mehrere | Verbindungen |
| USB Kabel | 2 | Stromversorgung |

---

# Sensor – BMP280

Der BMP280 misst:

- Temperatur (°C)
- Luftdruck (Pa)

Messintervall:
- Alle 2 Sekunden

# 🔌 Schaltplan

## Sender (ESP A + BMP280)

Der Sender besteht aus einem ESP32 und einem BMP280 Sensor.

Verdrahtung:

| BMP280 | ESP32 |
|--------|--------|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

Schaltplan:

![Sender Schaltplan](schaltplan/Sender_Schaltplan.png)

---

## Receiver (ESP B + Buzzer)

Der Receiver besteht aus einem ESP32 und einem Allnet Buzzer.

Verdrahtung:

| Buzzer | ESP32 |
|--------|--------|
| Signal | GPIO 27 |
| GND | GND |

Schaltplan:

![Empfänger Schaltplan](schaltplan/Empfänger_Schaltplan.png)

# Programmcode:
## ESP A – Sender mit BMP280, WiFiManager und ESP-NOW
Der Sender liest alle 2 Sekunden Temperatur und Luftdruck vom BMP280 aus und sendet die Werte per ESP-NOW an den Receiver.

```cpp
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

// MAC von ESP B hier eintragen
uint8_t receiverMac[6] = {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC};

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
}

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);

  if (!bmp.begin(0x76)) {
    Serial.println("BMP280 nicht gefunden. Teste 0x76 oder 0x77.");
    while (true) delay(1000);
  }

  WiFi.mode(WIFI_STA);

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  bool ok = wm.autoConnect("ESP-A-Setup");

  if (!ok) {
    Serial.println("WiFiManager nicht verbunden.");
  }

  Serial.print("ESP A MAC: ");
  Serial.println(WiFi.macAddress());

  uint8_t ch = WiFi.channel();
  if (ch == 0) ch = 6;

  Serial.print("WiFi Kanal: ");
  Serial.println(ch);

  setWiFiChannel(ch);
  initEspNow(ch);
}

void loop() {
  SensorPacket p;
  p.seq = ++seqNo;
  p.tempC = bmp.readTemperature();
  p.pressurePa = bmp.readPressure();

  esp_err_t r = esp_now_send(receiverMac, (uint8_t*)&p, sizeof(p));

  if (r != ESP_OK) {
    Serial.print("Send error: ");
    Serial.println((int)r);
  }

  delay(2000);
}

---
