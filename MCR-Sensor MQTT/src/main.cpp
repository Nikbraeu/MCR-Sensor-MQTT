/*
ESP32 Sender mit allen Funktionen:
- Liest BME280 und BME680 aus
- Sendet die Daten per MQTT an einen lokalen Broker
- Loggt die Daten auf eine SD-Karte im CSV-Format
- Spiegelt die Daten in eine Cloud (EMQX) per MQTT
*/

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include <Wire.h>
#include <PicoMQTT.h>          // lokaler Broker (1883) muss auf MQTT Protokoll 3.1.1 eingestellt sein!
#include <PubSubClient.h>      // Cloud-Client (TLS zu EMQX)
#include <ArduinoJson.h>

#include <Adafruit_BME280.h>
#include <Adafruit_BME680.h>

#include <SPI.h>
#include <SD.h>
#include "secrets.h"          // enthält nur Cloud Zugangsdaten
#include <WebServer.h>        // <--- NEU
#include <Preferences.h>      // <--- NEU

// ======== WLAN =========
// (entfernt: WIFI_SSID / WIFI_PASS stehen jetzt in secrets.h)

// ======== EMQX Cloud (TLS, 8883) =========
// (entfernt: CLOUD_HOST / CLOUD_PORT / CLOUD_USER / CLOUD_PASS jetzt in secrets.h)

// Root-CA PEM der EMQX-Instanz (BEGIN/END inkl.)
static const char ROOT_CA[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR
TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw
DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr
hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg
06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF
PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls
YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk
CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=
-----END CERTIFICATE-----
)PEM";

// ======== Topics ========
const char* TOPIC_ENV    = "smarthome/senderlukas/env";    // kombiniert (beide Sensoren)
const char* TOPIC_STATUS = "smarthome/senderlukas/status";
const char* REMOTE_PREFIX = ""; // optional z.B. "edge/"

// ======== Sensoren / I2C ========
#define I2C_SDA 21
#define I2C_SCL 22
#define BME280_ADDR 0x76
#define BME680_ADDR 0x77

Adafruit_BME280 bme280;
Adafruit_BME680 bme680;

// ======== SD-Karte (SPI) ========
const int SD_CS = 5;
const char* CSV_PATH = "/smarthome.csv";

// ======== Intervall ========
const uint32_t SEND_EVERY_MS = 60e3; // 60 Sekunden

// ======== MQTT Objekte ========
PicoMQTT::Server broker;  // lokaler MQTT-Broker (Port 1883)
WiFiClientSecure tls;
PubSubClient cloud(tls);

// ======== State ========
unsigned long lastSend = 0;
bool bme280_ok = false, bme680_ok = false;

void ensureTime() {
  static bool ok = false;
  if (ok) return;
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  time_t now = 0; int tries = 0;
  while (now < 1700000000 && tries < 60) { // ~2023-11
    delay(250);
    now = time(nullptr);
    tries++;
  }
  ok = (now >= 1700000000);
}

void ensureCloud() {
  if (cloud.connected()) return;
  tls.setCACert(ROOT_CA);
  tls.setTimeout(15);
  cloud.setServer(CLOUD_HOST, CLOUD_PORT);
  cloud.setKeepAlive(60);
  cloud.setSocketTimeout(15);
  cloud.setBufferSize(1024);
  while (!cloud.connected()) {
    String cid = "bridge-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    // Last Will: offline
    if (cloud.connect(
          cid.c_str(),
          CLOUD_USER, CLOUD_PASS,
          "smarthome/senderlukas/status", // will topic
          1, true,                        // QoS1, retain
          "offline"                       // will payload
        )) {
      cloud.publish("smarthome/senderlukas/status","online", true);
      String ip = WiFi.localIP().toString();
      cloud.publish("smarthome/senderlukas/ip", ip.c_str(), true);
      break;
    }
    delay(1000);
  }
}

String mapTopic(const char* in) {
  if (REMOTE_PREFIX && REMOTE_PREFIX[0]) { String out = REMOTE_PREFIX; out += in; return out; }
  return String(in);
}

void forwardToCloud(const char* topic, const char* payload) {
  ensureCloud();
  String t = mapTopic(topic);
  cloud.publish(t.c_str(), (const uint8_t*)payload, strlen(payload), true /*retain in Cloud*/);
}

