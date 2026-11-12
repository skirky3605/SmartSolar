import os
import sys
import json
import logging
from datetime import datetime
from flask import Flask, jsonify, request
from flask_cors import CORS
import requests

app = Flask(__name__)
CORS(app)

# 华为云与设备参数配置
HUAWEI_IAM_ENDPOINT = os.getenv("HUAWEI_IAM_ENDPOINT", "https://iam.cn-east-3.myhuaweicloud.com")
HUAWEI_IOTDA_ENDPOINT = os.getenv("HUAWEI_IOTDA_ENDPOINT", "https://iotda.cn-east-3.myhuaweicloud.com")
HUAWEI_REGION = os.getenv("HUAWEI_REGION", "cn-east-3")

# 华为云账号凭据（可通过环境变量或直接在此填入测试）
HUAWEI_DOMAIN_NAME = os.getenv("HUAWEI_DOMAIN_NAME", "")       # 主账号名称
HUAWEI_USER_NAME = os.getenv("HUAWEI_USER_NAME", "")           # IAM 用户名（若为主账号可相同）
HUAWEI_PASSWORD = os.getenv("HUAWEI_PASSWORD", "")             # IAM 用户/账号密码
HUAWEI_PROJECT_ID = os.getenv("HUAWEI_PROJECT_ID", "")         # 我的凭证中的华东-上海一项目ID

DEVICE_ID = os.getenv("HUAWEI_DEVICE_ID", "6addd23218855b39c55968de_rk3576")
INSTANCE_ID = os.getenv("HUAWEI_INSTANCE_ID", "634c99984d")
SERVICE_ID = "solar_panel"

cached_token = ""
token_expire_time = 0

def get_huawei_token():
    global cached_token, token_expire_time
    import time
    now = time.time()
    if cached_token and now < token_expire_time - 300:
        return cached_token

    if not HUAWEI_DOMAIN_NAME or not HUAWEI_USER_NAME or not HUAWEI_PASSWORD:
        return None

    url = f"{HUAWEI_IAM_ENDPOINT}/v3/auth/tokens?nocatalog=true"
    payload = {
        "auth": {
            "identity": {
                "methods": ["password"],
                "password": {
                    "user": {
                        "domain": {"name": HUAWEI_DOMAIN_NAME},
                        "name": HUAWEI_USER_NAME,
                        "password": HUAWEI_PASSWORD
                    }
                }
            },
            "scope": {
                "project": {"name": HUAWEI_REGION}
            }
        }
    }
    resp = requests.post(url, json=payload, timeout=10)
    if resp.status_code == 201:
        cached_token = resp.headers.get("X-Auth-Token")
        token_expire_time = now + 24 * 3600
        return cached_token
    else:
        logging.error("Failed to get token: %s %s", resp.status_code, resp.text)
        return None

@app.route('/api/v1/solar-panel/status', methods=['GET'])
def get_solar_panel_status():
    token = get_huawei_token()
    if not token or not HUAWEI_PROJECT_ID:
        # 如果未配置 IAM 账密，返回提示
        return jsonify({
            "error": "Huawei Cloud IAM credentials or PROJECT_ID not configured on backend",
            "device_id": DEVICE_ID,
            "service_id": SERVICE_ID
        }), 503

    # 调用华为云 IoTDA 查询设备影子 API
    url = f"{HUAWEI_IOTDA_ENDPOINT}/v5/iot/{HUAWEI_PROJECT_ID}/devices/{DEVICE_ID}/shadow"
    headers = {
        "X-Auth-Token": token,
        "Instance-Id": INSTANCE_ID
    }
    resp = requests.get(url, headers=headers, timeout=10)
    if resp.status_code != 200:
        return jsonify({"error": "Failed to fetch device shadow from Huawei IoTDA", "detail": resp.text}), resp.status_code

    shadow_data = resp.json()
    # 提取 reported 属性
    services = shadow_data.get("shadow", [])
    target_service = None
    for s in services:
        if s.get("service_id") == SERVICE_ID:
            target_service = s
            break

    reported = target_service.get("reported", {}).get("properties", {}) if target_service else {}
    event_time = target_service.get("reported", {}).get("event_time", datetime.utcnow().isoformat()) if target_service else datetime.utcnow().isoformat()

    # 组装鸿蒙 App 所需的 SolarPanelStatus 数据结构
    result = {
        "device_id": DEVICE_ID,
        "device_name": "rk3576",
        "online": shadow_data.get("status") == "ONLINE",
        "service_id": SERVICE_ID,
        "properties": {
            "light_avg": reported.get("light_avg", 0),
            "light_tl": reported.get("light_tl", 0),
            "light_tr": reported.get("light_tr", 0),
            "light_bl": reported.get("light_bl", 0),
            "light_br": reported.get("light_br", 0),
            "elevation": reported.get("elevation", 0.0),
            "azimuth": reported.get("azimuth", 0.0),
            "target_elevation": reported.get("target_elevation", 0.0),
            "target_azimuth": reported.get("target_azimuth", 0.0),
            "temperature": reported.get("temperature", 0.0),
            "humidity": reported.get("humidity", 0.0),
            "weather": reported.get("weather", "clear"),
            "tracking": reported.get("tracking", False),
            "warning": reported.get("warning", False),
            "alarm_list": reported.get("alarm_list", {"count": 0, "items": []})
        },
        "timestamp": event_time
    }
    return jsonify(result)

if __name__ == '__main__':
    port = int(os.getenv("PORT", 8080))
    print(f"Starting Huawei IoTDA Bridge Backend on port {port}...")
    app.run(host='0.0.0.0', port=port)
