#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <PubSubClient.h>
#include <ElegantOTA.h>
#include <ZMPT101B.h>
#include <math.h>
#include <string.h>

#define SDA_PIN D2
#define SCL_PIN D1

#define SCT_SAMPLE_COUNT 700
#define SCT_CALIBRATION 30.12

#define ZMPT_PIN A0
#define ZMPT_SENSITIVITY 500.0f
#define ZMPT_CALIBRATION_FACTOR 1.20f
#define ZMPT_FREQUENCY 50.0f

#define MQTT_DEFAULT_PORT 1883

#define DEFAULT_MQTT_ROOT "HaikalPower"

#define MQTT_IP_TOPIC   "IP"
#define MQTT_LWT_TOPIC  "LWT"
#define MQTT_DATA_TOPIC "Data"

ESP8266WebServer server(80);


struct StoredSettings {
  uint32_t magic;
  char wifiSSID[64];
  char wifiPassword[64];
  char mqttRoot[64];
  char mqttHost[128];
  int mqttPort;
  char mqttUser[64];
  char mqttPassword[64];
};

StoredSettings storedSettings;
const uint32_t SETTINGS_MAGIC = 0x48504F57;

class PreferencesCompat {
  String space;
public:
  void begin(const char *name, bool readOnly) {
    space = name;
    EEPROM.begin(sizeof(StoredSettings));
    EEPROM.get(0, storedSettings);
    if (storedSettings.magic != SETTINGS_MAGIC) {
      memset(&storedSettings, 0, sizeof(storedSettings));
      storedSettings.magic = SETTINGS_MAGIC;
      storedSettings.mqttPort = 1883;
      EEPROM.put(0, storedSettings);
      EEPROM.commit();
    }
  }
  String getString(const char *key, const String &fallback = "") {
    const char *v = "";
    if (space == "wifi" && String(key) == "ssid") v = storedSettings.wifiSSID;
    else if (space == "wifi" && String(key) == "pass") v = storedSettings.wifiPassword;
    else if (space == "mqtt" && String(key) == "root") v = storedSettings.mqttRoot;
    else if (space == "mqtt" && String(key) == "host") v = storedSettings.mqttHost;
    else if (space == "mqtt" && String(key) == "user") v = storedSettings.mqttUser;
    else if (space == "mqtt" && String(key) == "pass") v = storedSettings.mqttPassword;
    if (!strlen(v)) return fallback;
    return String(v);
  }
  int getInt(const char *key, int fallback = 0) {
    if (space == "mqtt" && String(key) == "port" && storedSettings.mqttPort > 0) return storedSettings.mqttPort;
    return fallback;
  }
  size_t putString(const char *key, const String &value) {
    char *dst = nullptr; size_t cap = 0;
    if (space == "wifi" && String(key) == "ssid") { dst=storedSettings.wifiSSID; cap=sizeof(storedSettings.wifiSSID); }
    else if (space == "wifi" && String(key) == "pass") { dst=storedSettings.wifiPassword; cap=sizeof(storedSettings.wifiPassword); }
    else if (space == "mqtt" && String(key) == "root") { dst=storedSettings.mqttRoot; cap=sizeof(storedSettings.mqttRoot); }
    else if (space == "mqtt" && String(key) == "host") { dst=storedSettings.mqttHost; cap=sizeof(storedSettings.mqttHost); }
    else if (space == "mqtt" && String(key) == "user") { dst=storedSettings.mqttUser; cap=sizeof(storedSettings.mqttUser); }
    else if (space == "mqtt" && String(key) == "pass") { dst=storedSettings.mqttPassword; cap=sizeof(storedSettings.mqttPassword); }
    if (!dst) return 0;
    strlcpy(dst, value.c_str(), cap);
    storedSettings.magic = SETTINGS_MAGIC;
    EEPROM.put(0, storedSettings); EEPROM.commit();
    return value.length();
  }
  size_t putInt(const char *key, int value) {
    if (space == "mqtt" && String(key) == "port") {
      storedSettings.mqttPort = value;
      storedSettings.magic = SETTINGS_MAGIC;
      EEPROM.put(0, storedSettings); EEPROM.commit();
      return sizeof(value);
    }
    return 0;
  }
  void end() { EEPROM.end(); }
};

PreferencesCompat preferences;


WiFiClient espClient;
PubSubClient mqtt(espClient);

Adafruit_ADS1115 ads;

ZMPT101B voltageSensor(
  ZMPT_PIN,
  ZMPT_FREQUENCY
);

String wifiSSID;
String wifiPassword;

String mqttHost;
String mqttUser;
String mqttPassword;

String mqttRoot = DEFAULT_MQTT_ROOT;

int mqttPort = MQTT_DEFAULT_PORT;

String deviceIP;

String mqttIPTopic;
String mqttLWTTopic;
String mqttDataTopic;

float voltageValue = 0.0;
float currentValue = 0.0;
float powerValue = 0.0;

unsigned long lastMeasurement = 0;
unsigned long lastMQTT = 0;

const unsigned long MEASUREMENT_INTERVAL = 3000;
const unsigned long MQTT_INTERVAL = 5000;


// ======================================================
// MQTT TOPIC
// ======================================================

void updateMQTTTopics() {

  mqttIPTopic =
    mqttRoot +
    "/" +
    MQTT_IP_TOPIC;

  mqttLWTTopic =
    mqttRoot +
    "/" +
    MQTT_LWT_TOPIC;

  mqttDataTopic =
    mqttRoot +
    "/" +
    MQTT_DATA_TOPIC;
}


