import paho.mqtt.client as mqtt
import json

# --- Konfigurasi MQTT Broker ---
BROKER = "172.17.17.233"
PORT = 1883
TOPIC = "parking/config_indicator"
USERNAME = "aicctv"
PASSWORD = "@Jmt02022!"

# --- Callback ketika terhubung ke broker ---
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print("✅ Terhubung ke broker MQTT!")
        client.subscribe(TOPIC)
        print(f"📡 Subscribed ke topik: {TOPIC}")
    else:
        print(f"❌ Gagal konek ke broker. Kode: {reason_code}")

# --- Callback ketika pesan diterima ---
def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode()
        print(f"\n📨 Pesan diterima dari {msg.topic}: {payload}")

        # Kalau payload berupa JSON
        try:
            data = json.loads(payload)
            print("📦 Data JSON:", data)
        except json.JSONDecodeError:
            print("⚠️ Payload bukan JSON valid")

    except Exception as e:
        print("❌ Error parsing pesan:", e)

# --- Inisialisasi client MQTT ---
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

# Set username & password broker
client.username_pw_set(USERNAME, PASSWORD)

# Set callback
client.on_connect = on_connect
client.on_message = on_message

# --- Koneksi ke broker ---
try:
    print("🔌 Menghubungkan ke broker...")
    client.connect(BROKER, PORT, keepalive=60)
    client.loop_forever()
except Exception as e:
    print("❌ Tidak bisa konek ke broker:", e)
