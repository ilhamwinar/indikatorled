// =====================
// Program: MQTT Subscriber ESP8266 (Fixed/Improved)
// Fungsi: Konek ke broker RabbitMQ dan kontrol lampu berdasar pesan dari topic,
//         sekaligus baca sensor ultrasonik dan publish jarak.
// Board:  NodeMCU/ESP-12E (ESP8266)
// =====================

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ====== Konfigurasi WiFi ======
const char* SSID = "JMTO_ITDEV";
const char* PASS = "2508002015";

// ====== Konfigurasi MQTT ======
const char* mqtt_server = "172.17.17.233";   // IP broker RabbitMQ / Mosquitto
const int   mqtt_port   = 1883;
const char* mqtt_user   = "aicctv";
const char* mqtt_pass   = "@Jmt02022!";
const char* MQTT_CLIENT_ID = "parking-1-esp";

// ====== Topic ======
const char* topic_indicator = "parking/config_indicator_ultrasonik"; // config (optional)
const char* topic_control   = "parkir/GF/1/1/cmd";                   // <-- control lampu by MQTT
const char* topic_published = "parkir/GF/1/1";                       // publish jarak/status

// ====== Pin Ultrasonik (HC-SR04) & LED ======
#define TRIG_PIN D5
#define ECHO_PIN D6
#define HIJAU    D2
#define MERAH    D3

// ===== Static IP (ON/OFF) =====
#define USE_STATIC_IP  1   // 1 = pakai static IP, 0 = DHCP
// Contoh: 172.17.17.209 /24, gateway 172.17.17.1
IPAddress STATIC_IP(172, 17, 17, 209);
IPAddress GATEWAY  (172, 17, 17, 1);
IPAddress SUBNET   (255, 255, 255, 0);
IPAddress DNS1     (8, 8, 8, 8);     // boleh pakai DNS internal
IPAddress DNS2     (1, 1, 1, 1);     // optional

// ====== Koneksi MQTT ======
WiFiClient    espClient;
PubSubClient  client(espClient);

// ----- Variabel -----
long  duration = 0;
float distance = 0;

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

// ====== Helpers ======
void setLampuMerah() {
  digitalWrite(MERAH, HIGH);
  digitalWrite(HIJAU, LOW);
}

void setLampuHijau() {
  digitalWrite(MERAH, LOW);
  digitalWrite(HIJAU, HIGH);
}

void setLampuOff() {
  digitalWrite(MERAH, LOW);
  digitalWrite(HIJAU, LOW);
}

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

// =====================
// Callback MQTT
// =====================
void callback(char* topic, byte* payload, unsigned int length) {

  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) { logJsonError(topic, err); return; }

  Serial.println("========== MQTT IN ==========");
  Serial.print("Topic: "); 
  Serial.println(topic);

  if (strcmp(topic, topic_indicator) == 0) {
    // Config payload (opsional)
    indicatorCfg.zone_indicator            = doc["zone"]            | "";
    indicatorCfg.slot_parking_id_indicator = doc["slot_parking_id"] | -1;
    indicatorCfg.block_id_indicator        = doc["block_id"]        | -1;
    indicatorCfg.ip_address                = doc["ip_address"]      | "";
    indicatorCfg.fresh = true;

    Serial.print(" zone=");     Serial.print(indicatorCfg.zone_indicator);
    Serial.print(" slot=");     Serial.print(indicatorCfg.slot_parking_id_indicator);
    Serial.print(" block=");    Serial.print(indicatorCfg.block_id_indicator);
    Serial.print(" ip=");       Serial.println(indicatorCfg.ip_address);
  }

  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.println("=============================");
}

// =====================
// Fungsi Koneksi WiFi
// =====================
void setup_wifi() {
  Serial.begin(115200);
  delay(10);
  Serial.println();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                 // jaga koneksi MQTT lebih stabil
  WiFi.hostname("ESP8266-GF-01");       // ganti sesuai kebutuhan

  if (USE_STATIC_IP) {
    if (!WiFi.config(STATIC_IP, GATEWAY, SUBNET, DNS1, DNS2)) {
      Serial.println("[WiFi] WiFi.config() gagal");
    } else {
      Serial.print("[WiFi] Static IP diset: "); Serial.println(STATIC_IP);
    }
  }

  WiFi.begin(SSID, PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) {
    delay(500); Serial.print(".");
  }
  Serial.println();
  Serial.print("[WiFi] Status : "); Serial.println(WiFi.status());
  printNetInfo();
}

// ====== Reconnect MQTT (non-blocking dengan backoff) ======
unsigned long nextMqttAttemptMs = 0;
const unsigned long MQTT_RETRY_MS = 5000;

