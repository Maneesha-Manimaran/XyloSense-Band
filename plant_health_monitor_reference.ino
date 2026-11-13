// Plant Health Monitor - ESP32-C3
// Mobile-HOTSPOT-ONLY version: Local Web Server dashboard + OLED + 10-day resistance monitoring.
// NO RainMaker / cloud. NO Wi-Fi setup portal. Works with the DEFAULT partition scheme.
//
// OLED FIX: display now uses HARDWARE I2C (Wire) instead of software I2C.
//
// Libraries: "U8g2", "DHT sensor library" (Adafruit), "Adafruit Unified Sensor".
// WebServer, ESPmDNS, Preferences, Wire come with the ESP32 core.
//
// Arduino IDE: Board = ESP32C3 Dev Module, "USB CDC On Boot" = Enabled
//
// ARCHITECTURE: ESP32 -> Mobile Hotspot (2.4 GHz, WPA2) -> Browser -> ESP32 Web Server
// The ESP32 connects ONLY to the hotspot written in WIFI_SSID / WIFI_PASSWORD below.
//
// ELECTRODE WIRING (for ~142 kOhm plant, RFIXED = 10 kOhm):
//   3.3V -> ELECTRODE -> GPIO0 (ADC) -> RFIXED (10k) -> GND

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// =====================================================================
// ===================== USER CONFIGURATION ============================
// =====================================================================

// ---- Mobile hotspot ----  >>> CHANGE THESE TWO LINES <<<
#define WIFI_SSID      "YOUR_2_4_GHZ_HOTSPOT_NAME"   // exact hotspot name, upper/lower case matters
#define WIFI_PASSWORD  "YOUR_HOTSPOT_PASSWORD"       // hotspot password (at least 8 characters)
#define WIFI_CONNECT_TIMEOUT_MS  15000UL     // wait per connection attempt
#define WIFI_MAX_ATTEMPTS        3           // attempts at boot
#define WIFI_RETRY_INTERVAL_MS   10000UL     // background retry interval
#define WIFI_LOW_TX_POWER        1           // 1 = helps many ESP32-C3 boards connect (0 = off)

// ---- Firebase Cloud Firestore ----
// Enable Authentication > Sign-in method > Anonymous in Firebase Console.
#define FIREBASE_API_KEY       "AIzaSyCvzVC2juhHdsLnz0l2Wm8517xyWKk0BPg"
#define FIREBASE_PROJECT_ID    "xylosenseband"
#define FIREBASE_DATABASE_ID   "(default)"
#define FIRESTORE_TREE_ID      "TREE-01"
#define XYLOSENSE_BACKEND_IP   "10.59.179.201"
#define XYLOSENSE_BACKEND_PORT 8081


// ---- Resistance condition (EXISTING project logic: health index) ----
// health index >= NORMAL_LIMIT -> NORMAL ; >= MEDIUM_LIMIT -> MEDIUM ; below -> CRITICAL
#define RESISTANCE_NORMAL_LIMIT  75
#define RESISTANCE_MEDIUM_LIMIT  40

// ---- 10-day trend ----
#define TREND_STABLE_PCT   5.0f     // change below +/- this % between first and last 5 days = STABLE
#define DAY_LENGTH_MS      86400000UL   // 24 h of running time = 1 day (use 60000UL to test quickly)
#define HISTORY_SAVE_MS    3600000UL    // save history to flash every hour
#define DEMO_FALLBACK_BASE_KOHM 5.0f    // demo base if no baseline exists yet (inside the GOOD range)

// ---- Plant condition by ABSOLUTE resistance (kOhm) ----
// below 10 kOhm      -> GOOD (NORMAL)
// 10 to 20 kOhm      -> MEDIUM
// above 20 kOhm      -> ABNORMAL (CRITICAL)
#define RES_GOOD_MAX_KOHM    10.0f
#define RES_MEDIUM_MAX_KOHM  20.0f

// =====================================================================

// ================= PIN CONFIG (ESP32-C3) - UNCHANGED =================
#define ELECTRODE_PIN 0   // ADC1_CH0
#define SOIL_PIN      1   // ADC1_CH1
#define DHT_PIN       10
#define RED_LED       3
#define YELLOW_LED    4
#define BLUE_LED      5
#define BUZZER_PIN    6
#define EXCITE_PIN    7   // only used if USE_EXCITATION = 1
#define OLED_SDA      8   // ESP32-C3 default I2C SDA
#define OLED_SCL      9   // ESP32-C3 default I2C SCL
// NOTE: GPIO 8 and 9 are strapping pins. If the board does not boot with the OLED
// connected, move the OLED to other pins (e.g. SDA=20, SCL=21).

// 0 = divider top connected to 3.3V (no rewiring)
// 1 = divider top connected to EXCITE_PIN (pulsed, protects the plant)
#define USE_EXCITATION 0

// OLED type: 0 = SSD1306 (0.96"), 1 = SH1106 (most 1.3")
#define OLED_SH1106 0

// ================= SETTINGS =================
#define HOSTNAME             "plant"
#define SENSOR_INTERVAL_MS   5000UL
#define BEEP_REPEAT_MS       30000UL

// ================= DHT =================
// 0 = DHT11 (blue body), 1 = DHT22 / AM2302 (white body)
#define DHT_IS_DHT22 0
#if DHT_IS_DHT22
#define DHTTYPE DHT22
#else
#define DHTTYPE DHT11
#endif
DHT dht(DHT_PIN, DHTTYPE);

// ================= ELECTRODE =================
#define VCC          3.3f
#define RFIXED       10000.0f    // fixed resistor to GND (10 kOhm)
#define ADC_MAX_V    3.05f       // ESP32-C3 ADC saturates near this at 11 dB
#define R_OPEN       999999.0f

// Wiring selector - MUST match your hardware:
// 1 = 3.3V -> ELECTRODE -> GPIO0 -> RFIXED -> GND   (electrode on TOP)
// 0 = 3.3V -> RFIXED -> GPIO0 -> ELECTRODE -> GND   (electrode on BOTTOM)
#define ELECTRODE_ON_TOP 1
float g_adcVolt = 0;

// ================= SOIL CALIBRATION =================
// Measure raw value in dry air and in water, then put your values here
#define SOIL_DRY_RAW 3200   // default only - calibrate with the dashboard buttons
#define SOIL_WET_RAW 1400
int soilDry = SOIL_DRY_RAW;
int soilWet = SOIL_WET_RAW;
int g_soilRaw = 0;

// ================= OLED (HARDWARE I2C) =================
#if OLED_SH1106
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#endif

bool oledOk = false;
uint8_t oledAddr = 0;

// ================= GLOBALS =================
WebServer server(80);
Preferences prefs;