// ======================================================
// WIFI
// ======================================================

bool connectWiFi() {

  if (wifiSSID.length() == 0) {
    return false;
  }

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    wifiSSID.c_str(),
    wifiPassword.c_str()
  );

  Serial.print("Connecting WiFi");

  unsigned long start = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < 15000
  ) {

    delay(500);

    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    deviceIP =
      WiFi.localIP().toString();

    Serial.println("WiFi connected");

    Serial.print("IP: ");
    Serial.println(deviceIP);

    updateMQTTTopics();

    return true;
  }

  Serial.println("WiFi failed");

  return false;
}


// ======================================================
// VOLTAGE - ZMPT101B
// ======================================================

void readVoltage() {

  yield();
  voltageValue =
    voltageSensor.getRmsVoltage() *
    ZMPT_CALIBRATION_FACTOR;
  yield();

  if (voltageValue < 1.0) {
    voltageValue = 0;
  }

  Serial.print("Voltage : ");
  Serial.print(voltageValue, 2);
  Serial.println(" V");
}


// ======================================================
// CURRENT - SCT013 + ADS1115
// ======================================================

// Set ke 1 untuk mencetak data mentah sensor arus ke Serial Monitor
// (membantu mencari tahu kenapa arus terbaca 0). Set ke 0 setelah selesai.
#define DEBUG_CURRENT 1

#if DEBUG_CURRENT
void debugCurrentRaw() {

  Serial.println();
  Serial.println("----- DEBUG SENSOR ARUS (ADS1115) -----");

  // 1) Tegangan tiap pin ADS1115 (single-ended), rata-rata dan naik-turunnya.
  //    Tujuan: lihat apakah ada bias ~1.65V dan apakah ada ayunan sinyal.
  for (uint8_t ch = 0; ch < 4; ch++) {

    int16_t mn = 32767, mx = -32768;
    double sum = 0;
    const int N = 120;

    for (int i = 0; i < N; i++) {
      int16_t r = ads.readADC_SingleEnded(ch);
      if (r < mn) mn = r;
      if (r > mx) mx = r;
      sum += r;
      if ((i & 0x0F) == 0) yield();
    }

    Serial.printf(
      "A%u: rata2=%.1f mV | min=%.1f mV | max=%.1f mV | p-p=%.1f mV\n",
      ch,
      ads.computeVolts((int16_t)(sum / N)) * 1000.0,
      ads.computeVolts(mn) * 1000.0,
      ads.computeVolts(mx) * 1000.0,
      ads.computeVolts((int16_t)(mx - mn)) * 1000.0
    );
  }

  // 2) Selisih A0-A1 (yang dipakai untuk menghitung arus)
  int16_t mn = 32767, mx = -32768;
  double sum = 0, sq = 0;
  const int N = 400;

  for (int i = 0; i < N; i++) {
    int16_t r = ads.readADC_Differential_0_1();
    if (r < mn) mn = r;
    if (r > mx) mx = r;
    sum += r;
    sq  += (double)r * r;
    if ((i & 0x0F) == 0) yield();
  }

  double mean   = sum / N;
  double var    = sq / N - mean * mean;
  double rmsAC  = sqrt(var > 0 ? var : 0);   // tanpa offset DC
  double vrmsmV = ads.computeVolts((int16_t)rmsAC) * 1000.0;

  Serial.printf(
    "A0-A1: raw min=%d max=%d rata2=%.1f | Vrms(AC)=%.2f mV => ~%.3f A\n",
    mn, mx, mean, vrmsmV, vrmsmV / 1000.0 * SCT_CALIBRATION
  );

  if ((mx - mn) < 10) {
    Serial.println("=> A0-A1 FLAT: tidak ada sinyal AC dari sensor.");
  } else if (mn <= -32000 || mx >= 32000) {
    Serial.println("=> A0-A1 SATURASI: input mengambang / tanpa bias / salah sambung.");
  } else {
    Serial.println("=> Ada sinyal AC di A0-A1. Cek konstanta kalibrasi.");
  }

  Serial.println("---------------------------------------");
}
#endif


void readCurrent() {

#if DEBUG_CURRENT
  debugCurrentRaw();
#endif

  double sum = 0;
  double sumSquare = 0;

  for (
    int i = 0;
    i < SCT_SAMPLE_COUNT;
    i++
  ) {

    int16_t raw =
      ads.readADC_Differential_0_1();

    float sampleVoltage =
      ads.computeVolts(raw);

    sum += sampleVoltage;
    sumSquare +=
      sampleVoltage * sampleVoltage;

    // Beri waktu untuk Wi-Fi dan watchdog ESP8266.
    if ((i & 0x0F) == 0) {
      yield();
    }
  }

  // RMS bagian AC saja: offset DC (bias) dibuang dulu.
  double mean = sum / SCT_SAMPLE_COUNT;
  double variance =
    sumSquare / SCT_SAMPLE_COUNT - mean * mean;

  if (variance < 0) {
    variance = 0;
  }

  float voltageRMS = sqrt(variance);

  currentValue =
    voltageRMS *
    SCT_CALIBRATION;

  if (currentValue < 0.02) {
    currentValue = 0;
  }

  Serial.print("Current : ");
  Serial.print(currentValue, 3);
  Serial.println(" A");
}


// ======================================================
// MEASUREMENT
// ======================================================

