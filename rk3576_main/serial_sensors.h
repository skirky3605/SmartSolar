#pragma once
#include <string>

struct SensorData {
    int l1 = 0, l2 = 0, l3 = 0, l4 = 0;
    double temperature = 0, humidity = 0, elevation = 90, azimuth = 90;
    bool valid = false;
};

class SerialSensors {
public:
    explicit SerialSensors(const std::string& device, int baud = 115200);
    ~SerialSensors();
    bool isOpen() const;
    bool poll(SensorData& latest);
    bool sendPosition(double pitch, double yaw);
private:
    int fd_ = -1;
    std::string pending_;
    bool parseLine(const std::string& line, SensorData& out);
};