bool serverStarted = false;
volatile bool recalRequested = false;

float baselineResistance = 0;
bool  baselineReady = false;

// Wi-Fi state
String cfgSsid, cfgPass;
uint32_t wifiAttempts = 0;
volatile uint8_t lastDisconnectReason = 0;

// Latest values (shared with web page + OLED)
float g_resistance = 0, g_deviation = 0, g_temp = 0, g_hum = 0;
int   g_soil = 0, g_health = 100;
String g_status = "STARTING";
bool  g_resValid = false;
bool  g_dhtOk = true;

bool buzzerOn = false;
uint32_t buzzerOffAt = 0;

// ================= 10-DAY RESISTANCE HISTORY =================
// Index 0 = Day 1 ... index 9 = Day 10 (TODAY, live average). Unit: kOhm.
// Days flagged historyDemo[] = true are DEMO / INITIAL data (generated around your baseline).
// They are replaced by REAL daily averages as days pass (see checkDayRollover()).
#define HIST_DAYS 10
float   resistanceHistory[HIST_DAYS];
uint8_t historyDemo[HIST_DAYS];

// DEMO / INITIAL DATA: factors of the baseline for Day 1..Day 10 (realistic slow drift)
const float DEMO_FACTORS[HIST_DAYS] = {0.98f, 1.00f, 1.01f, 0.99f, 1.02f, 1.01f, 1.03f, 1.02f, 1.04f, 1.03f};

struct HistStore
{
  float   r[HIST_DAYS];
  uint8_t demo[HIST_DAYS];
};

float    todaySum = 0;
uint32_t todayCount = 0;
uint32_t dayStartMs = 0;
uint32_t lastHistSave = 0;