void readSensors() {

  yield();
  readVoltage();
  yield();
  readCurrent();
  yield();

  powerValue =
    voltageValue *
    currentValue;

  Serial.println();
  Serial.println("==============================");

  Serial.print("Voltage : ");
  Serial.print(voltageValue, 2);
  Serial.println(" V");

  Serial.print("Current : ");
  Serial.print(currentValue, 3);
  Serial.println(" A");

  Serial.print("Power   : ");
  Serial.print(powerValue, 2);
  Serial.println(" W");

  Serial.println("==============================");
}


// ======================================================
// MQTT PUBLISH
// ======================================================

void publishMQTT() {

  if (!mqtt.connected()) {
    return;
  }


  // -----------------------------
  // IP
  // -----------------------------

  mqtt.publish(
    mqttIPTopic.c_str(),
    deviceIP.c_str(),
    true
  );


  // -----------------------------
  // DATA
  // -----------------------------

  // Fixed buffer avoids repeated String allocations on the ESP8266 heap.
  char voltageText[16];
  char currentText[16];
  char powerText[16];
  char payload[96];

  dtostrf(voltageValue, 1, 2, voltageText);
  dtostrf(currentValue, 1, 3, currentText);
  dtostrf(powerValue, 1, 2, powerText);

  snprintf(
    payload,
    sizeof(payload),
    "{\"voltage\":%s,\"current\":%s,\"power\":%s}",
    voltageText,
    currentText,
    powerText
  );

  mqtt.publish(
    mqttDataTopic.c_str(),
    payload,
    true
  );


  Serial.println();
  Serial.println("===== MQTT PUBLISH =====");

  Serial.print("IP Topic   : ");
  Serial.println(mqttIPTopic);

  Serial.print("IP Payload : ");
  Serial.println(deviceIP);

  Serial.print("Data Topic : ");
  Serial.println(mqttDataTopic);

  Serial.print("Data       : ");
  Serial.println(payload);
}


// ======================================================
// MQTT CONNECT
// ======================================================

bool connectMQTT() {

  if (
    WiFi.status() != WL_CONNECTED
  ) {
    return false;
  }

  if (mqtt.connected()) {
    return true;
  }

  mqtt.setServer(
    mqttHost.c_str(),
    mqttPort
  );

  String clientID =
    "HaikalPower-" +
    deviceIP;

  Serial.println();
  Serial.println("Connecting MQTT...");

  bool connected =
    mqtt.connect(
      clientID.c_str(),

      mqttUser.length() > 0
        ? mqttUser.c_str()
        : nullptr,

      mqttPassword.length() > 0
        ? mqttPassword.c_str()
        : nullptr,

      mqttLWTTopic.c_str(),
      0,
      true,
      "Offline"
    );

  if (connected) {

    Serial.println("MQTT connected");

    // LWT Online
    mqtt.publish(
      mqttLWTTopic.c_str(),
      "Online",
      true
    );

    // IP
    mqtt.publish(
      mqttIPTopic.c_str(),
      deviceIP.c_str(),
      true
    );

    // Data
    publishMQTT();

    return true;
  }

  Serial.print(
    "MQTT failed, state = "
  );

  Serial.println(
    mqtt.state()
  );

  return false;
}


// ======================================================
// WEB UI - STYLE, ICONS & HELPERS
// Theme: blue + white, red for warnings and errors.
// UI only: no CDN or external fonts, works offline (AP mode).
// To re-colour the whole UI, edit the variables in :root.
// ======================================================

static const char UI_CSS[] PROGMEM = R"rawliteral(
:root{
--blue:#1557e6;--blue-d:#0c3fb3;--tint:#eef3fd;--field:#f7faff;--line:#d9e4fa;
--ink:#0d1b3d;--muted:#5a6a8a;
--red:#e03247;--red-tint:#fdecee;--red-ink:#8f1d2c;--ok:#35d49a}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--tint);color:var(--ink);padding-bottom:92px;
font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif}
h1{margin:0;font-size:1.55rem;line-height:1.2}
h2{margin:0 0 12px;font-size:1.02rem;line-height:1.3}
p{margin:0 0 10px}
.in{max-width:540px;margin:0 auto;padding:0 16px}
.wrap{max-width:540px;margin:0 auto;padding:0 16px}
.pull{margin-top:-52px}
.ico{width:22px;height:22px;fill:none;stroke:currentColor;stroke-width:2;
stroke-linecap:round;stroke-linejoin:round;flex:none}

