// =====================
// Program: MQTT Subscriber ESP8266 (1 Broker, 2 Topic)
// Fungsi: Subscribe 2 topic & parsing JSON di callback (stabil, non-blocking)
// =====================

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ====== Pin Output ======
#define HIJAU D2
#define MERAH D3

// ====== Konfigurasi WiFi ======
const char* SSID = "JMTO_ITDEV";
const char* PASS = "2508002015";

// ===== Static IP (ON/OFF) =====
#define USE_STATIC_IP  1   // 1 = pakai static IP, 0 = DHCP
// Contoh: 172.17.17.209 /24, gateway 172.17.17.1
IPAddress STATIC_IP(172, 17, 17, 209);
IPAddress GATEWAY  (172, 17, 17, 1);
IPAddress SUBNET   (255, 255, 255, 0);
IPAddress DNS1     (8, 8, 8, 8);     // boleh pakai DNS internal
IPAddress DNS2     (1, 1, 1, 1);     // optional

// ====== Konfigurasi MQTT ======
const char* mqtt_server = "172.17.17.233";
const int   mqtt_port   = 1883;
const char* mqtt_user   = "aicctv";
const char* mqtt_pass   = "@Jmt02022!";
const char* MQTT_CLIENT_ID = "parking-1-esp";

// ====== Topics ======
const char* topic_zone_detail = "parkir/zone/detail";
const char* topic_indicator   = "parking/config_indicator";

// ====== Koneksi MQTT ======
WiFiClient espClient;
PubSubClient client(espClient);

// ====== Struktur data hasil parse ======
struct ZoneDetail {
  String zone_id_parkir;
  int    block_id_parkir = -1;
  String state_parkir;         // contoh "111111"
  String updated_at;           // contoh "2025-10-16T06:39:32Z"
  bool   fresh   = false;      // true jika baru diterima
} zoneDetail;

struct IndicatorCfg {
  String zone_indicator;
  int    slot_parking_id_indicator = -1;
  int    block_id_indicator        = -1;
  String ip_address;
  bool   fresh          = false; // true jika baru diterima
} indicatorCfg;

// ====== Utils ======
void logJsonError(const char* where, DeserializationError e) {
  Serial.print("[JSON] Gagal parsing di ");
  Serial.print(where);
  Serial.print(": ");
  Serial.println(e.c_str());
}

void printNetInfo() {
  Serial.print("[WiFi] IP   : "); Serial.println(WiFi.localIP());
  Serial.print("[WiFi] GW   : "); Serial.println(WiFi.gatewayIP());
  Serial.print("[WiFi] MASK : "); Serial.println(WiFi.subnetMask());
  Serial.print("[WiFi] DNS1 : "); Serial.println(WiFi.dnsIP(0));
  Serial.print("[WiFi] DNS2 : "); Serial.println(WiFi.dnsIP(1));
  Serial.print("[WiFi] RSSI : "); Serial.println(WiFi.RSSI());
}

// ====== Callback MQTT ======
void callback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<768> doc; // sedikit lebih besar
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) { logJsonError(topic, err); return; }

  Serial.println("==================================");
  // Serial.print("Pesan diterima dari topic: ");
  // Serial.println(topic);
  // Serial.print("Raw: "); serializeJson(doc, Serial); Serial.println();

  if (strcmp(topic, topic_zone_detail) == 0) {
    zoneDetail.zone_id_parkir = doc["zone_id"]   | "";
    zoneDetail.block_id_parkir= doc["block_id"]  | -1;
    zoneDetail.state_parkir   = doc["state"]     | "";
    zoneDetail.updated_at     = doc["updated_at"]| "";
    zoneDetail.fresh = true;

    // Serial.println("=== parkir/zone/detail ===");
    // Serial.print("zone_id    : "); Serial.println(zoneDetail.zone_id_parkir);
    // Serial.print("block_id   : "); Serial.println(zoneDetail.block_id_parkir);
    // Serial.print("state      : "); Serial.println(zoneDetail.state_parkir);
    // Serial.print("updated_at : "); Serial.println(zoneDetail.updated_at);
  }
  else if (strcmp(topic, topic_indicator) == 0) {
    indicatorCfg.zone_indicator            = doc["zone"]            | "";
    indicatorCfg.slot_parking_id_indicator = doc["slot_parking_id"] | -1;
    indicatorCfg.block_id_indicator        = doc["block_id"]        | -1;
    indicatorCfg.ip_address                = doc["ip_address"]      | "";
    indicatorCfg.fresh = true;

    // Serial.println("=== parking/config_indicator ===");
    // Serial.print("zone   : "); Serial.println(indicatorCfg.zone_indicator);
    // Serial.print("slot   : "); Serial.println(indicatorCfg.slot_parking_id_indicator);
    // Serial.print("block  : "); Serial.println(indicatorCfg.block_id_indicator);
    // Serial.print("ip     : "); Serial.println(indicatorCfg.ip_address);
  }
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.println("==================================");
}

