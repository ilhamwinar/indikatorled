import paho.mqtt.client as mqtt
import json
import time
import requests

# --- Konfigurasi RabbitMQ (dengan plugin MQTT aktif) ---
BROKER = "172.17.17.233"     # alamat broker RabbitMQ kamu
PORT = 1883                 # port default MQTT plugin (bukan AMQP)
USERNAME = "aicctv"          # username RabbitMQ
PASSWORD = "@Jmt02022!"          # password RabbitMQ
TOPIC = "parking/config_indicator_ultrasonik"

# --- Buat client MQTT ---
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

# --- Set username & password ---
client.username_pw_set(USERNAME, PASSWORD)

# --- Konfigurasi nodemcu config ---
url_get_active_config = "http://172.17.17.126:8400/get_data_active_nodemcu_ultrasonik"
url_get_data_config = "http://172.17.17.126:8400/get_data_nodemcu_ultrasonik"


# --- Callback ketika berhasil terkoneksi ---
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print("✅ Terhubung ke RabbitMQ MQTT broker!")
    else:
        print(f"❌ Gagal konek, kode: {reason_code}")

# --- Callback saat publish sukses ---
def on_publish(client, userdata, mid, reason_code, properties):
    print("📤 Pesan berhasil dikirim!")

# --- Callback ketika berhasil konek ---
def on_subscribe(client, userdata, flags, rc):
    if rc == 0:
        print("✅ Berhasil terhubung ke broker MQTT!")
        client.subscribe(TOPIC)
        print(f"📡 Berlangganan ke topik: {TOPIC}")
    else:
        print(f"❌ Gagal koneksi, kode error: {rc}")

# --- Callback ketika pesan diterima ---
def on_message(client, userdata, msg):
    print("\n📩 Pesan diterima:")
    print(f"Topik   : {msg.topic}")
    print(f"Payload : {msg.payload.decode('utf-8')}")


# Set callback
client.on_connect = on_connect
client.on_publish = on_publish

# --- Koneksi ke broker ---
client.connect(BROKER, PORT, 60)

# --- Mulai loop background ---
client.loop_start()

# --- Kirim pesan JSON terus menerus setiap 2 detik ---
# try:
while True:
    try:
        # Kirim permintaan GET
        response = requests.get(url_get_active_config, timeout=5)

        # Cek apakah sukses
        if response.status_code == 200:
            print("✅ Berhasil mengambil data dari API")

            # Jika respon JSON
            try:
                data = response.json()
                print("📦 Data JSON:", data)

                for ip in data["ip_address"]:
                    payload = {
                        "ip_address": ip
                    }

                    try:
                        response = requests.post(url_get_data_config, json=payload, timeout=5)
                        print(f"✅ {ip} -> {response.status_code}, {response.text}")

                        result = client.publish(TOPIC, response.text)
                        print(f"📡 Publish ke {TOPIC}: {response.text}")
                        time.sleep(2)

                    except Exception as e:
                        print(f"❌ {ip} -> Gagal: {e}")

            except ValueError:
                print("⚠️ Respon bukan format JSON, ini raw text:")
                print(response.text)

        else:
            print(f"❌ Gagal, kode status: {response.status_code}")

    except requests.exceptions.ConnectionError:
        print("⚠️ Tidak bisa terhubung ke server, periksa IP / jaringan.")
    except requests.exceptions.Timeout:
        print("⌛ Timeout — server tidak merespons.")
    except Exception as e:
        print("⚠️ Terjadi error:", e)

    except KeyboardInterrupt:
        print("\n⛔ Dihentikan oleh user.")
        client.loop_stop()
        client.disconnect()

