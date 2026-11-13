#include "huawei_mqtt.h"
#include <mosquitto.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
std::string env(const char* name, const char* fallback="") {
    const char* v = std::getenv(name);
    return v ? v : fallback;
}

std::string getUtcTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm gmt{};
    gmtime_r(&now, &gmt);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d%H", &gmt);
    return std::string(buf);
}

std::string hmacHex(const std::string& key, const std::string& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.data(), (int)key.size(),
         (const unsigned char*)data.data(), data.size(), digest, &len);
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < len; ++i) {
        out << std::setw(2) << (unsigned int)digest[i];
    }
    return out.str();
}

void onConnectCb(struct mosquitto* mosq, void* userdata, int rc) {
    auto* self = static_cast<HuaweiMqtt*>(userdata);
    if (rc == 0) {
        std::cout << "[Huawei MQTT] Successfully connected to Huawei Cloud IoTDA!\n";
        if (self) self->setConnected(true);
    } else {
        std::cerr << "[Huawei MQTT] Connect callback error: rc=" << rc << " (" << mosquitto_connack_string(rc) << ")\n";
        if (self) self->setConnected(false);
    }
}

void onDisconnectCb(struct mosquitto* mosq, void* userdata, int rc) {
    auto* self = static_cast<HuaweiMqtt*>(userdata);
    if (self) self->setConnected(false);
    if (rc != 0) {
        std::cerr << "[Huawei MQTT] Unexpected disconnect: " << rc << "\n";
    }
}
}

HuaweiMqtt::HuaweiMqtt() {
    mosquitto_lib_init();
}

HuaweiMqtt::~HuaweiMqtt() {
    if (client_) {
        mosquitto_disconnect(client_);
        mosquitto_loop_stop(client_, false);
        mosquitto_destroy(client_);
    }
    mosquitto_lib_cleanup();
}

bool HuaweiMqtt::connected() const {
    return connected_;
}

void HuaweiMqtt::setConnected(bool val) {
    connected_ = val;
}

bool HuaweiMqtt::connect(const std::string& host, const std::string& deviceId, const std::string& secret, int port) {
    if (host.empty() || deviceId.empty() || secret.empty()) {
        std::cerr << "[Huawei MQTT] Error: host, device_id, and secret must not be empty.\n";
        return false;
    }
    deviceId_ = deviceId;

    // 华为云 IoTDA 标准 HMACSHA256 鉴权算法：
    // key = 时间戳 (YYYYMMDDHH UTC时间)
    // message = secret (设备密钥)
    // clientId = deviceId_0_0_时间戳
    // password = HMACSHA256(key=timestamp, message=secret)
    const std::string ts = getUtcTimestamp();
    const std::string clientId = deviceId + "_0_0_" + ts;
    const std::string password = hmacHex(ts, secret);

    std::cout << "[Huawei MQTT] Connecting to " << host << ":" << port << " ...\n";
    std::cout << "[Huawei MQTT] Device ID: " << deviceId << "\n";
    std::cout << "[Huawei MQTT] Client ID: " << clientId << "\n";

    client_ = mosquitto_new(clientId.c_str(), true, this);
    if (!client_) {
        std::cerr << "[Huawei MQTT] Failed to create mosquitto instance.\n";
        return false;
    }

    mosquitto_connect_callback_set(client_, onConnectCb);
    mosquitto_disconnect_callback_set(client_, onDisconnectCb);

    mosquitto_username_pw_set(client_, deviceId.c_str(), password.c_str());

    if (port == 8883) {
        const char* cafile = "/etc/ssl/certs/ca-certificates.crt";
        int tls_rc = mosquitto_tls_set(client_, cafile, nullptr, nullptr, nullptr, nullptr);
        if (tls_rc != MOSQ_ERR_SUCCESS) {
            std::cerr << "[Huawei MQTT] TLS set warning: " << mosquitto_strerror(tls_rc) << "\n";
        }
        mosquitto_tls_insecure_set(client_, true);
    }

    int rc = mosquitto_connect(client_, host.c_str(), port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[Huawei MQTT] Connect failed: " << mosquitto_strerror(rc) << "\n";
        return false;
    }

    mosquitto_loop_start(client_);
    return true;
}

bool HuaweiMqtt::connectFromEnvironment() {
    // 默认接入地址为华东-上海一 (cn-east-3) 设备接入实例
    const std::string defaultHost = "634c99984d.st1.iotda-device.cn-east-3.myhuaweicloud.com";
    const std::string defaultDevice = "6addd23218855b39c55968de_rk3576";
    const std::string defaultSecret = "NsXTDdprx7qDKGX";

    std::string host = env("HUAWEI_IOT_HOST", defaultHost.c_str());
    std::string device = env("HUAWEI_DEVICE_ID", defaultDevice.c_str());
    std::string secret = env("HUAWEI_DEVICE_SECRET", defaultSecret.c_str());
    int port = std::atoi(env("HUAWEI_IOT_PORT", "8883").c_str());

    return connect(host, device, secret, port);
}

bool HuaweiMqtt::publish(const std::string& json) {
    if (!connected_ || !client_) return false;
    const std::string topic = "$oc/devices/" + deviceId_ + "/sys/properties/report";
    int rc = mosquitto_publish(client_, nullptr, topic.c_str(), (int)json.size(), json.data(), 1, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[Huawei MQTT] Publish failed: " << mosquitto_strerror(rc) << "\n";
    }
    return rc == MOSQ_ERR_SUCCESS;
}
