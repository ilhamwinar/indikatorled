from fastapi import Depends, FastAPI, HTTPException, status, Form, Request
from fastapi.security import HTTPBasic, HTTPBasicCredentials
import uvicorn
from starlette.middleware import Middleware
from starlette.middleware.cors import CORSMiddleware
import os
import sys
from pathlib import Path
from datetime import datetime
import json
import mysql.connector
import subprocess
import requests
from requests_toolbelt import MultipartEncoder
from requests.exceptions import HTTPError
import base64
import numpy as np
import cv2
import ast
from pydantic import BaseModel
#import datetime

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

## function connect

def connect():
    try:
        cnx = mysql.connector.connect(
            host=host_db,
            user=user_db,
            password=password_db,
            database=database_db
        )
        if cnx.is_connected():
            write_log("Connected to MySQL")
            return cnx

    except Exception as e:
        write_log(f"Error: {e}")
    return None


def nodemcu_config(ip_address):
    query = f"""select zone,slot_parking_id,block_id from nodemcu_config where ip_address='{ip_address}'"""
    print(query)
    print(ip_address)
    try :
        cursor.execute("SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED")
        cursor.execute(query) 
        results = cursor.fetchall()
        return results
    except Exception as e:
        write_log(ip_address, f"Get Config Error {str(e)}")
        write_sql(query)
        return 0

def nodemcu_config_ultrasonik(ip_address):
    query = f"""select zone,slot_parking_id,block_id from nodemcu_config_ultrasonik where ip_address='{ip_address}'"""
    print(query)
    print(ip_address)
    try :
        cursor.execute("SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED")
        cursor.execute(query) 
        results = cursor.fetchall()
        return results
    except Exception as e:
        write_log(ip_address, f"Get Config Error {str(e)}")
        write_sql(query)
        return 0

def nodemcu_config_active():
    query = f"""select ip_address from nodemcu_config where status_active='1'"""
    try :
        cursor.execute("SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED")
        cursor.execute(query) 
        results = cursor.fetchall()
        print(results)
        return results
    except Exception as e:
        write_log(ip_address, f"Get Config Error {str(e)}")
        write_sql(query)
        return 0

def nodemcu_config_ultrasonik_active():
    query = f"""select ip_address from nodemcu_config_ultrasonik where status_active='1'"""
    try :
        cursor.execute("SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED")
        cursor.execute(query) 
        results = cursor.fetchall()
        return results
    except Exception as e:
        write_log(ip_address, f"Get Config Error {str(e)}")
        write_sql(query)
        return 0

def nodemcu_update_waktu_masuk(zone_id,block_id,slot_id,slot_status,waktu_masuk):
    # sql = """
    #     INSERT INTO tbl_zone_sensor
    #         (zone_id, block_id, slot_id, slot_status, waktu_masuk)
    #     VALUES
    #         (%s, %s, %s, %s, %s)
    #     ON DUPLICATE KEY UPDATE
    #         slot_status = VALUES(slot_status),
    #         waktu_masuk = IF(VALUES(slot_status) = 1, VALUES(waktu_masuk), tbl_zone_sensor.waktu_masuk);
    # """

    sql = """
        INSERT INTO tbl_zone_sensor (zone_id, block_id, slot_id, slot_status, waktu_masuk)
        VALUES (%s, %s, %s, %s, %s)
        ON DUPLICATE KEY UPDATE
        waktu_masuk = CASE
            WHEN tbl_zone_sensor.slot_status = 0 AND VALUES(slot_status) = 1
                THEN COALESCE(VALUES(waktu_masuk), NOW())
            ELSE tbl_zone_sensor.waktu_masuk
        END,
        slot_status = VALUES(slot_status);
    """
    params = (zone_id, block_id, slot_id, slot_status, waktu_masuk)

    try:
        cursor.execute("SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED")
        cursor.execute(sql, params)
        cnx.commit()  
        return {"status": 200, "affected": cursor.rowcount, "last_id": cursor.lastrowid}
    except Exception as e:
        # log SQL yang sudah di-param (untuk debugging aman)
        write_log("api", f"Upsert Error: {e}")
        write_sql(f"{sql} | params={params}")
        cnx.rollback()
        return {"status": 500, "error": str(e)}

def nodemcu_update_waktu_keluar(zone_id,block_id,slot_id,slot_status,waktu_keluar):
    print(waktu_keluar)
    # sql = """
    #     INSERT INTO tbl_zone_sensor
    #         (zone_id, block_id, slot_id, slot_status, waktu_keluar)
    #     VALUES
    #         (%s, %s, %s, %s, %s)
    #     ON DUPLICATE KEY UPDATE
    #         slot_status = VALUES(slot_status),
    #         waktu_keluar = IF(VALUES(slot_status) = 0, VALUES(waktu_keluar), tbl_zone_sensor.waktu_keluar);
    # """
    
    sql = """ 
            INSERT INTO tbl_zone_sensor (zone_id, block_id, slot_id, slot_status, waktu_keluar)
        VALUES (%s, %s, %s, %s, %s)
        ON DUPLICATE KEY UPDATE
        waktu_keluar = CASE
            WHEN tbl_zone_sensor.slot_status = 1 AND VALUES(slot_status) = 0
                THEN COALESCE(VALUES(waktu_keluar), NOW())
            ELSE tbl_zone_sensor.waktu_keluar
        END,
        slot_status = VALUES(slot_status);
    """


    params = (zone_id, block_id, slot_id, slot_status, waktu_keluar)

    try:
        cursor.execute("SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED")
        cursor.execute(sql, params)
        cnx.commit()  
        return {"status": 200, "affected": cursor.rowcount, "last_id": cursor.lastrowid}
    except Exception as e:
        # log SQL yang sudah di-param (untuk debugging aman)
        write_log("api", f"Upsert Error: {e}")
        write_sql(f"{sql} | params={params}")
        cnx.rollback()
        return {"status": 500, "error": str(e)}