// ================= WEB PAGE (pure HTML/CSS/JS, works offline) =================
const char DASH_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Plant Health Monitor</title>
<style>
body{font-family:sans-serif;margin:0;background:#eef1f4;color:#222}
header{background:#2e7d32;color:#fff;padding:14px 16px}header h1{margin:0;font-size:1.3em}
main{max-width:560px;margin:0 auto;padding:10px}
.card{background:#fff;border-radius:10px;padding:12px 14px;margin:10px 0;box-shadow:0 1px 3px #0002}
.card h3{margin:0 0 8px;font-size:1em;color:#555}
table{width:100%;border-collapse:collapse}td,th{padding:6px 4px;border-bottom:1px solid #eee;text-align:left;font-size:.95em}
.b{display:inline-block;padding:8px 14px;border-radius:8px;color:#fff;font-weight:bold}
.ok{background:#2e7d32}.md{background:#f9a825}.cr{background:#d32f2f}.er{background:#757575}
.big{font-size:2em;font-weight:bold}.fd{font-size:1.8em;padding:14px 18px}
canvas{width:100%;height:200px}.lg{font-size:.8em;color:#666}
.td td{font-weight:bold;background:#f1f8e9}
button{padding:10px 14px;font-size:1em}
</style></head><body>
<header><h1>Plant Health Monitor</h1></header><main>
<div class="card"><h3>ESP32 CONNECTION STATUS</h3><div id="conn" class="b er">--</div><table id="ct"></table></div>
<div class="card"><h3>LIVE SENSOR VALUES</h3><table id="lv"></table></div>
<div class="card"><h3>CURRENT RESISTANCE</h3><div id="cr" class="big">--</div><div id="cs"></div></div>
<div class="card"><h3>RESISTANCE CONDITION</h3><div id="cd" class="b er">--</div></div>
<div class="card"><h3>10-DAY RESISTANCE GRAPH</h3><canvas id="cv"></canvas>
<div class="lg">Orange dot = DEMO / INITIAL data &nbsp; Green dot = real measured data &nbsp; (X: Day 1-10, Y: k&#937;)</div></div>
<div class="card"><h3>10-DAY DATA TABLE</h3><table id="ht"></table></div>
<div class="card"><h3>TREND ANALYSIS</h3><table id="tr"></table></div>
<div class="card"><h3>FINAL DECISION</h3><div id="fd" class="b fd er">--</div><p id="fw"></p></div>
<p><button onclick="rc()">Recalibrate baseline</button></p>
<div class="card"><h3>SOIL SENSOR CALIBRATION</h3>
<p class="lg">1) Sensor in AIR (dry) &rarr; press DRY. 2) Sensor in WATER (wet) &rarr; press WET. Wait 5 s before pressing so the raw value is fresh.</p>
<p><button onclick="sc('dry')">Set DRY (in air)</button> <button onclick="sc('wet')">Set WET (in water)</button></p></div>
</main>
<script>
const $=i=>document.getElementById(i);
const K={NORMAL:'ok',MEDIUM:'md',SEVERE:'cr',CRITICAL:'cr'};
function sc(t){fetch('/soil'+t,{method:'POST'}).then(r=>alert(r.ok?'Saved':'Failed - soil sensor reading invalid, check wiring')).catch(()=>alert('ESP32 not reachable'));}
const cls=t=>K[t]||'er';
function bd(el,t,c){el.textContent=t;el.className=el.className.split(' ').filter(x=>x!='ok'&&x!='md'&&x!='cr'&&x!='er').join(' ')+' '+c;}
function row(a,b){return '<tr><td>'+a+'</td><td>'+b+'</td></tr>';}
function up(s){const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h+' h '+m+' min';}
function rc(){if(confirm('Keep the plant healthy and the electrode attached. Takes about 20 s.'))fetch('/recal',{method:'POST'});}
function draw(v,dm){
 const c=$('cv');c.width=c.clientWidth;c.height=200;
 const x=c.getContext('2d'),W=c.width,H=c.height,L=44,B=24,T=10,R=10;
 x.clearRect(0,0,W,H);
 let mn=Math.min(...v),mx=Math.max(...v);if(mx-mn<1e-6){mn-=1;mx+=1;}
 const pad=(mx-mn)*.15;mn-=pad;mx+=pad;
 const X=i=>L+i*(W-L-R)/9,Y=y=>H-B-(y-mn)/(mx-mn)*(H-B-T);
 x.strokeStyle='#ddd';x.fillStyle='#666';x.font='11px sans-serif';x.lineWidth=1;
 for(let k=0;k<=4;k++){const y=mn+(mx-mn)*k/4;x.beginPath();x.moveTo(L,Y(y));x.lineTo(W-R,Y(y));x.stroke();x.fillText(y.toFixed(1),2,Y(y)+4);}
 for(let i=0;i<10;i++)x.fillText('D'+(i+1),X(i)-8,H-8);
 x.strokeStyle='#1e88e5';x.lineWidth=2;x.beginPath();
 v.forEach((y,i)=>{if(i)x.lineTo(X(i),Y(y));else x.moveTo(X(i),Y(y));});x.stroke();
 v.forEach((y,i)=>{x.fillStyle=dm[i]?'#f9a825':'#43a047';x.beginPath();x.arc(X(i),Y(y),4,0,7);x.fill();});
}
async function u(){
 try{
  const ac=new AbortController();const to=setTimeout(()=>ac.abort(),4000);
  const d=await(await fetch('/data',{cache:'no-store',signal:ac.signal})).json();clearTimeout(to);
  const w=d.wifi,r=d.res,e=d.env,h=d.hist;
  bd($('conn'),w.ok?'CONNECTED':'WiFi Disconnected',w.ok?'ok':'er');
  $('ct').innerHTML=row('Hotspot',w.ssid||'-')+row('IP address',w.ip)+row('Signal',w.rssi+' dBm')+row('Uptime',up(w.up));
  $('lv').innerHTML=
   row('Temperature',e.dht_ok?e.temp.toFixed(1)+' \u00B0C':'Sensor Error')+
   row('Humidity',e.dht_ok?e.hum.toFixed(0)+' %':'Sensor Error')+
   row('Soil moisture',e.soilok?e.soil+' %  (raw '+e.soilraw+')':'Sensor Error (raw '+e.soilraw+')')+
   row('Electrode resistance',r.valid?r.kohm.toFixed(1)+' k\u03A9':'Invalid / Sensor Error')+
   row('Health index',r.valid?r.health+' / 100':'--')+
   row('Baseline',r.base.toFixed(1)+' k\u03A9')+
   row('Deviation',r.valid?r.dev.toFixed(1)+' %':'--');
  $('cr').textContent=r.valid?r.kohm.toFixed(1)+' k\u03A9':'Invalid / Sensor Error';
  $('cs').textContent='Baseline '+r.base.toFixed(1)+' k\u03A9';
  bd($('cd'),r.cond,cls(r.cond));
  draw(h.v,h.demo);
  let t='<tr><th>Day</th><th>Resistance (k\u03A9)</th><th>Data</th></tr>';
  h.v.forEach((y,i)=>{t+='<tr'+(i==9?' class="td"':'')+'><td>Day '+(i+1)+(i==9?' (today)':'')+'</td><td>'+y.toFixed(1)+'</td><td>'+(h.demo[i]?'DEMO':'LIVE')+'</td></tr>';});
  $('ht').innerHTML=t;
  const ar={INCREASING:'\u2191 INCREASING',DECREASING:'\u2193 DECREASING',STABLE:'\u2192 STABLE'};
  $('tr').innerHTML=row('Current day','Day 10 (today)')+row('Trend',ar[h.trend]||h.trend)+
   row('Change (first 5 d vs last 5 d)',h.tpct.toFixed(1)+' %')+
   row('Average (10 days)',h.avg.toFixed(1)+' k\u03A9')+row('Minimum',h.min.toFixed(1)+' k\u03A9')+
   row('Maximum',h.max.toFixed(1)+' k\u03A9')+row('Current vs average',h.vsavg);
  bd($('fd'),d.dec.v,cls(d.dec.v));$('fw').textContent=d.dec.why;
 }catch(err){
  bd($('conn'),'WiFi Disconnected / ESP32 not reachable','er');
 }
}
u();setInterval(u,3000);
</script></body></html>)rawliteral";

// ================= SMALL HELPERS =================
String jsonEscape(const String &s)
{
  String o;
  for (size_t i = 0; i < s.length(); i++)
  {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c >= 32) o += c;
  }
  return o;
}

bool hotspotConfigured()
{
  return strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "YOUR_HOTSPOT_NAME") != 0;
}

const char *wlName(wl_status_t s)
{
  switch (s)
  {
    case WL_IDLE_STATUS:     return "IDLE";
    case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL (hotspot not found: wrong name / 5 GHz only / out of range)";
    case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
    case WL_CONNECTED:       return "CONNECTED";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED (wrong password?)";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED:    return "DISCONNECTED";
    default:                 return "UNKNOWN";
  }
}

const char *reasonText(uint8_t r)
{
  switch (r)
  {
    case 0:   return "none";
    case 2:   return "AUTH_EXPIRE";
    case 8:   return "ASSOC_LEAVE";
    case 15:
    case 204: return "HANDSHAKE_TIMEOUT (wrong password or weak signal)";
    case 201: return "NO_AP_FOUND (wrong hotspot name, out of range, or hotspot is 5 GHz only)";
    case 202: return "AUTH_FAIL (wrong password)";
    case 205: return "CONNECTION_FAIL";
    default:  return "see ESP-IDF Wi-Fi reason codes";
  }
}

// ================= WI-FI DEBUG =================
void printWifiDebug()
{
  wl_status_t st = WiFi.status();

  Serial.println("---------- WIFI (hotspot only) ----------");
  Serial.print("Hotspot SSID : "); Serial.println(cfgSsid);
  Serial.printf("WiFi status  : %d (%s)\n", (int)st, wlName(st));
  Serial.printf("Attempts     : %lu\n", (unsigned long)wifiAttempts);
  Serial.print("Local IP     : ");
  Serial.println(st == WL_CONNECTED ? WiFi.localIP().toString() : String("-"));
  Serial.printf("RSSI         : %d dBm\n", st == WL_CONNECTED ? (int)WiFi.RSSI() : 0);
  Serial.printf("Web Server   : %s\n", serverStarted ? "Started" : "Not started");
  if (st != WL_CONNECTED && lastDisconnectReason)
    Serial.printf("Last reason  : %d (%s)\n", (int)lastDisconnectReason, reasonText(lastDisconnectReason));
}

void onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info)
{
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
    lastDisconnectReason = info.wifi_sta_disconnected.reason;
  else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)
    lastDisconnectReason = 0;
}

// ================= 10-DAY HISTORY FUNCTIONS =================
void saveHistory()
{
  HistStore h;
  for (int i = 0; i < HIST_DAYS; i++)
  {
    h.r[i] = resistanceHistory[i];
    h.demo[i] = historyDemo[i];
  }
  prefs.putBytes("hist2", &h, sizeof(h));
  lastHistSave = millis();
}

bool loadHistory()
{
  if (prefs.getBytesLength("hist2") != sizeof(HistStore))
    return false;

  HistStore h;
  prefs.getBytes("hist2", &h, sizeof(h));
  for (int i = 0; i < HIST_DAYS; i++)
  {
    resistanceHistory[i] = h.r[i];
    historyDemo[i] = h.demo[i];
  }
  return true;
}

// (Re)generate DEMO / INITIAL entries around the baseline. Real (LIVE) days are never touched.
void refreshDemoHistory()
{
  float base = baselineReady ? baselineResistance / 1000.0f : DEMO_FALLBACK_BASE_KOHM;
  bool changed = false;

  for (int i = 0; i < HIST_DAYS; i++)
  {
    if (historyDemo[i])
    {
      resistanceHistory[i] = base * DEMO_FACTORS[i];
      changed = true;
    }
  }
  if (changed) saveHistory();
}

void fillDemoHistory()
{
  for (int i = 0; i < HIST_DAYS; i++) historyDemo[i] = 1;
  refreshDemoHistory();
}

// Today's slot (Day 10) = running average of today's valid readings
void updateTodayHistory(float rKohm)
{
  todaySum += rKohm;
  todayCount++;
  resistanceHistory[HIST_DAYS - 1] = todaySum / todayCount;
  historyDemo[HIST_DAYS - 1] = 0;
}

// Every DAY_LENGTH_MS the oldest day drops out and a new "today" starts.
void checkDayRollover()
{
  if (millis() - dayStartMs < DAY_LENGTH_MS) return;

  dayStartMs += DAY_LENGTH_MS;

  for (int i = 0; i < HIST_DAYS - 1; i++)
  {
    resistanceHistory[i] = resistanceHistory[i + 1];
    historyDemo[i] = historyDemo[i + 1];
  }
  // new today starts from yesterday's value until the first live reading arrives
  todaySum = 0;
  todayCount = 0;
  saveHistory();
  Serial.println("New day started - 10-day history shifted and saved.");
}

void calcStats(float &avg, float &mn, float &mx)
{
  avg = 0;
  mn = resistanceHistory[0];
  mx = resistanceHistory[0];
  for (int i = 0; i < HIST_DAYS; i++)
  {
    avg += resistanceHistory[i];
    if (resistanceHistory[i] < mn) mn = resistanceHistory[i];
    if (resistanceHistory[i] > mx) mx = resistanceHistory[i];
  }
  avg /= HIST_DAYS;
}

// Trend = average of last 5 days vs average of first 5 days
const char *calculateTrend(float &pct)
{
  const int h = HIST_DAYS / 2;
  float first = 0, last = 0;
  for (int i = 0; i < h; i++) first += resistanceHistory[i];
  for (int i = HIST_DAYS - h; i < HIST_DAYS; i++) last += resistanceHistory[i];
  first /= h;
  last /= h;

  if (first <= 0) { pct = 0; return "STABLE"; }

  pct = (last - first) / first * 100.0f;
  if (pct > TREND_STABLE_PCT)  return "INCREASING";
  if (pct < -TREND_STABLE_PCT) return "DECREASING";
  return "STABLE";
}

// ================= HEALTH (existing logic) =================
float calculateDeviation(float current)
{
  if (baselineResistance <= 0)
    return 0;

  return fabsf(current - baselineResistance) / baselineResistance * 100.0f;
}

// Health index from ABSOLUTE resistance (kOhm):
// <10 -> 100..75 (NORMAL), 10..20 -> 74..40 (MEDIUM), >20 -> below 40 (CRITICAL)
int calculateHealthIndexFromKohm(float kohm)
{
  float index;

  if (kohm < RES_GOOD_MAX_KOHM)
    index = 100.0f - kohm * 2.5f;
  else if (kohm <= RES_MEDIUM_MAX_KOHM)
    index = 74.0f - (kohm - RES_GOOD_MAX_KOHM) * 3.4f;
  else
    index = 39.0f - (kohm - RES_MEDIUM_MAX_KOHM) * 2.0f;

  index = constrain(index, 0, 100);
  return (int)index;
}

String getStatus(int healthIndex)
{
  if (healthIndex >= RESISTANCE_NORMAL_LIMIT) return "NORMAL";
  if (healthIndex >= RESISTANCE_MEDIUM_LIMIT) return "MEDIUM";
  return "SEVERE";
}

// Short message for Serial / OLED (max 21 characters for the OLED line)
const char *statusMessage(const String &s)
{
  if (s == "NORMAL") return "NORMAL: Plant GOOD";
  if (s == "MEDIUM") return "MEDIUM: Virus early";
  return "SEVERE: Very bad!";
}

// 0 = NORMAL, 1 = MEDIUM, 2 = CRITICAL, 3 = SENSOR ERROR, 4 = NO BASELINE
int analyzeResistance(bool valid, int healthIndex)
{
  if (!valid) return 3;

  String s = getStatus(healthIndex);
  if (s == "NORMAL") return 0;
  if (s == "MEDIUM") return 1;
  return 2;
}

const char *conditionName(int level)
{
  switch (level)
  {
    case 0:  return "NORMAL";
    case 1:  return "MEDIUM";
    case 2:  return "SEVERE";
    case 3:  return "SENSOR ERROR";
    default: return "NO BASELINE";
  }
}

// Final decision = current condition + 10-day trend (is resistance moving away from baseline?)
const char *getFinalDecision(int level, const char *trend, float curK, float baseK, String &why)
{
  if (level == 3) { why = "Resistance invalid or electrode not connected (Invalid / Sensor Error)"; return "SENSOR ERROR"; }
  if (level == 4) { why = "Baseline not set yet - press Recalibrate baseline"; return "NO BASELINE"; }

  if (level == 2) { why = "SEVERE: resistance above 20 kOhm - the plant is very badly affected"; return "SEVERE"; }
  if (level == 1) { why = "MEDIUM: resistance 10 to 20 kOhm - a virus or disease attack has started, the plant is slightly affected (early stage)"; return "MEDIUM"; }

  why = "NORMAL: resistance below 10 kOhm - the plant is healthy and in good condition";
  return "NORMAL";
}

// ================= WEB HANDLERS =================
void handleDash()
{
  server.send_P(200, "text/html", DASH_PAGE);
}

void handleRecal()
{
  recalRequested = true;
  server.send(200, "text/plain", "OK");
}

// Soil calibration: sensor in AIR -> /soildry ; sensor in WATER -> /soilwet
void handleSoilDry()
{
  if (g_soilRaw < 50 || g_soilRaw > 4085) { server.send(400, "text/plain", "Soil sensor reading invalid"); return; }
  soilDry = g_soilRaw;
  prefs.putInt("sdry", soilDry);
  server.send(200, "text/plain", "OK");
}

void handleSoilWet()
{
  if (g_soilRaw < 50 || g_soilRaw > 4085) { server.send(400, "text/plain", "Soil sensor reading invalid"); return; }
  soilWet = g_soilRaw;
  prefs.putInt("swet", soilWet);
  server.send(200, "text/plain", "OK");
}

void handleData()
{
  bool conn = (WiFi.status() == WL_CONNECTED);
  float curK  = g_resistance / 1000.0f;
  float baseK = baselineResistance / 1000.0f;

  float avg, mn, mx;
  calcStats(avg, mn, mx);

  float tpct = 0;
  const char *trend = calculateTrend(tpct);

  int level = analyzeResistance(g_resValid, g_health);
  String why;
  const char *decision = getFinalDecision(level, trend, curK, baseK, why);

  const char *vsAvg = "N/A";
  if (g_resValid && avg > 0)
  {
    float d = (curK - avg) / avg * 100.0f;
    if (d > 1.0f) vsAvg = "ABOVE AVERAGE";
    else if (d < -1.0f) vsAvg = "BELOW AVERAGE";
    else vsAvg = "NEAR AVERAGE";
  }

  String ssid = conn ? WiFi.SSID() : cfgSsid;
  String ip = WiFi.localIP().toString();

  String j;
  j.reserve(1300);

  j += F("{\"wifi\":{\"ok\":");     j += conn ? "true" : "false";
  j += F(",\"ssid\":\"");           j += jsonEscape(ssid);
  j += F("\",\"ip\":\"");           j += ip;
  j += F("\",\"rssi\":");           j += conn ? (int)WiFi.RSSI() : 0;
  j += F(",\"up\":");               j += (unsigned long)(millis() / 1000UL);

  j += F("},\"res\":{\"valid\":");  j += g_resValid ? "true" : "false";
  j += F(",\"kohm\":");             j += String(curK, 1);
  j += F(",\"base\":");             j += String(baseK, 1);
  j += F(",\"dev\":");              j += String(g_deviation, 1);
  j += F(",\"health\":");           j += g_health;
  j += F(",\"cond\":\"");           j += conditionName(level);
  j += F("\"},\"env\":{\"temp\":");  j += String(g_temp, 1);
  j += F(",\"hum\":");              j += String(g_hum, 1);
  j += F(",\"dht_ok\":");           j += g_dhtOk ? "true" : "false";
  j += F(",\"soil\":");             j += g_soil;
  j += F(",\"soilraw\":");          j += g_soilRaw;
  j += F(",\"soilok\":");           j += (g_soilRaw >= 50 && g_soilRaw <= 4085) ? "true" : "false";

  j += F("},\"hist\":{\"v\":[");
  for (int i = 0; i < HIST_DAYS; i++)
  {
    if (i) j += ',';
    j += String(resistanceHistory[i], 1);
  }
  j += F("],\"demo\":[");
  for (int i = 0; i < HIST_DAYS; i++)
  {
    if (i) j += ',';
    j += historyDemo[i] ? "1" : "0";
  }
  j += F("],\"avg\":");             j += String(avg, 1);
  j += F(",\"min\":");              j += String(mn, 1);
  j += F(",\"max\":");              j += String(mx, 1);
  j += F(",\"trend\":\"");          j += trend;
  j += F("\",\"tpct\":");           j += String(tpct, 1);
  j += F(",\"vsavg\":\"");          j += vsAvg;

  j += F("\"},\"dec\":{\"v\":\"");  j += decision;
  j += F("\",\"why\":\"");          j += jsonEscape(why);
  j += F("\"}}");

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.send(200, "application/json", j);
}

void handleNotFound()
{
  server.send(404, "text/plain", "Not found");
}

// Web server starts only after the hotspot is connected
void startWebServer()
{
  if (serverStarted) return;

  server.on("/", HTTP_GET, handleDash);
  server.on("/data", HTTP_GET, handleData);
  server.on("/data", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    server.send(204);
  });
  server.on("/recal", HTTP_POST, handleRecal);
  server.on("/soildry", HTTP_POST, handleSoilDry);
  server.on("/soilwet", HTTP_POST, handleSoilWet);
  server.onNotFound(handleNotFound);
  server.begin();
  serverStarted = true;

  Serial.println("Web Server Started");
  Serial.print("Open in browser: http://");
  Serial.println(WiFi.localIP());
}

void serviceWeb()
{
  if (serverStarted) server.handleClient();
}

void waitMs(uint32_t ms)
{
  uint32_t t = millis();
  while (millis() - t < ms)
  {
    serviceWeb();
    delay(5);
  }
}

// ================= OLED (HARDWARE I2C) =================
// Scan the I2C bus first so a wrong address / wrong wiring is easy to spot in Serial Monitor.
// Wire stays running afterwards: U8g2 hardware I2C uses the same bus.
bool detectOled()
{
  oledAddr = 0;
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(100000);
  delay(50);
  Serial.println("Scanning I2C bus for the OLED...");

  for (uint8_t addr = 1; addr < 127; addr++)
  {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0)
    {
      Serial.printf("  I2C device found at 0x%02X\n", addr);
      if (addr == 0x3C || addr == 0x3D) oledAddr = addr;
    }
  }

  if (oledAddr == 0)
    Serial.println("  OLED NOT FOUND -> check SDA/SCL pins, 3.3V, GND, and that pins match OLED_SDA/OLED_SCL.");

  return oledAddr != 0;
}

void oledInit()
{
  oledOk = detectOled();
  if (!oledOk) return;

  u8g2.setI2CAddress(oledAddr << 1);
  u8g2.begin();
  u8g2.setBusClock(400000);
  u8g2.setFont(u8g2_font_6x10_tf);
  Serial.printf("OLED ready at 0x%02X\n", oledAddr);
}

void oledLines(const char *a, const char *b = nullptr, const char *c = nullptr,
               const char *d = nullptr, const char *e = nullptr)
{
  if (!oledOk) return;

  const char *lines[5] = {a, b, c, d, e};
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  for (int i = 0; i < 5; i++)
    if (lines[i]) u8g2.drawStr(0, 10 + i * 12, lines[i]);
  u8g2.sendBuffer();
}

void oledDashboard()
{
  if (!oledOk) return;

  char l1[24], l2[24], l3[24], l4[24], l5[24];
  snprintf(l1, sizeof(l1), "%s", statusMessage(g_status));
  if (g_resValid) snprintf(l2, sizeof(l2), "Elec %.1f kOhm", g_resistance / 1000.0f);
  else            snprintf(l2, sizeof(l2), "Elec: Sensor Error");
  if (g_soilRaw >= 50 && g_soilRaw <= 4085) snprintf(l3, sizeof(l3), "Soil %d%%  Hum %.0f%%", g_soil, g_hum);
  else                                      snprintf(l3, sizeof(l3), "Soil ERR  Hum %.0f%%", g_hum);
  snprintf(l4, sizeof(l4), "Temp %.1f C", g_temp);
  if (WiFi.status() == WL_CONNECTED) snprintf(l5, sizeof(l5), "%s", WiFi.localIP().toString().c_str());
  else                               snprintf(l5, sizeof(l5), "WiFi Disconnected");
  oledLines(l1, l2, l3, l4, l5);
}

// ================= ADC HELPERS =================
uint32_t readMilliVoltsAvg(uint8_t pin, uint8_t n = 16)
{
  uint32_t total = 0;
  for (uint8_t i = 0; i < n; i++)
  {
    total += analogReadMilliVolts(pin);
    delayMicroseconds(200);
  }
  return total / n;
}

int readRawAvg(uint8_t pin, uint8_t n = 16)
{
  uint32_t total = 0;
  for (uint8_t i = 0; i < n; i++)
  {
    total += analogRead(pin);
    delayMicroseconds(200);
  }
  return (int)(total / n);
}

// Electrode is the TOP resistor: 3.3V -> ELECTRODE -> ADC pin -> RFIXED (10k) -> GND
float readElectrodeResistance()
{
#if USE_EXCITATION
  digitalWrite(EXCITE_PIN, HIGH);
  delay(10);
#endif

  float voltage = readMilliVoltsAvg(ELECTRODE_PIN) / 1000.0f;

#if USE_EXCITATION
  digitalWrite(EXCITE_PIN, LOW);
#endif

  g_adcVolt = voltage;

#if ELECTRODE_ON_TOP
  if (voltage <= 0.01f)       // pin at ground -> electrode open / no contact
    return R_OPEN;
  if (voltage >= ADC_MAX_V)   // pin at top -> electrode shorted
    return 0;
  float r = RFIXED * (VCC - voltage) / voltage;
#else
  if (voltage <= 0.01f)       // pin at ground -> electrode shorted
    return 0;
  if (voltage >= ADC_MAX_V)   // pin at top -> electrode open / no contact
    return R_OPEN;
  float r = RFIXED * voltage / (VCC - voltage);
#endif
  return (r >= R_OPEN) ? R_OPEN : r;
}

int readSoilMoisture()
{
  int raw = readRawAvg(SOIL_PIN);
  g_soilRaw = raw;
  if (abs(soilDry - soilWet) < 100) return 0;   // calibration invalid
  int moisture = map(raw, soilDry, soilWet, 0, 100);
  return constrain(moisture, 0, 100);
}

// ================= LED + BUZZER =================
void startBeep(uint32_t ms)
{
  digitalWrite(BUZZER_PIN, HIGH);
  buzzerOn = true;
  buzzerOffAt = millis() + ms;
}

void showStatus(const String &status, bool beepNow)
{
  digitalWrite(RED_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(BLUE_LED, LOW);

  if (status == "NORMAL")
  {
    digitalWrite(BLUE_LED, HIGH);
  }
  else if (status == "MEDIUM" || status == "NO BASELINE")
  {
    digitalWrite(YELLOW_LED, HIGH);
    if (beepNow && status == "MEDIUM") startBeep(150);
  }
  else
  {
    digitalWrite(RED_LED, HIGH);
    if (beepNow) startBeep(400);
  }
}

// ================= BASELINE =================
bool calibrateBaseline()
{
  Serial.println();
  Serial.println("================================");
  Serial.println("BASELINE CALIBRATION");
  Serial.println("Keep plant healthy, electrode attached");
  Serial.println("================================");

  float total = 0;
  int valid = 0;

  for (int i = 0; i < 20; i++)
  {
    float r = readElectrodeResistance();

    Serial.print("Reading ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(r / 1000.0f);
    Serial.println(" kOhm");

    if (oledOk)
    {
      char l2[24];
      snprintf(l2, sizeof(l2), "Reading %d/20", i + 1);
      oledLines("CALIBRATING", l2, "Keep plant healthy");
    }

    if (r > 0 && r < R_OPEN)
    {
      total += r;
      valid++;
    }

    waitMs(1000);
  }

  if (valid >= 10)
  {
    baselineResistance = total / valid;
    baselineReady = true;
    prefs.putFloat("base2", baselineResistance);
    refreshDemoHistory();   // DEMO days follow the new baseline (LIVE days untouched)

    Serial.print("Baseline saved = ");
    Serial.print(baselineResistance / 1000.0f);
    Serial.println(" kOhm");
    return true;
  }

  Serial.println("Calibration FAILED: not enough valid readings. Check electrode contact.");
  oledLines("CALIBRATION FAILED", "Check electrode");
  waitMs(2000);
  return false;
}

// ================= WI-FI (MOBILE HOTSPOT ONLY) =================
// ESP32-C3 only sees 2.4 GHz, so a hotspot missing from this scan is probably 5 GHz-only
void checkHotspotVisible()
{
  Serial.println("Scanning 2.4 GHz networks to find your hotspot...");
  int n = WiFi.scanNetworks();
  bool found = false;

  for (int i = 0; i < n; i++)
  {
    if (WiFi.SSID(i) == cfgSsid)
    {
      found = true;
      Serial.printf("  Found '%s'  RSSI %d dBm  channel %d  (2.4 GHz OK)\n",
                    cfgSsid.c_str(), (int)WiFi.RSSI(i), (int)WiFi.channel(i));
    }
  }

  if (!found)
  {
    Serial.println("  HOTSPOT NOT FOUND in 2.4 GHz scan!");
    Serial.println("  -> Turn ON the hotspot, set band to 2.4 GHz, check the name (upper/lower case), move closer.");
  }

  WiFi.scanDelete();
}

bool connectWifi()
{
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);              // station mode (required for hotspot)
  WiFi.disconnect(false, true);     // forget any Wi-Fi saved earlier: only the hotspot below is used
  delay(100);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);             // more stable link on phone hotspots
  WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);   // accept WPA/WPA2 hotspots
  WiFi.setHostname(HOSTNAME);
#if WIFI_LOW_TX_POWER
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif

  Serial.println();
  Serial.println("Connecting to WiFi...");
  Serial.print("Hotspot SSID: ");
  Serial.println(cfgSsid);
  Serial.println("Reminder: hotspot must be 2.4 GHz and WPA2 (not 5 GHz / WPA3-only).");

  checkHotspotVisible();

  for (int attempt = 1; attempt <= WIFI_MAX_ATTEMPTS; attempt++)
  {
    wifiAttempts++;
    Serial.printf("Connection attempt %d/%d\n", attempt, WIFI_MAX_ATTEMPTS);

    WiFi.disconnect();
    delay(100);
    WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());

    uint32_t t = millis();
    int dots = 0;
    while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_CONNECT_TIMEOUT_MS)
    {
      delay(500);
      Serial.print(".");
      char l3[24];
      snprintf(l3, sizeof(l3), "Connecting%.*s", (dots++ % 4), "...");
      oledLines("HOTSPOT", cfgSsid.c_str(), l3);
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
      return true;

    Serial.println("WiFi connection failed");
    printWifiDebug();
    Serial.println("Retrying...");
  }

  return false;
}

