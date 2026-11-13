#pragma once
#include <string>

class HuaweiMqtt {
public:
    HuaweiMqtt();
    ~HuaweiMqtt();
    bool connectFromEnvironment();
    bool connect(const std::string& host, const std::string& deviceId, const std::string& secret, int port = 8883);
    bool publish(const std::string& json);
    bool connected() const;
    void setConnected(bool val);
private:
    struct mosquitto* client_ = nullptr;
    bool connected_ = false;
    std::string deviceId_;
};