// ======== SD Logging ========
bool sdReady = false;

bool initSD() {
  if (sdReady) return true;
  if (!SD.begin(SD_CS)) return false;
  // Header anlegen, falls Datei neu
  if (!SD.exists(CSV_PATH)) {
    File f = SD.open(CSV_PATH, FILE_WRITE);
    if (!f) return false;
    f.println("iso8601,epoch,t280,rh280,p280,t680,rh680,p680,gasOhm");
    f.close();
  }
  sdReady = true;
  return true;
}

void appendCSV(time_t ts, float t280, float h280, float p280,
               float t680, float h680, float p680, float gas) {
  if (!initSD()) return;
  File f = SD.open(CSV_PATH, FILE_APPEND);
  if (!f) return;
  // ISO8601
  struct tm tmv;
  gmtime_r(&ts, &tmv); // UTC; falls Lokalzeit gewünscht, setze TZ vorher
  char iso[24];
  strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tmv);
  f.printf("%s,%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.0f\n",
           iso, (unsigned long)ts,
           t280, h280, p280,
           t680, h680, p680, gas);
  f.flush();
  f.close();
}

// ======== Sensoren ========
void setupSensors() {
  Wire.begin(I2C_SDA, I2C_SCL);

  bme280_ok = bme280.begin(BME280_ADDR);
  bme680_ok = bme680.begin(BME680_ADDR);

  if (bme680_ok) {
    bme680.setTemperatureOversampling(BME680_OS_8X);
    bme680.setHumidityOversampling(BME680_OS_2X);
    bme680.setPressureOversampling(BME680_OS_4X);
    bme680.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme680.setGasHeater(320, 150); // 320°C für 150 ms
  }
}

struct Readout {
  float t280=NAN, h280=NAN, p280=NAN;
  float t680=NAN, h680=NAN, p680=NAN, gas=NAN;
};

Readout readSensors() {
  Readout r;
  if (bme280_ok) {
    r.t280 = bme280.readTemperature();
    r.h280 = bme280.readHumidity();
    r.p280 = bme280.readPressure() / 100.0f;
  }
  if (bme680_ok) {
    if (bme680.performReading()) {
      r.t680 = bme680.temperature;
      r.h680 = bme680.humidity;
      r.p680 = bme680.pressure / 100.0f;
      r.gas  = bme680.gas_resistance; // Ohm
    }
  }
  return r;
}

// ======== Provisioning (AP + Portal) ========
Preferences gPrefs;
WebServer   gProvServer(80);
bool        gProvisioningActive = false;
bool        gNetReady = false;

static void provHandleRoot() {
  String html =
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>"
    "<title>WiFi Setup</title></head><body>"
    "<h2>SmartHome ESP32 - Sender Lukas</h2>"
    "<form method='POST' action='/save'>"
    "SSID:<br/><input name='ssid' maxlength='32'/><br/>"
    "Passwort:<br/><input name='pass' type='password' maxlength='64'/><br/><br/>"
    "<button type='submit'>Speichern & Verbinden</button>"
    "</form>"
    "</body></html>";
  gProvServer.send(200, "text/html", html);
}

static void provStopAP() {
  gProvServer.close();
  WiFi.softAPdisconnect(true);
  gProvisioningActive = false;
}

static bool saveCreds(const String& ssid, const String& pass) {
  gPrefs.begin("wifi", false);
  bool ok = gPrefs.putString("ssid", ssid) > 0 && gPrefs.putString("pass", pass) >= 0;
  gPrefs.end();
  return ok;
}

static bool loadCreds(String& ssid, String& pass) {
  gPrefs.begin("wifi", true);
  ssid = gPrefs.getString("ssid", "");
  pass = gPrefs.getString("pass", "");
  gPrefs.end();
  return ssid.length() > 0;
}

