
import paho.mqtt.client as mqtt
import json
import time
import requests

BROKER   = "172.17.17.233"
PORT     = 1883
USERNAME = "aicctv"
PASSWORD = "@Jmt02022!"
URL_API  = "http://172.17.17.126:8400"

TOPIC_CONFIG = "parking/config_indicator_ultrasonik"  # kalau masih perlu

# Endpoint
URL_GET_ACTIVE = f"{URL_API}/get_data_active_nodemcu_ultrasonik"
URL_GET_DATA   = f"{URL_API}/get_data_nodemcu_ultrasonik"
URL_POST_UPDATE= f"{URL_API}/update_data_nodemcu_parking"

# ------- State -------
# Kumpulan topik yang saat ini disubscribe (untuk mencegah duplikat)
subscribed_topics = set()

# Optional: cache terakhir untuk menghindari POST berulang ketika state sama
last_state_cache = {}  # key: (zone, block, slot) -> "0"/"1"

# ------- MQTT callbacks -------
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print("✅ Terhubung ke broker MQTT")
        # Re-subscribe saat reconnect
        if subscribed_topics:
            subs = [(t, 0) for t in subscribed_topics]
            client.subscribe(subs)
            print(f"🔁 Re-subscribe {len(subs)} topik")
    else:
        print(f"❌ Gagal konek, code={reason_code}")

def on_subscribe(client, userdata, mid, granted_qos, properties):
    print(f"📡 Subscribe OK (mid={mid}, qos={granted_qos})")

def on_message(client, userdata, msg):
    # Topik format: parkir/<zone>/<block>/<slot>
    parts = msg.topic.split("/")
    if len(parts) < 4 or parts[0] != "parkir":
        return

    zone, block, slot = parts[1], parts[2], parts[3]

    # Decode payload JSON
    try:
        payload = msg.payload.decode("utf-8", errors="ignore")
        data = json.loads(payload)
    except Exception as e:
        print(f"⚠️ Payload bukan JSON valid di {msg.topic}: {e}")
        return

    # Ambil state (0/1)
    state = data.get("state")
    if state not in (0, 1, "0", "1"):
        # Abaikan pesan yang tidak membawa state
        return

    state_str = "1" if str(state) == "1" else "0"

    # Hindari POST berulang kalau tidak ada perubahan
    cache_key = (str(zone), str(block), str(slot))
    if last_state_cache.get(cache_key) == state_str:
        # Tidak berubah, skip
        return

    json_data = {
        "zone_id": str(zone),
        "block_id": str(block),
        "slot_id": str(slot),
        "flag": state_str
    }

    try:
        r = requests.post(URL_POST_UPDATE, json=json_data, timeout=5)
        print(f"➡️  POST {URL_POST_UPDATE} {json_data} -> {r.status_code}")
        if r.ok:
            last_state_cache[cache_key] = state_str
        else:
            print(f"⚠️ Gagal update: {r.text[:200]}")
    except requests.exceptions.Timeout:
        print("⌛ Timeout saat POST update")
    except Exception as e:
        print(f"❌ Error POST update: {e}")

# ------- Helper: build topic list from API -------
def fetch_topics_from_api():
    """
    Ambil daftar IP aktif -> ambil detail -> kembalikan set topik:
    'parkir/{zone}/{block}/{slot}'
    """
    topics = set()

    try:
        resp = requests.get(URL_GET_ACTIVE, timeout=5)
        if not resp.ok:
            print(f"❌ GET active gagal: {resp.status_code}")
            return topics

        active = resp.json()
        ip_list = active.get("ip_address", [])
    except Exception as e:
        print(f"❌ Error GET active: {e}")
        return topics

    for ip in ip_list:
        try:
            detail = requests.post(URL_GET_DATA, json={"ip_address": ip}, timeout=5)
            if not detail.ok:
                print(f"⚠️ Detail {ip} gagal: {detail.status_code}")
                continue
            parsed = detail.json()

            zone = parsed.get("zone")
            block = parsed.get("block_id")
            slot  = parsed.get("slot_parking_id")

            if zone is None or block is None or slot is None:
                print(f"⚠️ Data tidak lengkap untuk {ip}: {parsed}")
                continue

            t = f"parkir/{zone}/{block}/{slot}"
            topics.add(t)
        except Exception as e:
            print(f"⚠️ Gagal ambil detail {ip}: {e}")

    return topics

# ------- Main -------
def main():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(USERNAME, PASSWORD)
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_subscribe = on_subscribe

    client.connect(BROKER, PORT, 60)
    client.loop_start()

    try:
        # 1) Initial subscribe ke topik spesifik
        topics = fetch_topics_from_api()
        if topics:
            to_sub = sorted(topics - subscribed_topics)
            if to_sub:
                client.subscribe([(t, 0) for t in to_sub])
                subscribed_topics.update(to_sub)
                print(f"✅ Subscribe {len(to_sub)} topik spesifik.")

        # 2) Opsional: tetap dengarkan topik konfigurasi global (jika perlu)
        #    client.subscribe(TOPIC_CONFIG)

        # 3) Periodik re-sync daftar topik (misal tiap 60 detik)
        while True:
            time.sleep(60)
            new_topics = fetch_topics_from_api()
            # Tambah topik baru
            add = sorted(new_topics - subscribed_topics)
            if add:
                client.subscribe([(t, 0) for t in add])
                subscribed_topics.update(add)
                print(f"➕ Tambah subscribe {len(add)} topik baru.")
            # Unsubscribe topik yang tidak aktif lagi
            remove = sorted(subscribed_topics - new_topics)
            for t in remove:
                client.unsubscribe(t)
                subscribed_topics.remove(t)
            if remove:
                print(f"➖ Unsubscribe {len(remove)} topik yang tak aktif.")

    except KeyboardInterrupt:
        print("\n⛔ Dihentikan oleh user.")
    finally:
        client.loop_stop()
        client.disconnect()

if __name__ == "__main__":
    main()

