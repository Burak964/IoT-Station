// ESP_B_RECEIVER_WEB_GRAPH_BUZZER_CORE2.ino
// ESP B: Empfängt Temperatur und Druck per ESP-NOW, zeigt Webinterface mit Graph und steuert Buzzer
// ESP32 Core 2.x kompatibel

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

const char* WIFI_SSID = "iPhone von Leon";
const char* WIFI_PASS = "Jonmusa123";

#define BUZZER_PIN 27
#define BUZZER_ACTIVE_HIGH 1

AsyncWebServer server(80);

// ESP A MAC hier eintragen
// WICHTIG: Das muss die MAC-Adresse von ESP A sein
static const uint8_t ESP_A_MAC[6] = {0x00, 0x70, 0x07, 0x19, 0xF6, 0x68};

typedef struct __attribute__((packed)) {
  uint32_t seq;
  float tempC;
  float pressurePa;
} SensorPacket;

template <size_t N>
struct Ring {
  float temp[N];
  float pressure[N];
  uint32_t time[N];

  size_t head = 0;
  size_t count = 0;

  void push(float tempValue, float pressureValue, uint32_t ts) {
    temp[head] = tempValue;
    pressure[head] = pressureValue;
    time[head] = ts;

    head = (head + 1) % N;

    if (count < N) {
      count++;
    }
  }

  size_t size() const {
    return count;
  }

  void get(size_t i, float &tempValue, float &pressureValue, uint32_t &ts) const {
    size_t start = (head + N - count) % N;
    size_t idx = (start + i) % N;

    tempValue = temp[idx];
    pressureValue = pressure[idx];
    ts = time[idx];
  }
};

// Letzte Stunde bei 2-Minuten-Werten: 30 Werte
Ring<30> history2m;

// Letzte Woche bei 1-Stunden-Werten: 168 Werte
Ring<168> history1h;

float accTemp = 0.0f;
float accPressure = 0.0f;
uint32_t accCount = 0;
uint32_t bucketStartMs = 0;

float accTempHour = 0.0f;
float accPressureHour = 0.0f;
uint32_t accCountHour = 0;
uint32_t hourBucketStartMs = 0;

volatile bool gotSample = false;
volatile SensorPacket lastSample;

static inline uint32_t nowSeconds() {
  return (uint32_t)(millis() / 1000);
}

static inline void buzzerOn() {
  digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_HIGH ? HIGH : LOW);
}

static inline void buzzerOff() {
  digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_HIGH ? LOW : HIGH);
}

static bool isFromA(const uint8_t* mac) {
  return memcmp(mac, ESP_A_MAC, 6) == 0;
}

static void addToAggregates(float tempC, float pressurePa) {
  uint32_t ms = millis();

  if (bucketStartMs == 0) {
    bucketStartMs = ms;
  }

  accTemp += tempC;
  accPressure += pressurePa;
  accCount++;

  // Alle 2 Minuten Durchschnitt speichern
  if (ms - bucketStartMs >= 2UL * 60UL * 1000UL) {
    float avgTemp2m = accCount ? (accTemp / (float)accCount) : tempC;
    float avgPressure2m = accCount ? (accPressure / (float)accCount) : pressurePa;

    // Druck wird von Pa in hPa umgerechnet
    history2m.push(avgTemp2m, avgPressure2m / 100.0f, nowSeconds());

    if (hourBucketStartMs == 0) {
      hourBucketStartMs = ms;
    }

    accTempHour += avgTemp2m;
    accPressureHour += avgPressure2m;
    accCountHour++;

    // Alle 1 Stunde Durchschnitt speichern
    if (ms - hourBucketStartMs >= 60UL * 60UL * 1000UL) {
      float avgTemp1h = accCountHour ? (accTempHour / (float)accCountHour) : avgTemp2m;
      float avgPressure1h = accCountHour ? (accPressureHour / (float)accCountHour) : avgPressure2m;

      // Druck wird von Pa in hPa umgerechnet
      history1h.push(avgTemp1h, avgPressure1h / 100.0f, nowSeconds());

      accTempHour = 0.0f;
      accPressureHour = 0.0f;
      accCountHour = 0;
      hourBucketStartMs = ms;
    }

    accTemp = 0.0f;
    accPressure = 0.0f;
    accCount = 0;
    bucketStartMs = ms;
  }
}

static String seriesJson2m() {
  String s = "[";

  for (size_t i = 0; i < history2m.size(); i++) {
    float tempValue;
    float pressureValue;
    uint32_t ts;

    history2m.get(i, tempValue, pressureValue, ts);

    if (i) {
      s += ",";
    }

    s += "{\"t\":";
    s += String(ts);
    s += ",\"temp\":";
    s += String(tempValue, 2);
    s += ",\"pressure\":";
    s += String(pressureValue, 2);
    s += "}";
  }

  s += "]";
  return s;
}

static String seriesJson1h() {
  String s = "[";

  for (size_t i = 0; i < history1h.size(); i++) {
    float tempValue;
    float pressureValue;
    uint32_t ts;

    history1h.get(i, tempValue, pressureValue, ts);

    if (i) {
      s += ",";
    }

    s += "{\"t\":";
    s += String(ts);
    s += ",\"temp\":";
    s += String(tempValue, 2);
    s += ",\"pressure\":";
    s += String(pressureValue, 2);
    s += "}";
  }

  s += "]";
  return s;
}