static bool tryConnect(const String& ssid, const String& pass, uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  if (ssid.length() == 0) return false;
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

static void provHandleSave() {
  String ssid = gProvServer.hasArg("ssid") ? gProvServer.arg("ssid") : "";
  String pass = gProvServer.hasArg("pass") ? gProvServer.arg("pass") : "";
  if (ssid.length() == 0) {
    gProvServer.send(400, "text/plain", "SSID fehlt");
    return;
  }
  saveCreds(ssid, pass);
  bool ok = tryConnect(ssid, pass, 15000);
  String msg = ok ? "Verbunden mit " + ssid + ". Beende Setup-AP..." : "Verbindung fehlgeschlagen. Bitte zurück und erneut versuchen.";
  gProvServer.send(ok ? 200 : 500, "text/plain", msg);

  if (ok) {
    provStopAP();
    gNetReady = true; // löst Initialisierung im loop aus
  }
}

static void startProvisionAP() {
  WiFi.mode(WIFI_AP_STA);
  const char* apSsid = "MCR-Setup";
  const char* apPass = "smarthome"; // mind. 8 Zeichen
  bool apUp = WiFi.softAP(apSsid, apPass);
  (void)apUp;

  gProvServer.on("/", HTTP_GET, provHandleRoot);
  gProvServer.on("/save", HTTP_POST, provHandleSave);
  gProvServer.begin();

  gProvisioningActive = true;
  Serial.printf("Provisioning AP aktiv: SSID=%s, IP=%s\n", apSsid, WiFi.softAPIP().toString().c_str());
}

// Ersetzt das alte ensureWiFi(): verbindet aus Speicher oder startet Provisioning
static void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  String ssid, pass;
  if (loadCreds(ssid, pass)) {
    if (tryConnect(ssid, pass, 15000)) {
      gNetReady = true;
      return;
    }
  }
  if (!gProvisioningActive) startProvisionAP();
}

// ======== Setup/Loop ========
void setup() {
  Serial.begin(115200);

  ensureWiFi();     // versucht gespeicherte Zugangsdaten, sonst startet AP-Portal
  // ensureTime();  // wird nach erfolgreicher WLAN-Verbindung im loop aufgerufen
  // ensureCloud(); // dito
  initSD();

  // Lokalen Broker starten & Bridge aktivieren
  broker.subscribe("#", [](const char* topic, const char* payload){
    forwardToCloud(topic, payload);
  });
  broker.begin();

  setupSensors();

  Serial.println("Setup fertig. Öffne ggf. das Provisioning-Portal: http://192.168.4.1/");
}

void loop() {
  // Provisioning-Portal bedienen (nur aktiv, wenn AP läuft)
  if (gProvisioningActive) {
    gProvServer.handleClient();
  }

  // Wenn WLAN nun verbunden ist und Initialisierung noch aussteht
  if (WiFi.status() == WL_CONNECTED && !gNetReady) {
    gNetReady = true;
  }

  // Netzwerkabhängige Initialisierung einmalig nach erfolgreichem Connect
  static bool initializedAfterNet = false;
  if (gNetReady && !initializedAfterNet) {
    ensureTime();
    ensureCloud();
    broker.publish(TOPIC_STATUS, "boot");
    cloud.publish("smarthome/senderlukas/status", "online", true);
    String ip = WiFi.localIP().toString();
    cloud.publish("smarthome/senderlukas/ip", ip.c_str(), true);
    Serial.print("Broker IP: "); Serial.println(WiFi.localIP());
    initializedAfterNet = true;
  }

  // Bestehende Loops
  broker.loop();
  cloud.loop();

  if (WiFi.status() != WL_CONNECTED) {
    // Cloud-Verbindungsaufbau nur mit WLAN, blockiere nicht
    delay(25);
    return;
  }

  if (millis() - lastSend >= SEND_EVERY_MS) {
    lastSend = millis();

    Readout r = readSensors();
    uint32_t ts = (uint32_t)time(nullptr);

    StaticJsonDocument<384> doc;
    doc["ts"]   = ts;
    if (bme280_ok) {
      doc["t280"] = r.t280; doc["rh280"] = r.h280; doc["p280"] = r.p280;
    }
    if (bme680_ok) {
      doc["t680"] = r.t680; doc["rh680"] = r.h680; doc["p680"] = r.p680; doc["gas"] = r.gas;
    }

    char payload[384];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    (void)n;

    broker.publish(TOPIC_ENV, payload);
    forwardToCloud(TOPIC_ENV, payload);
    appendCSV(ts, r.t280, r.h280, r.p280, r.t680, r.h680, r.p680, r.gas);

    Serial.println("Senden");
  }
}
