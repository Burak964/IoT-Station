// ESP_B_RECEIVER_WEB_GRAPH_BUZZER_CORE2.ino
// ESP32 Core 2.x kompatibel

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

// ESP A MAC
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

Ring<12>  temp5m;
Ring<168> temp1h;

float accTemp = 0.0f;
uint32_t accCount = 0;
uint32_t bucketStartMs = 0;

float accTempHour = 0.0f;
uint32_t accCountHour = 0;
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

static void addToAggregates(float tempC) {
  uint32_t ms = millis();

  if (bucketStartMs == 0) bucketStartMs = ms;
  accTemp += tempC;
  accCount++;

  if (ms - bucketStartMs >= 5UL * 60UL * 1000UL) {
    float avg5m = accCount ? (accTemp / (float)accCount) : tempC;
    temp5m.push(avg5m, nowSeconds());

    if (hourBucketStartMs == 0) hourBucketStartMs = ms;
    accTempHour += avg5m;
    accCountHour++;

    if (ms - hourBucketStartMs >= 60UL * 60UL * 1000UL) {
      float avg1h = accCountHour ? (accTempHour / (float)accCountHour) : avg5m;
      temp1h.push(avg1h, nowSeconds());
      accTempHour = 0.0f;
      accCountHour = 0;
      hourBucketStartMs = ms;
    }

    accTemp = 0.0f;
    accCount = 0;
    bucketStartMs = ms;
  }
}

static String seriesJson5m() {
  String s = "[";
  for (size_t i = 0; i < temp5m.size(); i++) {
    float v; uint32_t t;
    temp5m.get(i, v, t);
    if (i) s += ",";
    s += "{\"t\":" + String(t) + ",\"v\":" + String(v, 2) + "}";
  }
  s += "]";
  return s;
}

static String seriesJson1h() {
  String s = "[";
  for (size_t i = 0; i < temp1h.size(); i++) {
    float v; uint32_t t;
    temp1h.get(i, v, t);
    if (i) s += ",";
    s += "{\"t\":" + String(t) + ",\"v\":" + String(v, 2) + "}";
  }
  s += "]";
  return s;
}

// Core 2.x Callback Signatur
void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (!isFromA(mac)) return;
  if (len != (int)sizeof(SensorPacket)) return;

  memcpy((void*)&lastSample, data, sizeof(SensorPacket));
  gotSample = true;
}

const char INDEX_HTML[] PROGMEM =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>ESP32 Historie</title></head><body>"
"<h3>Temperatur Historie</h3>"
"<div>"
"<button onclick='load5m()'>Letzte Stunde (5min)</button>"
"<button onclick='load1h()'>Letzte Woche (1h)</button>"
"<button onclick='buzz(1)'>Buzzer an</button>"
"<button onclick='buzz(0)'>Buzzer aus</button>"
"</div>"
"<canvas id='c' width='360' height='200'></canvas>"
"<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
"<script>"
"let chart;"
"function render(points,label){"
"const xs=points.map(p=>p.t);"
"const ys=points.map(p=>p.v);"
"const ctx=document.getElementById('c').getContext('2d');"
"if(chart) chart.destroy();"
"chart=new Chart(ctx,{type:'line',data:{labels:xs,datasets:[{label:label,data:ys}]},options:{responsive:true,animation:false}});"
"}"
"async function load5m(){const r=await fetch('/api/series?res=5m');const j=await r.json();render(j,'Temp C 5m');}"
"async function load1h(){const r=await fetch('/api/series?res=1h');const j=await r.json();render(j,'Temp C 1h');}"
"async function buzz(on){await fetch(on?'/api/buzzer?on=1':'/api/buzzer?off=1');}"
"load5m();setInterval(load5m,15000);"
"</script>"
"</body></html>";

static void setupWeb() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/api/series", HTTP_GET, [](AsyncWebServerRequest *req){
    String res = "5m";
    if (req->hasParam("res")) res = req->getParam("res")->value();
    if (res == "1h") req->send(200, "application/json", seriesJson1h());
    else req->send(200, "application/json", seriesJson5m());
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
    addToAggregates(t);

    if (t > 35.0f) buzzerOn();
    else buzzerOff();

    Serial.print("Temp: ");
    Serial.print(t, 2);
    Serial.println(" C");
  }

  delay(10);
}
