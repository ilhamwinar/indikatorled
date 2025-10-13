// =====================
// Program: MQTT Subscriber ESP8266
// Fungsi: Konek ke broker RabbitMQ dan kontrol lampu berdasar pesan dari topic
// =====================

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

// ====== Konfigurasi WiFi ======
const char* SSID = "JMTO_ITDEV";
const char* PASS = "2508002015";

// ====== Konfigurasi MQTT ======
const char* mqtt_server = "172.17.17.233";   // IP broker RabbitMQ / Mosquitto
const int   mqtt_port   = 1883;
const char* mqtt_user   = "aicctv";
const char* mqtt_pass   = "@Jmt02022!";

// ====== Topic ======
const char* topic_subscribe = "parkir/zone/detail";

// ====== Kapasitas default (akan dioverride dari API bila ada) ======
const int CAPACITY_PARKING_DEFAULT = 6;

// ====== Pin Output ======
#define HIJAU D2
#define MERAH D3

// ====== Endpoint (HTTP) ======
const char* URL  = "http://172.17.17.126:8400/get_data_nodemcu";

// ====== Static IP (optional) ======
const char* LOCAL_IP_STR = "172.17.17.209";
const char* GATEWAY_STR  = "172.17.17.1";
const char* SUBNET_STR   = "255.255.255.0";
const char* DNS1_STR     = "8.8.8.8";
const char* DNS2_STR     = "8.8.4.4";

// ====== Objek WiFi & MQTT ======
WiFiClient espClient;            // untuk MQTT
PubSubClient client(espClient);  // MQTT client

// ====== Variabel Global ======
String lastMessage = "";   // pesan terakhir dari callback
bool   newMessage  = false;

// Info dari API DATABASE CONFIG
struct ParkingInfo {
  String zone;
  int    slot_id;   // 1-based index slot milik perangkat ini
  int    block_id;  // total kapasitas area
  int    status;    // bila API sediakan status awal
};
ParkingInfo g_info;

int capacity_parking = CAPACITY_PARKING_DEFAULT;

// =====================
// Util
// =====================
IPAddress local_IP, gateway, subnet, dns1, dns2;
bool toIP(IPAddress& out, const char* s) { return out.fromString(s); }

// =====================
// Callback MQTT
// Dipanggil tiap pesan baru datang
// =====================

void callback(char* topic, byte* payload, unsigned int length) {
  // rakit payload mentah jadi String
  String message;
  message.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  message.trim();

  lastMessage = message;
  newMessage  = true;
}


// =====================
// Reconnect ke MQTT
// =====================
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Menghubungkan ke broker MQTT... ");
    String clientId = "ESP8266-" + String(ESP.getChipId(), HEX) + "-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("✅ Terhubung!");
      if (client.subscribe(topic_subscribe)) {
        Serial.printf("📡 Subscribe: %s\n", topic_subscribe);
      } else {
        Serial.println("⚠️  Subscribe gagal.");
      }
    } else {
      Serial.printf("❌ rc=%d | retry 3s...\n", client.state());
      delay(3000);
    }
  }
}

// =====================
// Koneksi WiFi (static IP)
// =====================
void connectWiFi() {
  toIP(local_IP, LOCAL_IP_STR);
  toIP(gateway,  GATEWAY_STR);
  toIP(subnet,   SUBNET_STR);
  toIP(dns1,     DNS1_STR);
  toIP(dns2,     DNS2_STR);

  WiFi.config(local_IP, gateway, subnet, dns1, dns2);

  Serial.printf("Connecting to %s ...\n", SSID);
  WiFi.begin(SSID, PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 30000) {  // 30s timeout
      Serial.println("\n⚠️  WiFi timeout, retry begin()");
      start = millis();
      WiFi.disconnect();
      delay(500);
      WiFi.begin(SSID, PASS);
    }
  }
  Serial.printf("\nWiFi OK. IP: %s\n", WiFi.localIP().toString().c_str());
}