// Called every loop: starts the web server after the first connection and auto-reconnects
void maintainWifi()
{
  static bool wasConnected = false;
  static uint32_t lastTry = 0;

  if (!hotspotConfigured())
  {
    if (millis() - lastTry >= 10000UL)
    {
      lastTry = millis();
      Serial.println("Set your mobile hotspot name/password in WIFI_SSID / WIFI_PASSWORD at the top of the code.");
    }
    return;
  }

  bool nowConnected = (WiFi.status() == WL_CONNECTED);

  if (nowConnected && !wasConnected)
  {
    wasConnected = true;
    Serial.println();
    Serial.println("WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    MDNS.end();
    if (MDNS.begin(HOSTNAME))
    {
      MDNS.addService("http", "tcp", 80);
      Serial.println("Or: http://" HOSTNAME ".local");
    }

    startWebServer();       // only after the hotspot is connected
    printWifiDebug();
  }
  else if (!nowConnected)
  {
    if (wasConnected)
    {
      wasConnected = false;
      lastTry = millis();
      Serial.println("WiFi Disconnected - Reconnecting...");
      WiFi.reconnect();
    }
    else if (millis() - lastTry >= WIFI_RETRY_INTERVAL_MS)
    {
      lastTry = millis();
      wifiAttempts++;
      Serial.println("WiFi connection failed");
      Serial.println("Retrying...");
      printWifiDebug();
      WiFi.disconnect();
      delay(50);
      WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
    }
  }
}

// ================= SETUP =================
String firebaseIdToken;
uint32_t firebaseLastAuth = 0;

String jsonValue(String json, const char *key)
{
  String needle = String("\"") + key + "\":";
  int start = json.indexOf(needle);
  if (start < 0) return "";
  start += needle.length();
  if (json.charAt(start) == '"') start++;
  int end = json.indexOf(json.charAt(start - 1) == '"' ? '"' : ',', start);
  if (end < 0) end = json.indexOf('}', start);
  return json.substring(start, end);
}

bool firebaseSignInAnonymously()
{
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  String url = "https://identitytoolkit.googleapis.com/v1/accounts:signUp?key=";
  url += FIREBASE_API_KEY;
  if (!http.begin(secureClient, url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST("{\"returnSecureToken\":true}");
  String body = http.getString();
  http.end();
  if (code != 200)
  {
    Serial.printf("Firebase anonymous authentication failed: HTTP %d\n", code);
    return false;
  }

  firebaseIdToken = jsonValue(body, "idToken");
  if (firebaseIdToken.length() == 0) return false;
  firebaseLastAuth = millis();
  Serial.println("Firebase anonymous authentication ready.");
  return true;
}

bool sendReadingToBackend(float resistanceKohm, float temperature, float humidity, int soilMoisture, int healthIndex, const String &status)
{
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  HTTPClient http;
  String url = "http://" + String(XYLOSENSE_BACKEND_IP) + ":" + String(XYLOSENSE_BACKEND_PORT) + "/api/readings";
  if (!http.begin(client, url)) return false;
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");

  String body = "{\"treeId\":\"" + String(FIRESTORE_TREE_ID) + "\",";
  body += "\"resistance\":" + String(resistanceKohm, 3) + ",";
  body += "\"temperature\":" + String(temperature, 2) + ",";
  body += "\"humidity\":" + String(humidity, 2) + ",";
  body += "\"soilMoisture\":" + String(soilMoisture) + ",";
  body += "\"healthIndex\":" + String(healthIndex) + ",";
  body += "\"status\":\"" + status + "\"}";

  int code = http.POST(body);
  http.end();
  if (code == 202) {
    Serial.println("Scala backend write accepted.");
    return true;
  }
  Serial.printf("Scala backend write failed: HTTP %d\n", code);
  return false;
}

bool sendReadingToFirestore(float resistanceKohm, float temperature, float humidity, int soilMoisture, int healthIndex, const String &status)
{
  if (WiFi.status() != WL_CONNECTED) return false;
  if (firebaseIdToken.length() == 0 || millis() - firebaseLastAuth > 50UL * 60UL * 1000UL)
    if (!firebaseSignInAnonymously()) return false;

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID);
  url += "/databases/" + String(FIREBASE_DATABASE_ID) + "/documents:commit?key=" + String(FIREBASE_API_KEY);
  if (!http.begin(secureClient, url)) return false;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + firebaseIdToken);

  String documentId = String((uint32_t)(millis() / 1000UL), HEX) + "-" + String(esp_random(), HEX);
  String documentName = "projects/" + String(FIREBASE_PROJECT_ID);
  documentName += "/databases/" + String(FIREBASE_DATABASE_ID) + "/documents/sensor_readings/" + documentId;

  String body = "{\"writes\":[{\"update\":{\"name\":\"" + documentName + "\",\"fields\":{";
  body += "\"treeId\":{\"stringValue\":\"" + String(FIRESTORE_TREE_ID) + "\"}";
  body += ",\"resistance\":{\"doubleValue\":" + String(resistanceKohm, 3) + "}";
  body += ",\"temperature\":{\"doubleValue\":" + String(temperature, 2) + "}";
  body += ",\"humidity\":{\"doubleValue\":" + String(humidity, 2) + "}";
  body += ",\"soilMoisture\":{\"integerValue\":\"" + String(soilMoisture) + "\"}";
  body += ",\"healthIndex\":{\"integerValue\":\"" + String(healthIndex) + "\"}";
  body += ",\"status\":{\"stringValue\":\"" + status + "\"}";
  body += "}},\"updateTransforms\":[{\"fieldPath\":\"timestamp\",\"setToServerValue\":\"REQUEST_TIME\"}]}]}";

  int code = http.POST(body);
  String response = http.getString();
  http.end();
  if (code == 200)
  {
    Serial.print("Firestore write complete: ");
    Serial.println(documentId);
    return true;
  }

  Serial.printf("Firestore write failed: HTTP %d\n", code);
  Serial.println(response);
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  // ADC
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // Outputs
  pinMode(RED_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(RED_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(BLUE_LED, LOW);
  digitalWrite(BUZZER_PIN, LOW);

#if USE_EXCITATION
  pinMode(EXCITE_PIN, OUTPUT);
  digitalWrite(EXCITE_PIN, LOW);
#endif

  pinMode(DHT_PIN, INPUT_PULLUP);   // helps bare DHT sensors without an external pull-up
  dht.begin();
  delay(2000);                      // DHT needs 1-2 s after power-up before the first read

  prefs.begin("plant", false);
  prefs.remove("ssid");   // old saved Wi-Fi from earlier versions is removed
  prefs.remove("pass");
  soilDry = prefs.getInt("sdry", SOIL_DRY_RAW);
  soilWet = prefs.getInt("swet", SOIL_WET_RAW);
  baselineResistance = prefs.getFloat("base2", 0);   // new key: forces a fresh auto-calibration after this upload
  baselineReady = baselineResistance > 0;
  recalRequested = !baselineReady;   // first run: calibrate automatically

  // 10-day history: load from flash, otherwise start with DEMO / INITIAL data
  if (!loadHistory())
  {
    fillDemoHistory();
    Serial.println("10-day history: DEMO / INITIAL data created (replaced by real data over time).");
  }
  dayStartMs = millis();

  cfgSsid = WIFI_SSID;
  cfgPass = WIFI_PASSWORD;
  WiFi.onEvent(onWiFiEvent);

  // OLED
  oledInit();
  oledLines("PLANT MONITOR", "Starting...");

  // Mobile hotspot connection (the only network this device will use)
  if (!hotspotConfigured())
  {
    Serial.println("WIFI_SSID is empty or still the placeholder -> set your hotspot name/password in the code.");
    oledLines("SET HOTSPOT NAME", "AND PASSWORD", "IN THE CODE");
  }
  else if (!connectWifi())
  {
    Serial.println("WiFi connection failed - will keep retrying in the background.");
    Serial.println("Check: hotspot ON, 2.4 GHz band, WPA2, correct SSID/password.");
    oledLines("HOTSPOT FAILED", "Retrying...", "Check 2.4GHz hotspot");
  }
  // If connected, maintainWifi() (in loop) starts the web server and prints the IP.

  Serial.println("System Ready!");
}

// ================= LOOP =================
void loop()
{
  serviceWeb();
  maintainWifi();

  // Non-blocking buzzer off
  if (buzzerOn && (int32_t)(millis() - buzzerOffAt) >= 0)
  {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerOn = false;
  }

  // Calibration requested (first run or from the web page)
  if (recalRequested)
  {
    recalRequested = false;
    calibrateBaseline();
  }

  // 10-day history housekeeping
  checkDayRollover();
  if (millis() - lastHistSave >= HISTORY_SAVE_MS) saveHistory();

  static uint32_t lastUpdate = 0;
  static bool firstRun = true;

  if (!firstRun && (millis() - lastUpdate) < SENSOR_INTERVAL_MS)
  {
    delay(5);
    return;
  }
  firstRun = false;
  lastUpdate = millis();

  // ---- Read sensors ----
  float resistance = readElectrodeResistance();
  int soilMoisture = readSoilMoisture();

  static float lastTemp = 0, lastHum = 0;
  static uint8_t dhtFails = 0;
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h))
  {
    waitMs(1200);                     // one retry after the DHT's minimum sample gap
    t = dht.readTemperature(false, true);   // force a fresh read
    h = dht.readHumidity(true);
  }
  if (!isnan(t) && !isnan(h))
  {
    lastTemp = t;     // keep last good value if a read fails
    lastHum = h;
    dhtFails = 0;
    Serial.printf("DHT OK: %.1f C, %.0f %%\n", t, h);
  }
  else
  {
    Serial.println("DHT read FAILED -> check DHT type (DHT_IS_DHT22), DATA wire on GPIO 10, 3.3V, GND");
    if (dhtFails < 255) dhtFails++;
  }
  g_dhtOk = (dhtFails < 3);   // "Sensor Error" after 3 failed reads in a row

  // ---- Resistance validity + 10-day history ----
  bool resValid = (resistance > 0 && resistance < R_OPEN);
  g_resValid = resValid;
  if (resValid) updateTodayHistory(resistance / 1000.0f);

  // ---- Health ----
  float deviation = calculateDeviation(resistance);
  int healthIndex = calculateHealthIndexFromKohm(resistance / 1000.0f);

  if (!resValid)                 // no electrode contact or shorted
    healthIndex = 0;

  String status = getStatus(healthIndex);

  // ---- Share with web page + OLED ----
  g_resistance = resistance;
  g_deviation  = deviation;
  g_soil       = soilMoisture;
  g_temp       = lastTemp;
  g_hum        = lastHum;
  g_health     = healthIndex;
  g_status     = status;

  // ---- Serial ----
  Serial.println();
  Serial.println("========== PLANT ==========");
  Serial.print("Resistance : "); Serial.print(resistance / 1000.0f); Serial.println(" kOhm");
  Serial.print("ADC volt   : "); Serial.print(g_adcVolt, 3); Serial.println(" V");
  Serial.print("Baseline   : "); Serial.print(baselineResistance / 1000.0f); Serial.println(" kOhm");
  Serial.print("Deviation  : "); Serial.print(deviation); Serial.println(" %");
  Serial.print("Health     : "); Serial.print(healthIndex); Serial.println("/100");
  Serial.print("Status     : "); Serial.println(status);
  Serial.print("Message    : "); Serial.println(statusMessage(status));
  Serial.print("Soil       : "); Serial.print(soilMoisture); Serial.println(" %");
  Serial.print("Soil raw   : "); Serial.print(g_soilRaw);
  Serial.print("  (dry="); Serial.print(soilDry); Serial.print(", wet="); Serial.print(soilWet); Serial.println(")");
  if (g_soilRaw < 50 || g_soilRaw > 4085) Serial.println("Soil       : Sensor Error -> check AOUT wire on GPIO 1, VCC 3.3V, GND");
  Serial.print("Temperature: "); Serial.print(lastTemp); Serial.println(" C");
  Serial.print("Humidity   : "); Serial.print(lastHum); Serial.println(" %");
  if (!resValid) Serial.println("Resistance : Invalid / Sensor Error");
  if (!g_dhtOk)  Serial.println("DHT        : Sensor Error");
  if (hotspotConfigured()) printWifiDebug();

  // ---- LEDs + buzzer (beep on change, then every 30 s while not NORMAL) ----
  static String lastStatus = "";
  static uint32_t lastBeep = 0;
  bool beepNow = (status != lastStatus) || (millis() - lastBeep >= BEEP_REPEAT_MS);
  showStatus(status, beepNow);
  if (beepNow) lastBeep = millis();
  lastStatus = status;

  // ---- OLED ----
  oledDashboard();

  static uint32_t lastFirebaseWrite = 0;
  if (WiFi.status() == WL_CONNECTED && millis() - lastFirebaseWrite >= SENSOR_INTERVAL_MS)
  {
    lastFirebaseWrite = millis();
    sendReadingToBackend(
      resistance / 1000.0f,
      lastTemp,
      lastHum,
      soilMoisture,
      healthIndex,
      status
    );
    sendReadingToFirestore(
      resistance / 1000.0f,
      lastTemp,
      lastHum,
      soilMoisture,
      healthIndex,
      status
    );
  }
}