/* blue header (dashboard + sub pages) */
.hero,.bar{background:var(--blue);color:#fff;border-radius:0 0 28px 28px}
.hero{padding:18px 0 82px}
.bar{padding:22px 0 70px}
.bar p{margin:4px 0 0;opacity:.85}
.brand{display:flex;align-items:center;justify-content:space-between;gap:12px}
.logo{display:flex;align-items:center;gap:10px}
.logo-mark{display:flex;align-items:center;justify-content:center;width:40px;height:40px;
border-radius:12px;background:#fff;color:var(--blue)}
.logo b{display:block;font-size:1.1rem;line-height:1.2}
.logo span.s{display:block;font-size:.8rem;opacity:.8}
.pill{display:inline-flex;align-items:center;gap:8px;padding:6px 12px;border-radius:999px;
background:rgba(255,255,255,.18);font-size:.82rem;font-weight:600;white-space:nowrap}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:#ff7a87}
.pill.on .dot{background:var(--ok)}
.banner{margin:14px 0 0;padding:10px 14px;border-radius:12px;background:#fff;color:var(--red-ink);
border-left:5px solid var(--red);font-size:.9rem;font-weight:600}
.banner[hidden]{display:none}

/* live power */
.power{margin-top:22px}
.power .label{opacity:.85;font-size:.95rem}
.power .num{font-size:3.8rem;line-height:1.05;font-weight:800;letter-spacing:-.03em;
font-variant-numeric:tabular-nums}
.power .num small{margin-left:6px;font-size:1.5rem;font-weight:600;opacity:.85;letter-spacing:0}
.spark{display:block;width:100%;height:64px;margin-top:10px}
.spark-cap{margin:4px 0 0;font-size:.78rem;opacity:.7}
.stale .num,.stale .num2,.stale .spark{opacity:.4}

/* tiles + cards */
.pair{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:12px}
.tile{background:#fff;border-radius:18px;padding:16px;box-shadow:0 8px 20px rgba(21,87,230,.16)}
.tile .ic{display:flex;align-items:center;justify-content:center;width:36px;height:36px;
border-radius:11px;background:var(--tint);color:var(--blue);margin-bottom:10px}
.label{color:var(--muted);font-size:.92rem}
.num2{font-size:1.9rem;line-height:1.2;font-weight:800;color:var(--blue-d);
font-variant-numeric:tabular-nums}
.num2 small{margin-left:4px;font-size:1rem;font-weight:600;color:var(--muted)}
.card{background:#fff;border:1px solid var(--line);border-radius:16px;padding:16px 18px;
margin-bottom:12px;box-shadow:0 2px 10px rgba(21,87,230,.06)}
.rows{margin:0}
.rows div{display:flex;justify-content:space-between;gap:16px;padding:10px 0;border-top:1px solid var(--line)}
.rows div:first-child{border-top:0;padding-top:0}
.rows div:last-child{padding-bottom:0}
.rows dt{flex:none;color:var(--muted)}
.rows dd{margin:0;text-align:right;font-weight:600;word-break:break-all;
-webkit-user-select:all;user-select:all}
.hint{margin:6px 0 0;color:var(--muted);font-size:.85rem}

/* forms + buttons */
.field{margin-bottom:16px}
.field:last-child{margin-bottom:0}
label{display:block;margin:0 0 6px;font-weight:600}
input{width:100%;min-height:48px;padding:10px 14px;border:1.5px solid var(--line);border-radius:12px;
background:var(--field);color:var(--ink);font:inherit}
input:focus{outline:none;border-color:var(--blue);background:#fff;box-shadow:0 0 0 4px rgba(21,87,230,.15)}
.pw{display:flex;gap:8px}
.pw input{flex:1;min-width:0}
.pw .btn{min-width:72px;padding:0 14px}
.btn{display:inline-flex;align-items:center;justify-content:center;min-height:48px;padding:10px 20px;
border-radius:12px;border:1.5px solid var(--blue);background:#fff;color:var(--blue);
font:inherit;font-weight:700;text-decoration:none;cursor:pointer}
.btn.primary{background:var(--blue);color:#fff}
.btn.primary:hover{background:var(--blue-d)}
.btn.block{width:100%}
.btn:disabled{opacity:.6;cursor:wait}
a:focus-visible,button:focus-visible{outline:3px solid var(--blue);outline-offset:2px}
.note{margin:0 0 14px;padding:12px 14px;border-radius:12px;background:var(--red-tint);
color:var(--red-ink);border-left:5px solid var(--red);font-size:.9rem}

/* result pages */
.done{text-align:center;padding:28px 20px}
.tick{display:inline-flex;align-items:center;justify-content:center;width:52px;height:52px;
border-radius:50%;background:var(--blue);color:#fff;font-size:1.6rem;margin-bottom:12px}
.done .btn{margin-top:8px}

/* bottom tab bar */
.tabs{position:fixed;left:0;right:0;bottom:0;z-index:5;background:#fff;border-top:1px solid var(--line);
padding-bottom:env(safe-area-inset-bottom,0px)}
.tabs .in{display:flex;padding:0 8px}
.tabs a{flex:1;display:flex;flex-direction:column;align-items:center;gap:2px;padding:9px 4px 10px;
color:var(--muted);text-decoration:none;font-size:.74rem;font-weight:600;
border-top:3px solid transparent;margin-top:-1px}
.tabs a.on{color:var(--blue);border-top-color:var(--blue)}

@media (max-width:380px){.power .num{font-size:3rem}.num2{font-size:1.6rem}}
)rawliteral";

static const char UI_JS[] PROGMEM = R"rawliteral(
function tp(b){var i=b.previousElementSibling;var s=i.type==='password';
i.type=s?'text':'password';b.textContent=s?'Hide':'Show';}
function busy(f){var b=f.querySelector('button[type=submit]');
if(b){b.disabled=true;b.textContent='Saving...';}return true;}
)rawliteral";

// Icon paths (24x24, stroke style)
static const char ICON_BOLT[]  PROGMEM = "<path d='M13 2L4 14h7l-1 8 9-12h-7z'/>";
static const char ICON_WAVE[]  PROGMEM = "<path d='M2 12c2-8 4-8 6 0s4 8 6 0 4-8 6 0'/>";
static const char ICON_HOME[]  PROGMEM = "<path d='M3 11l9-8 9 8v9a1 1 0 0 1-1 1h-5v-6H9v6H4a1 1 0 0 1-1-1z'/>";
static const char ICON_WIFI[]  PROGMEM = "<path d='M2 9a15 15 0 0 1 20 0M5 13a10 10 0 0 1 14 0M8.5 16.5a5 5 0 0 1 7 0'/><circle cx='12' cy='20' r='1'/>";
static const char ICON_MQTT[]  PROGMEM = "<circle cx='12' cy='12' r='2'/><path d='M16.2 7.8a6 6 0 0 1 0 8.4M7.8 16.2a6 6 0 0 1 0-8.4M19 5a10 10 0 0 1 0 14M5 19A10 10 0 0 1 5 5'/>";
static const char ICON_OTA[]   PROGMEM = "<path d='M12 3v12m0 0l-4-4m4 4l4-4M4 17v3h16v-3'/>";


String htmlEscape(const String &text) {

  String out;
  out.reserve(text.length() + 8);

  for (size_t i = 0; i < text.length(); i++) {

    char c = text[i];

    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;
    }
  }

  return out;
}


String icon(const char *path) {

  String html;

  html += "<svg class='ico' viewBox='0 0 24 24' aria-hidden='true'>";
  html += FPSTR(path);   // path ada di PROGMEM
  html += "</svg>";

  return html;
}


// ======================================================
// HTML HEADER / FOOTER
// ======================================================

String htmlHeader(String title) {

  String html;

  html += "<!DOCTYPE html>";
  html += "<html lang='en'>";
  html += "<head>";

  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta name='theme-color' content='#1557e6'>";

  html += "<title>";
  html += title;
  html += "</title>";

  html += "<style>";
  html += FPSTR(UI_CSS);   // PROGMEM: wajib FPSTR()
  html += "</style>";

  html += "</head>";
  html += "<body>";

  return html;
}


// Blue title bar used by the settings and result pages.
// Opens <main>, which htmlFooter() closes.
String pageBar(const char *title, const char *subtitle) {

  String html;

  html += "<header class='bar'><div class='in'>";
  html += "<h1>";
  html += title;
  html += "</h1>";
  html += "<p>";
  html += subtitle;
  html += "</p>";
  html += "</div></header>";
  html += "<main class='wrap pull'>";

  return html;
}


String tabLink(
  const char *key,
  const char *href,
  const char *label,
  const char *iconPath,
  const char *active
) {

  String html;

  html += "<a href='";
  html += href;
  html += "'";

  if (strcmp(key, active) == 0) {
    html += " class='on' aria-current='page'";
  }

  html += ">";
  html += icon(iconPath);
  html += label;
  html += "</a>";

  return html;
}


// Closes <main>, adds the bottom tab bar and shared script.
// active: "home", "wifi" or "mqtt"
String htmlFooter(const char *active) {

  String html;

  html += "</main>";

  html += "<nav class='tabs'><div class='in'>";
  html += tabLink("home", "/",       "Home",   ICON_HOME, active);
  html += tabLink("wifi", "/wifi",   "WiFi",   ICON_WIFI, active);
  html += tabLink("mqtt", "/mqtt",   "MQTT",   ICON_MQTT, active);
  html += tabLink("ota",  "/update", "Update", ICON_OTA,  active);
  html += "</div></nav>";

  html += "<script>";
  html += FPSTR(UI_JS);    // PROGMEM: wajib FPSTR()
  html += "</script>";

  html += "</body></html>";

  return html;
}


// Label + input (+ show/hide button for password) + optional hint.
// The input "name" equals its id, so form field names are unchanged.
String formField(
  const char *id,
  const char *label,
  const String &value,
  const char *type = "text",
  const char *extra = "",
  const char *hint = ""
) {

  bool isPassword = strcmp(type, "password") == 0;

  String html;

  html += "<div class='field'>";

  html += "<label for='";
  html += id;
  html += "'>";
  html += label;
  html += "</label>";

  if (isPassword) {
    html += "<div class='pw'>";
  }

  html += "<input id='";
  html += id;
  html += "' name='";
  html += id;
  html += "' type='";
  html += type;
  html += "' value='";
  html += htmlEscape(value);
  html += "' ";
  html += extra;
  html += ">";

  if (isPassword) {
    html += "<button class='btn' type='button' onclick='tp(this)'>Show</button>";
    html += "</div>";
  }

  if (strlen(hint) > 0) {
    html += "<p class='hint'>";
    html += hint;
    html += "</p>";
  }

  html += "</div>";

  return html;
}


// ======================================================
// DASHBOARD
// ======================================================

void handleRoot() {

  String html =
    htmlHeader(
      "HaikalPower"
    );

  bool mqttOn = mqtt.connected();


  // -----------------------------
  // BLUE HEADER + LIVE POWER
  // -----------------------------

  html += "<header class='hero'><div class='in'>";

  html += "<div class='brand'>";

  html += "<div class='logo'>";
  html += "<span class='logo-mark'>";
  html += icon(ICON_BOLT);
  html += "</span>";
  html += "<div><b>HaikalPower</b><span class='s'>Power monitor</span></div>";
  html += "</div>";

  html += "<span class='pill";
  html += mqttOn ? " on" : "";
  html += "' id='mqttPill'>";
  html += "<i class='dot'></i>";
  html += "<span>MQTT <span id='mqtt'>";
  html += mqttOn ? "Connected" : "Disconnected";
  html += "</span></span>";
  html += "</span>";

  html += "</div>";

  html += "<div class='banner' id='offline' hidden>";
  html += "Can't reach the device. Retrying...";
  html += "</div>";

  html += "<div class='power'>";
  html += "<div class='label'>Power</div>";
  html += "<div class='num'><span id='power'>--</span><small>W</small></div>";

  html += "<svg class='spark' viewBox='0 0 300 64' preserveAspectRatio='none' aria-hidden='true'>";
  html += "<path id='area' fill='rgba(255,255,255,.18)'/>";
  html += "<path id='line' fill='none' stroke='#fff' stroke-width='2' ";
  html += "stroke-linejoin='round' vector-effect='non-scaling-stroke'/>";
  html += "</svg>";
  html += "<p class='spark-cap'>Last minute</p>";
  html += "</div>";

  html += "</div></header>";


  html += "<main class='wrap pull'>";


  // -----------------------------
  // VOLTAGE + CURRENT
  // -----------------------------

  html += "<div class='pair'>";

  html += "<section class='tile'>";
  html += "<div class='ic'>";
  html += icon(ICON_WAVE);
  html += "</div>";
  html += "<div class='label'>Voltage</div>";
  html += "<div class='num2'><span id='voltage'>--</span><small>V</small></div>";
  html += "</section>";

  html += "<section class='tile'>";
  html += "<div class='ic'>";
  html += icon(ICON_BOLT);
  html += "</div>";
  html += "<div class='label'>Current</div>";
  html += "<div class='num2'><span id='current'>--</span><small>A</small></div>";
  html += "</section>";

  html += "</div>";


  // -----------------------------
  // WIFI
  // -----------------------------

  html += "<section class='card'>";
  html += "<h2>Network</h2>";
  html += "<dl class='rows'>";

  html += "<div><dt>WiFi</dt><dd>";
  html += wifiSSID.length() > 0 ? htmlEscape(wifiSSID) : String("Not set");
  html += "</dd></div>";

  html += "<div><dt>IP address</dt><dd>";
  html += deviceIP.length() > 0 ? deviceIP : String("Not connected");
  html += "</dd></div>";

  html += "</dl>";
  html += "</section>";


  // -----------------------------
  // MQTT
  // -----------------------------

  html += "<section class='card'>";
  html += "<h2>MQTT topics</h2>";
  html += "<dl class='rows'>";

  html += "<div><dt>Main</dt><dd>";
  html += htmlEscape(mqttRoot);
  html += "</dd></div>";

  html += "<div><dt>IP</dt><dd>";
  html += htmlEscape(mqttIPTopic);
  html += "</dd></div>";

  html += "<div><dt>LWT</dt><dd>";
  html += htmlEscape(mqttLWTTopic);
  html += "</dd></div>";

  html += "<div><dt>Data</dt><dd>";
  html += htmlEscape(mqttDataTopic);
  html += "</dd></div>";

  html += "</dl>";
  html += "<p class='hint' style='margin-top:12px'>Tap a topic to select it for copying.</p>";
  html += "</section>";


  // -----------------------------
  // JAVASCRIPT
  // -----------------------------

  html += "<script>";

  html += R"rawliteral(
var offline=document.getElementById('offline');
var pill=document.getElementById('mqttPill');
var line=document.getElementById('line');
var area=document.getElementById('area');
var hist=[];
function draw(){
var n=hist.length;
if(n<2){return;}
var mn=Math.min.apply(null,hist),mx=Math.max.apply(null,hist),r=mx-mn;
var step=300/59,pts=[],x,y,i;
for(i=0;i<n;i++){
x=300-(n-1-i)*step;
y=r===0?32:58-((hist[i]-mn)/r)*50;
pts.push(x.toFixed(1)+' '+y.toFixed(1));
}
var d='M'+pts.join(' L');
line.setAttribute('d',d);
area.setAttribute('d',d+' L300 64 L'+pts[0].split(' ')[0]+' 64 Z');
}
function updateData(){
fetch('/data',{cache:'no-store'})
.then(function(r){if(!r.ok){throw 0;}return r.json();})
.then(function(d){
document.getElementById('voltage').innerText=d.voltage.toFixed(2);
document.getElementById('current').innerText=d.current.toFixed(3);
document.getElementById('power').innerText=d.power.toFixed(2);
document.getElementById('mqtt').innerText=d.mqtt?'Connected':'Disconnected';
pill.className='pill'+(d.mqtt?' on':'');
hist.push(d.power);
if(hist.length>60){hist.shift();}
draw();
offline.hidden=true;
document.body.classList.remove('stale');
})
.catch(function(){
offline.hidden=false;
document.body.classList.add('stale');
});
}
setInterval(updateData,1000);
updateData();
)rawliteral";

  html += "</script>";

  html += htmlFooter("home");

  server.send(
    200,
    "text/html",
    html
  );
}


// ======================================================
// JSON DATA
// ======================================================

void handleData() {
  // /data is requested every second by the dashboard. Use a fixed buffer
  // instead of repeated String concatenation to reduce heap fragmentation.
  char voltageText[16];
  char currentText[16];
  char powerText[16];
  char json[112];

  dtostrf(voltageValue, 1, 2, voltageText);
  dtostrf(currentValue, 1, 3, currentText);
  dtostrf(powerValue, 1, 2, powerText);

  snprintf(
    json,
    sizeof(json),
    "{\"voltage\":%s,\"current\":%s,\"power\":%s,\"mqtt\":%s}",
    voltageText,
    currentText,
    powerText,
    mqtt.connected() ? "true" : "false"
  );

  server.send(200, "application/json", json);
}


// ======================================================
// WIFI SETTINGS
// ======================================================

void handleWiFi() {

  String html =
    htmlHeader(
      "WiFi Settings"
    );

  html += pageBar(
    "WiFi settings",
    "Choose the network this device connects to."
  );

  html += "<form action='/savewifi' method='POST' onsubmit='return busy(this)'>";

  html += "<section class='card'>";

  html += formField(
    "ssid",
    "Network name (SSID)",
    wifiSSID,
    "text",
    "autocomplete='off' autocapitalize='none' spellcheck='false'"
  );

  html += formField(
    "pass",
    "Password",
    wifiPassword,
    "password",
    "autocomplete='off'"
  );

  html += "</section>";

  html += "<div class='note'>";
  html += "<b>The device restarts after saving.</b> ";
  html += "If the new network gives it a different IP address, ";
  html += "open the dashboard again using that address.";
  html += "</div>";

  html += "<button class='btn primary block' type='submit'>Save and restart</button>";

  html += "</form>";

  html += htmlFooter("wifi");

  server.send(
    200,
    "text/html",
    html
  );
}


// ======================================================
// SAVE WIFI
// ======================================================

void handleSaveWiFi() {

  wifiSSID =
    server.arg("ssid");

  wifiPassword =
    server.arg("pass");

  preferences.begin(
    "wifi",
    false
  );

  preferences.putString(
    "ssid",
    wifiSSID
  );

  preferences.putString(
    "pass",
    wifiPassword
  );

  preferences.end();

  String html =
    htmlHeader(
      "WiFi Saved"
    );

  html += pageBar(
    "WiFi settings",
    "Saved"
  );

  html += "<section class='card done'>";
  html += "<span class='tick'>&#10003;</span>";
  html += "<h1>WiFi saved</h1>";
  html += "<p style='margin-top:8px'>The device is restarting and will connect to <b>";
  html += htmlEscape(wifiSSID);
  html += "</b>.</p>";
  html += "<p class='hint'>This page reloads in <span id='t'>15</span> seconds. ";
  html += "If it doesn't load, the device may have a new IP address. ";
  html += "Check your router, or read the IP topic on your MQTT broker.</p>";
  html += "<a class='btn' href='/'>Open dashboard</a>";
  html += "</section>";

  html += "<script>";
  html += "var s=15,t=document.getElementById('t');";
  html += "setInterval(function(){s--;if(s<=0){location.href='/';}else{t.innerText=s;}},1000);";
  html += "</script>";

  html += htmlFooter("wifi");

  server.send(
    200,
    "text/html",
    html
  );

  delay(1500);

  ESP.restart();
}


// ======================================================
// MQTT SETTINGS
// ======================================================

void handleMQTT() {

  String html =
    htmlHeader(
      "MQTT Settings"
    );

  html += pageBar(
    "MQTT settings",
    "Where the device sends its readings."
  );

  html += "<form action='/savemqtt' method='POST' onsubmit='return busy(this)'>";


  // -----------------------------
  // TOPIC
  // -----------------------------

  html += "<section class='card'>";
  html += "<h2>Topic</h2>";

  html += formField(
    "root",
    "Main topic",
    mqttRoot,
    "text",
    "autocomplete='off' autocapitalize='none' spellcheck='false'",
    "Readings are published under this topic as /IP, /LWT and /Data."
  );

  html += "</section>";


  // -----------------------------
  // BROKER
  // -----------------------------

  html += "<section class='card'>";
  html += "<h2>Broker</h2>";

  html += formField(
    "host",
    "Host or IP address",
    mqttHost,
    "text",
    "placeholder='e.g. 192.168.1.10' autocomplete='off' autocapitalize='none' spellcheck='false'"
  );

  html += formField(
    "port",
    "Port",
    String(mqttPort),
    "number",
    "inputmode='numeric' min='1' max='65535'"
  );

  html += "</section>";


  // -----------------------------
  // LOGIN
  // -----------------------------

  html += "<section class='card'>";
  html += "<h2>Login</h2>";

  html += formField(
    "user",
    "Username",
    mqttUser,
    "text",
    "autocomplete='off' autocapitalize='none' spellcheck='false'",
    "Leave empty if the broker doesn't need a login."
  );

  html += formField(
    "pass",
    "Password",
    mqttPassword,
    "password",
    "autocomplete='off'"
  );

  html += "</section>";


  html += "<button class='btn primary block' type='submit'>Save settings</button>";

  html += "</form>";

  html += htmlFooter("mqtt");

  server.send(
    200,
    "text/html",
    html
  );
}


// ======================================================
// SAVE MQTT
// ======================================================

void handleSaveMQTT() {

  mqttRoot =
    server.arg("root");

  mqttHost =
    server.arg("host");

  mqttPort =
    server.arg("port").toInt();

  mqttUser =
    server.arg("user");

  mqttPassword =
    server.arg("pass");


  if (mqttRoot.length() == 0) {

    mqttRoot =
      DEFAULT_MQTT_ROOT;
  }

  if (mqttPort <= 0) {

    mqttPort =
      MQTT_DEFAULT_PORT;
  }


  preferences.begin(
    "mqtt",
    false
  );

  preferences.putString(
    "root",
    mqttRoot
  );

  preferences.putString(
    "host",
    mqttHost
  );

  preferences.putInt(
    "port",
    mqttPort
  );

  preferences.putString(
    "user",
    mqttUser
  );

  preferences.putString(
    "pass",
    mqttPassword
  );

  preferences.end();


  updateMQTTTopics();

  mqtt.disconnect();


  String html =
    htmlHeader(
      "MQTT Saved"
    );

  html += pageBar(
    "MQTT settings",
    "Saved"
  );

  html += "<section class='card done'>";
  html += "<span class='tick'>&#10003;</span>";
  html += "<h1>MQTT settings saved</h1>";
  html += "<p style='margin-top:8px'>The new settings are applied. ";
  html += "The device reconnects to the broker automatically.</p>";
  html += "<a class='btn' href='/'>Back to dashboard</a>";
  html += "</section>";

  html += "<script>setTimeout(function(){location.href='/';},3000);</script>";

  html += htmlFooter("mqtt");

  server.send(
    200,
    "text/html",
    html
  );
}


// ======================================================
// LOAD SETTINGS
// ======================================================

void loadSettings() {

  // -----------------------------
  // WiFi
  // -----------------------------

  preferences.begin(
    "wifi",
    true
  );

  wifiSSID =
    preferences.getString(
      "ssid",
      ""
    );

  wifiPassword =
    preferences.getString(
      "pass",
      ""
    );

  preferences.end();


  // -----------------------------
  // MQTT
  // -----------------------------

  preferences.begin(
    "mqtt",
    true
  );

  mqttRoot =
    preferences.getString(
      "root",
      DEFAULT_MQTT_ROOT
    );

  mqttHost =
    preferences.getString(
      "host",
      ""
    );

  mqttPort =
    preferences.getInt(
      "port",
      MQTT_DEFAULT_PORT
    );

  mqttUser =
    preferences.getString(
      "user",
      ""
    );

  mqttPassword =
    preferences.getString(
      "pass",
      ""
    );

  preferences.end();
}


// ======================================================
// WEB SERVER
// ======================================================

void setupWebServer() {

  server.on(
    "/",
    HTTP_GET,
    handleRoot
  );

  server.on(
    "/data",
    HTTP_GET,
    handleData
  );

  server.on(
    "/wifi",
    HTTP_GET,
    handleWiFi
  );

  server.on(
    "/savewifi",
    HTTP_POST,
    handleSaveWiFi
  );

  server.on(
    "/mqtt",
    HTTP_GET,
    handleMQTT
  );

  server.on(
    "/savemqtt",
    HTTP_POST,
    handleSaveMQTT
  );

  ElegantOTA.begin(
    &server
  );

  server.begin();

  Serial.println(
    "Web server started"
  );
}


// ======================================================
// SETUP
// ======================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println(
    "=============================="
  );

  Serial.println(
    "HaikalPower Wemos D1 mini ESP8266"
  );

  Serial.println(
    "SCT013 + ADS1115 + ZMPT101B (A0)"
  );

  Serial.println(
    "=============================="
  );


  // -----------------------------
  // I2C
  // -----------------------------

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );


  // -----------------------------
  // ADS1115
  // -----------------------------

  if (!ads.begin(0x48)) {

    Serial.println(
      "ADS1115 not found!"
    );

    while (true) {
      delay(1000);
    }
  }

  ads.setGain(
    GAIN_TWO
  );

  ads.setDataRate(
    RATE_ADS1115_860SPS
  );

  Serial.println(
    "ADS1115 OK"
  );


  // -----------------------------
  // ZMPT101B
  // -----------------------------

  voltageSensor.setSensitivity(
    ZMPT_SENSITIVITY
  );

  Serial.println(
    "ZMPT101B OK"
  );


  // -----------------------------
  // Settings
  // -----------------------------

  loadSettings();


  // -----------------------------
  // WiFi
  // -----------------------------

  if (!connectWiFi()) {

    WiFi.mode(
      WIFI_AP
    );

    WiFi.softAP(
      "HaikalPower-Setup",
      ""
    );

    Serial.println();
    Serial.println(
      "Setup AP started"
    );

    Serial.print(
      "AP IP: "
    );

    Serial.println(
      WiFi.softAPIP()
    );

  } else {

    Serial.print(
      "Device IP: "
    );

    Serial.println(
      deviceIP
    );
  }


  // -----------------------------
  // MQTT topics
  // -----------------------------

  updateMQTTTopics();


  // -----------------------------
  // MQTT
  // -----------------------------

  mqtt.setBufferSize(
    512
  );

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    connectMQTT();
  }


  // -----------------------------
  // Web
  // -----------------------------

  setupWebServer();
}


