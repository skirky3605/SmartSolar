#pragma once
#include <string>

class HuaweiMqtt {
public:
    HuaweiMqtt();
    ~HuaweiMqtt();
    bool connectFromEnvironment();
    bool publish(const std::string& json);
    bool connected() const;
private:
    struct mosquitto* client_ = nullptr;
    bool connected_ = false;
};