# def tbl_config_active():
#     query = f"""select cu_address from tbl_zone_sensor where status_active='1'"""
#     try :
#         cursor.execute("SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED")
#         cursor.execute(query) 
#         results = cursor.fetchall()
#         print(results)
#         return results
#     except Exception as e:
#         write_log(ip_address, f"Get Config Error {str(e)}")
#         write_sql(query)
#         return 0


# inisialisasi API
app = FastAPI()
origins = ["*"]
app.add_middleware(
    CORSMiddleware,
    allow_origins=origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
    expose_headers=["*"])

class User(BaseModel):
    ip_address: str

class Insert_User(BaseModel):
    zone_id: str
    block_id:str
    slot_id: str
    flag:str


# INITIALLY CONNECTION
cursor = cnx.cursor()
#cursor_prod = cnx_prod.cursor()

if cnx.is_connected():
    write_log("api","DATABASE CONNECTED TO SERVER")
    cursor = cnx.cursor()
else:
    #write_log("DATABASE NOT CONNEDTED TO SERVER")
    write_log_error("api","DATABASE NOT CONNEDTED TO SERVER")
    exit()

################################################################

@app.get("/get_ping",tags=['API MAIN'])
async def ping():
    return {"status":"200"}

@app.get("/reboot",tags=['API MAIN'])
async def reboot_system():
    subprocess.call(["sudo", "shutdown", "-r", "now"])
    return {"status":200,"message": "System reboot has been scheduled in 5 seconds..."}

#API UNTUK MENJALANKAN SUARA PADA REMOTE PC
@app.post("/get_data_nodemcu",tags=['node_mcu'])
async def get_data_nodemcu(user: User):
    
    ip_address=user.ip_address
    print(ip_address)
    results=nodemcu_config(ip_address)

    try:
        zone, slot_parking_id,block_id = results[0]
        return {"ip_address":ip_address,"zone":zone, "slot_parking_id":slot_parking_id,"block_id":block_id,"status":200}
    except:
        return {"status":404}

#API UNTUK MENJALANKAN SUARA PADA REMOTE PC
@app.post("/get_data_nodemcu_ultrasonik",tags=['node_mcu'])
async def get_data_nodemcu_ultrasonik(user: User):
    
    ip_address=user.ip_address
    print(ip_address)
    results=nodemcu_config_ultrasonik(ip_address)

    try:
        zone, slot_parking_id,block_id = results[0]
        return {"ip_address":ip_address,"zone":zone, "slot_parking_id":slot_parking_id,"block_id":block_id,"status":200}
    except:
        return {"status":404}

#API UNTUK MENJALANKAN SUARA PADA REMOTE PC
@app.get("/get_data_active_nodemcu",tags=['node_mcu'])
async def get_data_active_nodemcu():
    results=nodemcu_config_active()
    ip_list = [row[0] for row in results]
    print(ip_list)
    try:
        ip_address = ip_list
        return {"ip_address":ip_address, "status":200}
    except:
        return {"status":404}

#API UNTUK MENJALANKAN SUARA PADA REMOTE PC
@app.get("/get_data_active_nodemcu_ultrasonik",tags=['node_mcu'])
async def get_data_active_nodemcu_ultrasonik():
    results=nodemcu_config_ultrasonik_active()
    ip_list = [row[0] for row in results]
    print(ip_list)
    try:
        ip_address = ip_list
        return {"ip_address":ip_address, "status":200}
    except:
        return {"status":404}

#API UNTUK MENJALANKAN SUARA PADA REMOTE PC
@app.post("/update_data_nodemcu_parking",tags=['node_mcu'])
async def update_data_nodemcu_parking(user: Insert_User):
    zone_id       = user.zone_id
    block_id      = user.block_id
    slot_id       = user.slot_id
    flag  = user.flag

    if flag == "1":
        waktu_masuk=datetime.now()
        nodemcu_update_waktu_masuk(zone_id,block_id,slot_id,"1",waktu_masuk)
        return {"result":"berhasil masuk", "status":200}
       

    elif flag == "0":
        waktu_keluar=datetime.now()
        nodemcu_update_waktu_keluar(zone_id,block_id,slot_id,"0",waktu_keluar)
        return {"result":"berhasil keluar", "status":200}


if __name__ == '__main__':
    uvicorn.run("api_node:app", host="0.0.0.0", port=8400,log_level="info",reload=True)