// ESP32 Core 2.x Callback Signatur
void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (!isFromA(mac)) {
    return;
  }

  if (len != (int)sizeof(SensorPacket)) {
    return;
  }

  memcpy((void*)&lastSample, data, sizeof(SensorPacket));
  gotSample = true;
}

const char INDEX_HTML[] PROGMEM =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>ESP32 Temperatur und Druck</title>"
"<style>"
"body{font-family:Arial;background:#151818;color:white;margin:20px;}"
"button{margin:4px;padding:8px 12px;border:0;border-radius:5px;background:#666;color:white;}"
".card{background:#222;padding:12px;border-radius:8px;margin-bottom:12px;}"
"canvas{background:#111;border-radius:8px;}"
"</style>"
"</head><body>"
"<h2>ESP32 Temperatur und Druck</h2>"

"<div class='card'>"
"<p><b>Aktuelle Werte:</b></p>"
"<p id='live'>Warte auf Daten...</p>"
"</div>"

"<div>"
"<button onclick='load2m()'>Letzte Stunde (2min)</button>"
"<button onclick='load1h()'>Letzte Woche (1h)</button>"
"<button onclick='buzz(1)'>Buzzer an</button>"
"<button onclick='buzz(0)'>Buzzer aus</button>"
"</div>"

"<canvas id='c' width='900' height='420'></canvas>"

"<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
"<script>"
"let chart;"

"function render(points,label){"
"const labels=points.map(p=>p.t+'s');"
"const temps=points.map(p=>p.temp);"
"const pressures=points.map(p=>p.pressure);"

"if(points.length>0){"
"let last=points[points.length-1];"
"document.getElementById('live').innerHTML="
"'Temperatur: '+last.temp.toFixed(2)+' °C<br>Druck: '+last.pressure.toFixed(2)+' hPa';"
"}else{"
"document.getElementById('live').innerHTML='Noch keine Durchschnittswerte vorhanden. Bitte ca. 2 Minuten warten.';"
"}"

"const ctx=document.getElementById('c').getContext('2d');"
"if(chart) chart.destroy();"

"chart=new Chart(ctx,{"
"type:'line',"
"data:{"
"labels:labels,"
"datasets:["
"{label:'Temperatur °C',data:temps,yAxisID:'yTemp',borderWidth:2,tension:0.2},"
"{label:'Druck hPa',data:pressures,yAxisID:'yPressure',borderWidth:2,tension:0.2}"
"]"
"},"
"options:{"
"responsive:true,"
"animation:false,"
"interaction:{mode:'index',intersect:false},"
"scales:{"
"yTemp:{type:'linear',position:'left',title:{display:true,text:'Temperatur °C'}},"
"yPressure:{type:'linear',position:'right',title:{display:true,text:'Druck hPa'},grid:{drawOnChartArea:false}}"
"}"
"}"
"});"
"}"

"async function load2m(){"
"const r=await fetch('/api/series?res=2m');"
"const j=await r.json();"
"render(j,'2min');"
"}"

"async function load1h(){"
"const r=await fetch('/api/series?res=1h');"
"const j=await r.json();"
"render(j,'1h');"
"}"

"async function buzz(on){"
"await fetch(on?'/api/buzzer?on=1':'/api/buzzer?off=1');"
"}"

"load2m();"
"setInterval(load2m,15000);"
"</script>"
"</body></html>";

static void setupWeb() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/api/series", HTTP_GET, [](AsyncWebServerRequest *req) {
    String res = "2m";

    if (req->hasParam("res")) {
      res = req->getParam("res")->value();
    }

    if (res == "1h") {
      req->send(200, "application/json", seriesJson1h());
    } else {
      req->send(200, "application/json", seriesJson2m());
    }
  });

  server.on("/api/buzzer", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (req->hasParam("on")) {
      buzzerOn();
    }

    if (req->hasParam("off")) {
      buzzerOff();
    }

    req->send(200, "text/plain", "ok");
  });

  server.begin();
  Serial.println("Webserver gestartet.");
}

void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  buzzerOff();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Verbinde WLAN");

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();

  Serial.print("ESP B IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("ESP B MAC: ");
  Serial.println(WiFi.macAddress());

  Serial.print("WiFi Kanal: ");
  Serial.println(WiFi.channel());

  uint8_t ch = WiFi.channel();

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init fehlgeschlagen");
    while (true) {
      delay(1000);
    }
  }

  esp_now_register_recv_cb(onRecv);

  setupWeb();

  Serial.println("ESP B bereit. Browser: http://<ESP_B_IP>/");
}

void loop() {
  if (gotSample) {
    gotSample = false;

    float temp = lastSample.tempC;
    float pressurePa = lastSample.pressurePa;

    addToAggregates(temp, pressurePa);

    if (temp > 35.0f) {
      buzzerOn();
    } else {
      buzzerOff();
    }

    Serial.print("Temp: ");
    Serial.print(temp, 2);
    Serial.print(" C | Druck: ");
    Serial.print(pressurePa / 100.0f, 2);
    Serial.println(" hPa");
  }

  delay(10);
}  
