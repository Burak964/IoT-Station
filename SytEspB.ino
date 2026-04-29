// ESP_B_RECEIVER_WEB_GRAPH_BUZZER_CORE2.ino
// ESP32 Core 2.x kompatibel
// Empfängt SensorPacket (Temp + Pressure) via ESP-NOW von ESP A
// Aggregiert automatisch: 5min-Mittelwerte (letzte Stunde) + 1h-Mittelwerte (letzte Woche)
// Webinterface: 2 Linien (Temp °C + Druck hPa) im selben Graph + Buzzer an/aus

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

const char* WIFI_SSID = "Batman 🦇";
const char* WIFI_PASS = "King0000";

#define BUZZER_PIN 27
#define BUZZER_ACTIVE_HIGH 1

AsyncWebServer server(80);

// ESP A MAC (MUSS zu deinem ESP-A passen, sonst werden Pakete verworfen!)
static const uint8_t ESP_A_MAC[6] = {0x00, 0x70, 0x07, 0x25, 0xE4, 0x88};

typedef struct __attribute__((packed)) {
  uint32_t seq;
  float tempC;
  float pressurePa;
} SensorPacket;

template <size_t N>
struct Ring {
  float v[N];
  uint32_t t[N];
  size_t head = 0;
  size_t count = 0;

  void push(float value, uint32_t ts) {
    v[head] = value;
    t[head] = ts;
    head = (head + 1) % N;
    if (count < N) count++;
  }

  size_t size() const { return count; }

  void get(size_t i, float &value, uint32_t &ts) const {
    size_t start = (head + N - count) % N;
    size_t idx = (start + i) % N;
    value = v[idx];
    ts = t[idx];
  }
};

// Temperatur-Historie
Ring<12>  temp5m;   // 12 * 5min = 60min
Ring<168> temp1h;   // 168 * 1h  = 1 Woche

// Druck-Historie (Pa intern)
Ring<12>  press5m;
Ring<168> press1h;

// 5min Akkus
float accTemp = 0.0f;
uint32_t accCount = 0;

float accPress = 0.0f;
uint32_t accPressCount = 0;

uint32_t bucketStartMs = 0;

// 1h Akkus (aus 5min-Mittelwerten)
float accTempHour = 0.0f;
uint32_t accCountHour = 0;

float accPressHour = 0.0f;
uint32_t accPressCountHour = 0;

uint32_t hourBucketStartMs = 0;

volatile bool gotSample = false;
volatile SensorPacket lastSample;

static inline uint32_t nowSeconds() {
  return (uint32_t)(millis() / 1000);
}

static inline void buzzerOn()  { digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_HIGH ? HIGH : LOW); }
static inline void buzzerOff() { digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_HIGH ? LOW : HIGH); }

static bool isFromA(const uint8_t* mac) {
  return memcmp(mac, ESP_A_MAC, 6) == 0;
}

// Aggregiert Temp + Pressure automatisch in 5min und 1h Buckets
static void addToAggregates(float tempC, float pressurePa) {
  uint32_t ms = millis();
  if (bucketStartMs == 0) bucketStartMs = ms;

  // 5min Akkus füllen
  accTemp += tempC;
  accCount++;

  accPress += pressurePa;
  accPressCount++;

  // alle 5 Minuten mitteln und speichern
  if (ms - bucketStartMs >= 5UL * 60UL * 1000UL) {
    float avg5mT = accCount ? (accTemp / (float)accCount) : tempC;
    float avg5mP = accPressCount ? (accPress / (float)accPressCount) : pressurePa;

    uint32_t ts = nowSeconds();
    temp5m.push(avg5mT, ts);
    press5m.push(avg5mP, ts);

    // 1h Akkus füttern (aus 5min-Mitteln)
    if (hourBucketStartMs == 0) hourBucketStartMs = ms;

    accTempHour += avg5mT;
    accCountHour++;

    accPressHour += avg5mP;
    accPressCountHour++;

    // alle 60 Minuten mitteln und speichern
    if (ms - hourBucketStartMs >= 60UL * 60UL * 1000UL) {
      float avg1hT = accCountHour ? (accTempHour / (float)accCountHour) : avg5mT;
      float avg1hP = accPressCountHour ? (accPressHour / (float)accPressCountHour) : avg5mP;

      uint32_t ts2 = nowSeconds();
      temp1h.push(avg1hT, ts2);
      press1h.push(avg1hP, ts2);

      accTempHour = 0.0f; accCountHour = 0;
      accPressHour = 0.0f; accPressCountHour = 0;
      hourBucketStartMs = ms;
    }

    // 5min reset
    accTemp = 0.0f; accCount = 0;
    accPress = 0.0f; accPressCount = 0;
    bucketStartMs = ms;
  }
}

// --- JSON helpers (Press wird in hPa zurückgegeben: hPa = Pa/100) ---
static String arrayJsonTemp5m() {
  String s="[";
  for (size_t i=0;i<temp5m.size();i++){
    float v; uint32_t t; temp5m.get(i,v,t);
    if(i) s+=",";
    s += "{\"t\":" + String(t) + ",\"v\":" + String(v,2) + "}";
  }
  s+="]";
  return s;
}

