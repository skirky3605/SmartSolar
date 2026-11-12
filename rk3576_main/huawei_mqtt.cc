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
std::string env(const char* name, const char* fallback="") { const char* v=std::getenv(name); return v?v:fallback; }
std::string hmacHex(const std::string& secret, const std::string& data) {
    unsigned char digest[EVP_MAX_MD_SIZE]; unsigned int len=0;
    HMAC(EVP_sha256(),secret.data(),(int)secret.size(),(const unsigned char*)data.data(),data.size(),digest,&len);
    std::ostringstream out; out<<std::hex<<std::setfill('0');
    for(unsigned int i=0;i<len;++i) out<<std::setw(2)<<(unsigned int)digest[i];
    return out.str();
}
}
HuaweiMqtt::HuaweiMqtt() { mosquitto_lib_init(); }
HuaweiMqtt::~HuaweiMqtt() { if(client_) { mosquitto_disconnect(client_); mosquitto_destroy(client_); } mosquitto_lib_cleanup(); }
bool HuaweiMqtt::connected() const { return connected_; }
bool HuaweiMqtt::connectFromEnvironment() {
    const std::string host=env("HUAWEI_IOT_HOST"), device=env("HUAWEI_DEVICE_ID"), secret=env("HUAWEI_DEVICE_SECRET");
    if(host.empty()||device.empty()||secret.empty()) { std::cerr<<"MQTT disabled: set HUAWEI_IOT_HOST, HUAWEI_DEVICE_ID and HUAWEI_DEVICE_SECRET\n"; return false; }
    const int port=std::atoi(env("HUAWEI_IOT_PORT","8883").c_str());
    const std::string ts=std::to_string((long long)std::time(nullptr));
    const std::string clientId=device+"_0_0_"+ts;
    const std::string password=hmacHex(secret,clientId);
    client_=mosquitto_new(clientId.c_str(),true,nullptr);
    if(!client_) return false;
    mosquitto_username_pw_set(client_,device.c_str(),password.c_str());
    if(port==8883) mosquitto_tls_set(client_,nullptr,nullptr,nullptr,nullptr,nullptr);
    int rc=mosquitto_connect(client_,host.c_str(),port,60);
    if(rc!=MOSQ_ERR_SUCCESS) { std::cerr<<"MQTT connect failed: "<<mosquitto_strerror(rc)<<"\n"; return false; }
    mosquitto_loop_start(client_); connected_=true; return true;
}
bool HuaweiMqtt::publish(const std::string& json) {
    if(!connected_||!client_) return false;
    const std::string topic="$oc/devices/"+env("HUAWEI_DEVICE_ID")+"/sys/properties/report";
    int rc=mosquitto_publish(client_,nullptr,topic.c_str(),(int)json.size(),json.data(),1,false);
    if(rc!=MOSQ_ERR_SUCCESS) std::cerr<<"MQTT publish failed: "<<mosquitto_strerror(rc)<<"\n";
    return rc==MOSQ_ERR_SUCCESS;
}