// ====== WiFi ======
void setup_wifi() {
  Serial.begin(115200);
  delay(10);
  Serial.println();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                 // jaga koneksi MQTT lebih stabil
  WiFi.hostname("ESP8266-GF-01");       // ganti sesuai kebutuhan

  // WiFi.setSleep(false);

  // 1) Set static IP —> harus sebelum begin()
  if (!WiFi.config(STATIC_IP, GATEWAY, SUBNET, DNS1, DNS2)) {
    Serial.println("[WiFi] WiFi.config() gagal");
  } else {
    Serial.print("[WiFi] Static IP diset: "); Serial.println(STATIC_IP);
  }

  // 2) Connect
  WiFi.begin(SSID, PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-t0 < 30000) {
    delay(500); Serial.print(".");
  }
  Serial.println();
  Serial.print("[WiFi] Status : "); Serial.println(WiFi.status());
  Serial.print("[WiFi] IP     : "); Serial.println(WiFi.localIP());
  Serial.print("[WiFi] GW     : "); Serial.println(WiFi.gatewayIP());
  Serial.print("[WiFi] MASK   : "); Serial.println(WiFi.subnetMask());
}

// ====== Reconnect MQTT (non-blocking dengan backoff) ======
unsigned long nextMqttAttemptMs = 0;
const unsigned long MQTT_RETRY_MS = 5000;

void mqtt_try_connect() {
  if (client.connected()) return;
  unsigned long now = millis();
  if (now < nextMqttAttemptMs) return;

  Serial.print("[MQTT] Menghubungkan ke broker...");
  String clientId = String("ESP8266Client_DualTopic-") + String(ESP.getChipId(), HEX);

  // Last Will (opsional)
  // const char* willTopic = "device/lastwill";
  // const char* willMsg   = "offline";
  // bool ok = client.connect(clientId.c_str(), mqtt_user, mqtt_pass, willTopic, 0, true, willMsg);

  bool ok = client.connect(MQTT_CLIENT_ID, mqtt_user, mqtt_pass, NULL, 0, false, NULL, false);
  if (ok) {
    Serial.println("TERKONEKSI");
    client.subscribe(topic_zone_detail);
    client.subscribe(topic_indicator);
    // client.publish(willTopic, "online", true);
  } else {
    Serial.print("Gagal, rc="); Serial.println(client.state());
    nextMqttAttemptMs = now + MQTT_RETRY_MS;
  }
}

// ====== Setup ======
void setup() {
  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setKeepAlive(60);
  client.setSocketTimeout(5);
  client.setBufferSize(2048);   // kalau payload JSON bisa > 512 byte

  // coba connect pertama kali
  mqtt_try_connect();
  
  pinMode(MERAH, OUTPUT);
  pinMode(HIJAU, OUTPUT);
  digitalWrite(MERAH, LOW);
  digitalWrite(HIJAU, LOW);
}

// ====== Loop ======
unsigned long lastProcessMs = 0;

void loop() {
  // jaga WiFi
  Serial.print("WiFi RSSI: ");
  Serial.println(WiFi.RSSI());
  if (WiFi.status() != WL_CONNECTED) {
    // opsional: bisa trigger reconnect WiFi di sini jika butuh
  }

  // jaga MQTT
  if (!client.connected()) {
    mqtt_try_connect();
  }
  client.loop(); // non-blocking

  // Proses gabungan tiap ~1 detik saat data baru tersedia
  unsigned long now = millis();
  if (now - lastProcessMs >= 1000) {
    lastProcessMs = now;

    if (zoneDetail.fresh && indicatorCfg.fresh) {
      Serial.println("\n[PROCESS] Membandingkan zone dan blok dari kedua topic...");

    if (WiFi.localIP().toString() == indicatorCfg.ip_address) {

      Serial.println("INI STATE LOGIC zone parkir dan indicator");
      Serial.println(zoneDetail.zone_id_parkir);
      Serial.println(indicatorCfg.zone_indicator);
      Serial.println(zoneDetail.block_id_parkir);
      Serial.println(indicatorCfg.block_id_indicator);

      if (zoneDetail.zone_id_parkir == indicatorCfg.zone_indicator &&
          zoneDetail.block_id_parkir == indicatorCfg.block_id_indicator) {

        Serial.print("Zone dan block sama: ");
        Serial.println(zoneDetail.zone_id_parkir);
        Serial.println(zoneDetail.state_parkir);

        // Pastikan index valid sebelum ambil char
        if (indicatorCfg.slot_parking_id_indicator > 0 &&
            indicatorCfg.slot_parking_id_indicator <= (int)zoneDetail.state_parkir.length()) {
          char hasil_logic = zoneDetail.state_parkir[indicatorCfg.slot_parking_id_indicator - 1];
          Serial.println(hasil_logic);

          if (hasil_logic == '1') {
            // terisi, MERAH ON
            Serial.println("MERAH ON | TERISI");
            digitalWrite(MERAH, LOW);
            digitalWrite(HIJAU, HIGH);
          } 
          else
          {
            // kosong, HIJAU ON
            Serial.println("HIJAUN ON | KOSONG");
            digitalWrite(MERAH, HIGH);
            digitalWrite(HIJAU, LOW);
          }
        }

        } else {
          Serial.println("Index slot_parking_id_indicator di luar range state_parkir");
        }

      } else {
        Serial.println("Zone berbeda! detail lewatkan");
        // Serial.print(zoneDetail.zone_id_parkir);
        // Serial.print(" vs indicator=");
        // Serial.println(indicatorCfg.zone_indicator);
      }

      zoneDetail.fresh = false;
      indicatorCfg.fresh = false;
    }


    // contoh akses bit state: cek karakter ke-0 s/d n
    if (zoneDetail.fresh == false && zoneDetail.state_parkir.length() > 0) {
      // bool bit0 = (zoneDetail.state_parkir[0] == '1');
      // lakukan sesuatu jika perlu...
    }
    delay(1000);
  }
}