// Optional: Last Will (status offline)
const char* willTopic   = "parkir/GF/1/1/status";
const char* willMessage = "offline";

void mqtt_try_connect() {
  if (client.connected()) return;
  unsigned long now = millis();
  if (now < nextMqttAttemptMs) return;

  Serial.print("[MQTT] Menghubungkan ke broker... ");

  // connect(clientID, user, pass, willTopic, willQoS, willRetain, willMessage, cleanSession)
  bool ok = client.connect(
    MQTT_CLIENT_ID,
    mqtt_user,
    mqtt_pass,
    willTopic, 0, true, willMessage, true
  );

  if (ok) {
    Serial.println("TERKONEKSI");
    client.subscribe(topic_indicator);
    // mark online
    client.publish(willTopic, "online", true);
  } else {
    Serial.print("Gagal, rc="); Serial.println(client.state());
    nextMqttAttemptMs = now + MQTT_RETRY_MS;
  }
}

// =====================
// Setup Awal
// =====================
void setup() {
  pinMode(MERAH, OUTPUT);
  pinMode(HIJAU, OUTPUT);
  setLampuOff();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setKeepAlive(60);
  client.setSocketTimeout(5);
  client.setBufferSize(2048);   // kalau payload JSON bisa > 512 byte

  // coba connect pertama kali
  mqtt_try_connect();
}

// ====== Loop timing ======
unsigned long lastMeasureMs = 0;
const unsigned long MEASURE_MS = 1000;

// =====================
// Loop Utama
// =====================
void loop() {
  // Jaga WiFi
  if (WiFi.status() != WL_CONNECTED) {
    // Bisa tambahkan strategi reconnect jika diperlukan
  }

  // Jaga MQTT
  if (!client.connected()) {
    mqtt_try_connect();
  }
  client.loop(); // non-blocking

  // Ukur jarak (non-blocking timing)
  unsigned long now = millis();
  if (now - lastMeasureMs >= MEASURE_MS) {
    lastMeasureMs = now;

    // Kirim pulsa ultrasonik
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    // Baca echo dan hitung jarak (timeout 30ms ≈ ~5m)
    duration = pulseIn(ECHO_PIN, HIGH, 30000UL);
    if (duration == 0) 
    {
      distance = -1;
    } 
    else 
    {
      distance = duration * 0.034f / 2.0f;
    }

    Serial.print("📏 Jarak: ");

    if (distance < 0) 
      {
      Serial.println("no-echo");
      }
    else 
      { 
        Serial.print(distance); Serial.println(" cm"); 
      }

    if (WiFi.localIP().toString() == indicatorCfg.ip_address) {
      Serial.println("==========================================");
      Serial.println("STATE SESUAI IP YANG DIBUTUHKAN");
      Serial.println(indicatorCfg.zone_indicator);
      Serial.println(indicatorCfg.block_id_indicator);
      Serial.println(distance);
      //if (distance )
      int occupied = (distance >= 0 && distance < 20.0); // <20 cm = TERISI

      if (occupied==1) {
        Serial.println(F("MERAH ON | TERISI"));
        digitalWrite(MERAH, LOW);   // RED ON
        digitalWrite(HIJAU, HIGH);    // GREEN OFF
      } else {
        Serial.println(F("HIJAU ON | KOSONG"));
        digitalWrite(MERAH, HIGH);    // RED OFF
        digitalWrite(HIJAU, LOW);   // GREEN ON
      }


       // Build JSON payload
      StaticJsonDocument<256> doc;
      doc["zone_id"]    = indicatorCfg.zone_indicator;
      doc["block_id"]   = indicatorCfg.block_id_indicator;
      doc["state"]      = occupied;

      char buf[256];
      size_t n = serializeJson(doc, buf, sizeof(buf));
      // if (n < sizeof(buf)) buf[n] = '\0';
      //   Serial.println(buf);

      boolean ok = client.publish(
        topic_published,                 // sudah const char*, tidak perlu .c_str()
        (const uint8_t*)buf,             // cast payload ke uint8_t*
        (unsigned int)n,                 // length
        false                            // retained
      );

      Serial.println(ok ? "Published telemetry" : "Publish failed");

        // QoS 1 + retain=false
        // boolean ok = mqtt.publish(TOPIC_PUB, buf, n, false);
        // Serial.println(ok ? "Published telemetry" : "Publish failed");

    }

    //{"zone_id": "GF", "block_id": 1, "state": "111111", "updated_at": "2025-10-10T08:58:14Z"}


    }
  }