static String arrayJsonTemp1h() {
  String s="[";
  for (size_t i=0;i<temp1h.size();i++){
    float v; uint32_t t; temp1h.get(i,v,t);
    if(i) s+=",";
    s += "{\"t\":" + String(t) + ",\"v\":" + String(v,2) + "}";
  }
  s+="]";
  return s;
}

static String arrayJsonPress5m() {
  String s="[";
  for (size_t i=0;i<press5m.size();i++){
    float v; uint32_t t; press5m.get(i,v,t);
    if(i) s+=",";
    s += "{\"t\":" + String(t) + ",\"v\":" + String(v/100.0f,2) + "}";
  }
  s+="]";
  return s;
}

static String arrayJsonPress1h() {
  String s="[";
  for (size_t i=0;i<press1h.size();i++){
    float v; uint32_t t; press1h.get(i,v,t);
    if(i) s+=",";
    s += "{\"t\":" + String(t) + ",\"v\":" + String(v/100.0f,2) + "}";
  }
  s+="]";
  return s;
}

static String seriesJsonBoth5m() {
  return String("{\"temp\":") + arrayJsonTemp5m() + ",\"press\":" + arrayJsonPress5m() + "}";
}
static String seriesJsonBoth1h() {
  return String("{\"temp\":") + arrayJsonTemp1h() + ",\"press\":" + arrayJsonPress1h() + "}";
}

// Core 2.x Callback Signatur
void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (!isFromA(mac)) return;                 // <- wenn Probleme: testweise auskommentieren
  if (len != (int)sizeof(SensorPacket)) return;

  memcpy((void*)&lastSample, data, sizeof(SensorPacket));
  gotSample = true;
}

const char INDEX_HTML[] PROGMEM =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>ESP32 Historie</title></head><body>"
"<h3>Temperatur + Druck Historie</h3>"
"<div>"
"<button onclick='load5m()'>Letzte Stunde (5min)</button>"
"<button onclick='load1h()'>Letzte Woche (1h)</button>"
"<button onclick='buzz(1)'>Buzzer an</button>"
"<button onclick='buzz(0)'>Buzzer aus</button>"
"</div>"
"<canvas id='c' width='360' height='220'></canvas>"
"<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
"<script>"
"let chart;"
"function render(payload,labelSuffix){"
" const t = payload.temp || [];"
" const p = payload.press || [];"
" const xs = t.map(x=>x.t);"
" const ysT = t.map(x=>x.v);"
" const ysP = p.map(x=>x.v);"
" const ctx=document.getElementById('c').getContext('2d');"
" if(chart) chart.destroy();"
" chart=new Chart(ctx,{"
"  type:'line',"
"  data:{"
"   labels:xs,"
"   datasets:["
"    {label:'Temp \\u00B0C '+labelSuffix,data:ysT},"
"    {label:'Druck hPa '+labelSuffix,data:ysP}"
"   ]"
"  },"
"  options:{responsive:true,animation:false}"
" });"
"}"
"async function load5m(){"
" const r=await fetch('/api/series?res=5m');"
" const j=await r.json();"
" render(j,'(5m)');"
"}"
"async function load1h(){"
" const r=await fetch('/api/series?res=1h');"
" const j=await r.json();"
" render(j,'(1h)');"
"}"
"async function buzz(on){await fetch(on?'/api/buzzer?on=1':'/api/buzzer?off=1');}"
"load5m();setInterval(load5m,15000);"
"</script>"
"</body></html>";

static void setupWeb() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", INDEX_HTML);
  });

  // liefert immer beide Serien (Temp + Druck), je nach res=5m oder res=1h
  server.on("/api/series", HTTP_GET, [](AsyncWebServerRequest *req){
    String res = "5m";
    if (req->hasParam("res")) res = req->getParam("res")->value();
    if (res == "1h") req->send(200, "application/json", seriesJsonBoth1h());
    else            req->send(200, "application/json", seriesJsonBoth5m());
  });

  server.on("/api/buzzer", HTTP_GET, [](AsyncWebServerRequest *req){
    if (req->hasParam("on")) buzzerOn();
    if (req->hasParam("off")) buzzerOff();
    req->send(200, "text/plain", "ok");
  });

  server.begin();
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
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onRecv);

  setupWeb();

  Serial.println("ESP B bereit. Browser: http://<ESP_B_IP>/");
}

void loop() {
  if (gotSample) {
    gotSample = false;

    float t = lastSample.tempC;
    float p = lastSample.pressurePa;

    addToAggregates(t, p);

    // Buzzer-Logik (nur Temperatur)
    if (t > 35.0f) buzzerOn();
    else buzzerOff();

    Serial.print("Temp: ");
    Serial.print(t, 2);
    Serial.print(" C, Druck: ");
    Serial.print(p / 100.0f, 2);
    Serial.println(" hPa");
  }

  delay(10);
}
