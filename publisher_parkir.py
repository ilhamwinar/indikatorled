from datetime import datetime
import json
import mysql.connector
import base64
import paho.mqtt.client as mqtt
import os
import time

#defini direktori    
current_dir = os.getcwd()

#INISIALISASI ENDPOINT
ipendpoint_local="127.0.0.1:8200"
ipendpoint_prod="175.10.1.14:8310"

## FUNGSI UNTUK READ LOG
def write_log(lokasi_log,datalog):
    waktulog = datetime.now()
    dirpathlog = f"Log/{lokasi_log}"
    os.makedirs(dirpathlog, exist_ok=True)
    pathlog = f"{waktulog.strftime('%d%m%Y')}.log"
    file_path = Path(f"{dirpathlog}/{pathlog}")
    datalog = "[INFO] - " + datalog
    if not file_path.is_file():
        file_path.write_text(f"{waktulog.strftime('%d-%m-%Y %H:%M:%S')} - {datalog}\n")
    else :
        fb = open(f"{dirpathlog}/{pathlog}", "a")
        fb.write(f"{waktulog.strftime('%d-%m-%Y %H:%M:%S')} - {datalog}\n")
        fb.close
    
    print(f"{waktulog.strftime('%d-%m-%Y %H:%M:%S')} - {datalog}")

def write_log_error(lokasi_log,datalog):
    waktulog = datetime.now()
    dirpathlog = f"Log/{lokasi_log}"
    os.makedirs(dirpathlog, exist_ok=True)
    pathlog = f"{waktulog.strftime('%d%m%Y')}.log"
    file_path = Path(f"{dirpathlog}/{pathlog}")
    datalog = "[ERROR] - " + datalog
    if not file_path.is_file():
        file_path.write_text(f"{waktulog.strftime('%d-%m-%Y %H:%M:%S')} - {datalog}\n")
    else :
        fb = open(f"{dirpathlog}/{pathlog}", "a")
        fb.write(f"{waktulog.strftime('%d-%m-%Y %H:%M:%S')} - {datalog}\n")
        fb.close
    print(f"{waktulog.strftime('%d-%m-%Y %H:%M:%S')} - {datalog}")

def write_sql(sql):
    waktulog = datetime.now()
    dirpathlog = f"LOG/ITA/DB/SQL/{waktulog.strftime('%Y')}/{waktulog.strftime('%m')}"
    os.makedirs(dirpathlog, exist_ok=True)
    pathlog = f"{waktulog.strftime('%d%m%Y')}.sql"
    file_path = Path(f"{dirpathlog}/{pathlog}")
    if not file_path.is_file():
        file_path.write_text(f"{sql}\n")
    else :
        fb = open(f"{dirpathlog}/{pathlog}", "a")
        fb.write(f"{sql}\n")
        fb.close

#----------------------------------------------------------------------------------------------
user_db = os.getenv("USER_DB", "jmto-db")
password_db = os.getenv("PASSWORD_DB", "Jmt02024!#")
host_db  = os.getenv("HOST_DB", "175.10.1.11")
database_db  = os.getenv("DATABASE", "db_sse")
cur_dir = os.getcwd()

global cnx
global cursor

cnx=mysql.connector.connect(
    user=user_db,
    password=password_db,
    host=host_db,
    database=database_db
    )

import mysql.connector
from mysql.connector import Error

# --- Konfigurasi RabbitMQ (dengan plugin MQTT aktif) ---
BROKER = "172.17.17.233"     # alamat broker RabbitMQ kamu
PORT = 1883                 # port default MQTT plugin (bukan AMQP)
USERNAME = "aicctv"          # username RabbitMQ
PASSWORD = "@Jmt02022!"          # password RabbitMQ
TOPIC = "parkir/zone/detail_ultrasonik"

# --- Buat client MQTT ---
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

# --- Set username & password ---
client.username_pw_set(USERNAME, PASSWORD)

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

def nodemcu_config():
    query = """
        SELECT 
            zone_id,
            block_id,
            GROUP_CONCAT(slot_status ORDER BY slot_id ASC SEPARATOR ',') AS slot_status_list
        FROM tbl_zone_sensor
        GROUP BY zone_id, block_id
        ORDER BY zone_id, block_id;
    """

    cnx = None
    cursor = None
    try:
        cnx = mysql.connector.connect(
            host=host_db,
            user=user_db,
            password=password_db,
            database=database_db
        )
        if not cnx.is_connected():
            print("❌ Gagal konek MySQL")
            return None

        print("✅ Connected to MySQL")
        cursor = cnx.cursor(dictionary=True)

        # set isolation level (opsional)
        cursor.execute("SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED")

        cursor.execute(query)
        results = cursor.fetchall()  # list of dicts: [{'zone_id':..., 'block_id':..., 'slot_status_list':...}, ...]
        print(results)
        for item in results:
            
            state_str = item["slot_status_list"].replace(",", "")

            payload = {
                "zone_id": item["zone_id"],
                "block_id": int(item["block_id"]),
                "state": state_str,   # ← sudah jadi '000000'
                "updated_at": datetime.now().strftime("%Y-%m-%dT%H:%M:%SZ")
            }
            result = client.publish(TOPIC, str(payload) )
            print(f"📡 Publish ke {TOPIC}: {payload }")
            print(payload)
        return results

    except Error as e:
        print(f"MySQL Error: {e}")
        return None
    except Exception as e:
        print(f"Error: {e}")
        return None
    finally:
        if cursor is not None:
            cursor.close()
        if cnx is not None and cnx.is_connected():
            cnx.close()


if __name__ == '__main__':
    while True:
        nodemcu_config()
        time.sleep(2)