// =====================
// POST JSON ke server -> isi ParkingInfo
// body request: {"ip_address":"<local_IP>"}
// expected response: JSON berisi zone, slot_parking_id, capacity_parking, status
// =====================
bool postJson(ParkingInfo &out) {
  WiFiClient httpNet;
  HTTPClient http;

  if (!http.begin(httpNet, URL)) {
    Serial.println("❌ http.begin gagal");
    return false;
  }

  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("Content-Type", "application/json");

  String payload = String("{\"ip_address\":\"") + local_IP.toString() + "\"}";
  //Serial.printf("POST -> %s\n", URL);
  Serial.print("Payload: ");
  Serial.println(payload);

  int code = http.POST(payload);
  Serial.printf("HTTP code: %d (%s)\n", code, http.errorToString(code).c_str());

  if (code <= 0) {
    http.end();
    return false;
  }

  String resp = http.getString();
  //Serial.printf("Response (%d bytes):\n%s\n", resp.length(), resp.c_str());
  http.end();

  if (resp.length() == 0) return false;

  // Sesuaikan kapasitas dokumen dengan ukuran JSON kamu
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, resp);
  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }

  // Aman baca dengan fallback
  out.zone     = (const char*)(doc["zone"] | "");
  out.slot_id  = doc["slot_parking_id"]   | -1;
  out.block_id = doc["block_id"]  | -1;
  out.status   = doc["status"]            | -1;

  // Serial.printf("Parsed -> zone=%s, slot_id=%d, block_id=%d, status=%d\n",
  //               out.zone.c_str(), out.slot_id, out.block_id, out.status);

  return true;
}

// =====================
// Setup
// =====================
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== MQTT Subscriber Start ===");

  pinMode(MERAH, OUTPUT);
  pinMode(HIJAU, OUTPUT);
  digitalWrite(MERAH, LOW);
  digitalWrite(HIJAU, LOW);

  connectWiFi();

  // MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);


}

// =====================
// Loop Utama
// =====================
void loop() {
  Serial.println("-----------------");
  if (!client.connected()) {
    reconnectMQTT();
    digitalWrite(MERAH, LOW);
    digitalWrite(HIJAU, LOW);
    Serial.println("Lampu: OFF MQTT OFF");
  }
  else{
    Serial.println("MQTT ON");
  }

  client.loop(); // jaga koneksi MQTT

  if (newMessage) {

    // GET DATA JSON MQTT
    StaticJsonDocument<256> doc;               // kapasitas cukup untuk JSON kecil ini

    DeserializationError err = deserializeJson(doc, lastMessage);
    if (err) {
      Serial.printf("Parse error: %s\n", err.c_str());
      return;
    }

    // Ambil field
    const char* zone_id  = doc["zone_id"] | "";    // "GF"
    int         block_id = doc["block_id"] | -1;   // 1
    const char* state    = doc["state"]   | "";    // "111111"
    const char* updated  = doc["updated_at"] | ""; // "2025-10-13T02:17:35Z"

  
  // KALAU ADA HTTP
  if (postJson(g_info)) {

    if (zone_id && strcmp(zone_id, g_info.zone.c_str()) == 0) {
      Serial.println("BENAR SESUAI LANTAI");
      Serial.print("MQTT -> ");
      Serial.println(lastMessage);
      Serial.print("API -> ");
      Serial.printf("[INFO] zone=%s, slot_id=%d, block_id=%d, status=%d\n",g_info.zone.c_str(), g_info.slot_id, g_info.block_id, g_info.status);


      if (block_id > 0 && block_id == g_info.block_id) {
        Serial.println("MASUK SESUAI BENAR ID BLOCK");
        Serial.printf("state=%s\n", state);

        //int block_id = block_id;    // contoh: 3 berarti ambil indeks 2
        int len = strlen(state);

        String st = state;
        char buf[16];
        st.toCharArray(buf, sizeof(buf));
        Serial.printf("state_array=%s\n", st);

        if (g_info.slot_id >= 0 && g_info.slot_id < st.length()) {
            char c2 = st.charAt(g_info.slot_id-1);
            Serial.printf("INDEXNYA %d\n", g_info.slot_id-1);
            Serial.printf("COBA HASIL AKHIRNYA GINI %c\n", c2);

            if (c2 == '1') {
              // terisi, MERAH ON
              Serial.println("MERAH ON | TERISI");
              digitalWrite(MERAH, HIGH);
              digitalWrite(HIJAU, LOW);
            } 
            else
            {
              // kosong, HIJAU ON
              Serial.println("HIJAUN ON | KOSONG");
              digitalWrite(MERAH, LOW);
              digitalWrite(HIJAU, HIGH);
            }
        }
        else{
          Serial.println("⚠️ block_id di luar jangkauan state");
          digitalWrite(MERAH, LOW);
          digitalWrite(HIJAU, LOW);
        }
        
      }
    }
      newMessage = false;
    }
  delay(2000);
}
}