// ======================================================
// LOOP
// ======================================================

void loop() {

  static unsigned long lastHeapLog = 0;
  if (millis() - lastHeapLog >= 10000) {
    lastHeapLog = millis();
    Serial.print(F("Free heap: "));
    Serial.print(ESP.getFreeHeap());
    Serial.print(F(" bytes | max free block: "));
    Serial.println(ESP.getMaxFreeBlockSize());
  }

  yield();
  server.handleClient();
  ElegantOTA.loop();
  yield();


  // -----------------------------
  // WiFi reconnect
  // -----------------------------

  if (
    WiFi.status() != WL_CONNECTED
  ) {

    static unsigned long
      lastWiFiReconnect = 0;

    if (
      millis() -
      lastWiFiReconnect >
      10000
    ) {

      lastWiFiReconnect =
        millis();

      connectWiFi();
      yield();
    }
  }


  // -----------------------------
  // Sensor measurement
  // -----------------------------

  if (
    millis() -
    lastMeasurement >=
    MEASUREMENT_INTERVAL
  ) {

    lastMeasurement =
      millis();

    readSensors();
  }


  // -----------------------------
  // MQTT
  // -----------------------------

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    if (!mqtt.connected()) {

      if (
        millis() -
        lastMQTT >=
        MQTT_INTERVAL
      ) {

        lastMQTT =
          millis();

        connectMQTT();
        yield();
      }

    } else {

      mqtt.loop();

      if (
        millis() -
        lastMQTT >=
        MQTT_INTERVAL
      ) {

        lastMQTT =
          millis();

        publishMQTT();
      }
    }
  }
}