#include "serial_sensors.h"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <regex>

SerialSensors::SerialSensors(const std::string& device, int baud) {
    fd_ = open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) return;
    termios tty{};
    if (tcgetattr(fd_, &tty) != 0) { close(fd_); fd_ = -1; return; }
    cfmakeraw(&tty);
    speed_t speed = baud == 115200 ? B115200 : B9600;
    cfsetispeed(&tty, speed); cfsetospeed(&tty, speed);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 0; tty.c_cc[VTIME] = 0;
    tcsetattr(fd_, TCSANOW, &tty);
    const char enableAuto[] = "#AUTO:1\r\n";
    (void)write(fd_, enableAuto, sizeof(enableAuto)-1);
}

SerialSensors::~SerialSensors() {
    if (fd_ >= 0) close(fd_);
}

bool SerialSensors::isOpen() const {
    return fd_ >= 0;
}

bool SerialSensors::parseLine(const std::string& line, SensorData& out) {
    if (line.rfind("$DATA,", 0) != 0) return false;
    auto get = [&](const char* key, double& value) {
        std::regex re(std::string("(?:^|,)") + key + R"(:([-+]?[0-9]*\.?[0-9]+))");
        std::smatch m;
        if (!std::regex_search(line, m, re)) return false;
        value = std::strtod(m[1].str().c_str(), nullptr);
        return true;
    };

    double l1=0, l2=0, l3=0, l4=0, temp=0, humi=0, ch0=0, ch1=0, track=0, dht=0;
    if (!get("L1", l1) || !get("L2", l2) || !get("L3", l3) || !get("L4", l4)) return false;
    
    get("TEMP", temp);
    get("HUMI", humi);
    get("CH0", ch0);
    get("CH1", ch1);
    get("TRACK", track);
    get("DHT", dht);

    out.l1 = (int)l1;
    out.l2 = (int)l2;
    out.l3 = (int)l3;
    out.l4 = (int)l4;
    out.temperature = temp;
    out.humidity = humi;
    out.elevation = ch0;
    out.azimuth = ch1;
    out.tracking = (track != 0.0);
    out.dht = (int)dht;
    out.valid = true;
    return true;
}

bool SerialSensors::poll(SensorData& latest) {
    if (fd_ < 0) return false;
    char buf[512];
    ssize_t n = read(fd_, buf, sizeof(buf));
    if (n > 0) pending_.append(buf, (size_t)n);
    bool updated = false;
    size_t pos;
    while ((pos = pending_.find('\n')) != std::string::npos) {
        std::string line = pending_.substr(0, pos);
        pending_.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        updated = parseLine(line, latest) || updated;
    }
    if (pending_.size() > 4096) pending_.clear();
    return updated;
}

bool SerialSensors::sendPosition(double pitch, double yaw) {
    if (fd_ < 0) return false;
    char command[80];
    int n = std::snprintf(command, sizeof(command), "#POS:%.1f,%.1f\r\n",
        std::clamp(pitch, 0.0, 180.0), std::clamp(yaw, 0.0, 180.0));
    return write(fd_, command, (size_t)n) == n;
}
