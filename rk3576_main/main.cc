#include <opencv2/opencv.hpp>
#include "yolo11.h"
#include "image_utils.h"
#include "file_utils.h"
#include "image_drawing.h"
#include "huawei_mqtt.h"
#include "serial_sensors.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

namespace {

// 判断是否属于动物类别（基于 COCO 80 数据集类别标签）
// 14: bird, 15: cat, 16: dog, 17: horse, 18: sheep, 19: cow, 20: elephant, 21: bear, 22: zebra, 23: giraffe
bool isAnimalClass(int cls_id) {
    return (cls_id >= 14 && cls_id <= 23);
}

std::string jsonEscape(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += (char)c;
        } else if (c < 0x20) {
            char b[7];
            std::snprintf(b, sizeof(b), "\\u%04x", c);
            out += b;
        } else {
            out += (char)c;
        }
    }
    return out + '"';
}

struct AlarmState {
    bool hasAnimal = false;
    int animalCount = 0;
    std::string primaryAnimal;
    float maxConfidence = 0.0f;
    std::string detectionsJson;
};

std::string properties(const SensorData& s, const AlarmState& alarm) {
    // 对应关系:
    // L1: 左上 (light_tl)
    // L2: 右上 (light_tr)
    // L3: 左下 (light_bl)
    // L4: 右下 (light_br)
    const int avg = s.valid ? (s.l1 + s.l2 + s.l3 + s.l4) / 4 : 0;

    std::string weather = std::getenv("WEATHER_VALUE") ? std::getenv("WEATHER_VALUE") : "";
    if (weather.empty()) {
        /* 当光线较暗时（平均光强 < 100 lx），自动判定并上报天气为阴天(cloudy)，否则晴天(clear) */
        weather = (avg < 100) ? "cloudy" : "clear";
    } else if (weather != "clear" && weather != "cloudy" && weather != "rainy" && weather != "unknown") {
        weather = "clear";
    }

    std::ostringstream out;
    out << "{\"services\":[{\"service_id\":\"solar_panel\",\"properties\":{";
    out << "\"light_avg\":" << avg
        << ",\"light_tl\":" << (s.valid ? s.l1 : 0)
        << ",\"light_tr\":" << (s.valid ? s.l2 : 0)
        << ",\"light_bl\":" << (s.valid ? s.l3 : 0)
        << ",\"light_br\":" << (s.valid ? s.l4 : 0)
        << ",\"elevation\":" << (s.valid ? s.elevation : 0.0)
        << ",\"azimuth\":" << (s.valid ? s.azimuth : 0.0)
        << ",\"humidity\":" << (s.valid ? s.humidity : 0.0)
        << ",\"temperature\":" << (s.valid ? s.temperature : 0.0)
        << ",\"weather\":" << jsonEscape(weather)
        << ",\"target_azimuth\":" << (s.valid ? s.azimuth : 0.0)
        << ",\"target_elevation\":" << (s.valid ? s.elevation : 0.0)
        << ",\"tracking\":" << (s.valid && s.tracking ? "true" : "false")
        << ",\"warning\":" << (alarm.hasAnimal ? "true" : "false")
        << ",\"alarm_type\":" << jsonEscape(alarm.hasAnimal ? "animal_intrusion" : "none")
        << ",\"target_animal\":" << jsonEscape(alarm.hasAnimal ? alarm.primaryAnimal : "")
        << ",\"animal_count\":" << alarm.animalCount
        << ",\"alarm_list\":{\"detections\":" << alarm.detectionsJson << "}"
        << "}}]}";
    return out.str();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " <model.rknn> [camera_device] [serial_device]\n";
        return 2;
    }
    const char* model = argv[1];
    const char* cameraPath = argc >= 3 ? argv[2] : "/dev/video25";
    const char* serialPath = argc >= 4 ? argv[3] : "/dev/ttyS10";

    SerialSensors serial(serialPath, 115200);
    if (!serial.isOpen()) {
        std::cerr << "[ERROR] Cannot open serial port: " << serialPath << " (check permissions or device)\n";
    } else {
        std::cout << "[INFO] Successfully opened serial port: " << serialPath << "\n";
    }

    HuaweiMqtt mqtt;
    mqtt.connectFromEnvironment();

    rknn_app_context_t ctx{};
    init_post_process();
    int rc = init_yolo11_model(model, &ctx);
    if (rc) {
        std::cerr << "YOLO model init failed: " << rc << "\n";
        deinit_post_process();
        return 1;
    }

    cv::VideoCapture camera(cameraPath, cv::CAP_V4L2);
    if (!camera.isOpened()) {
        std::cerr << "Cannot open camera: " << cameraPath << "\n";
        release_yolo11_model(&ctx);
        deinit_post_process();
        return 1;
    }
    camera.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('Y', 'U', 'Y', 'V'));
    camera.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    camera.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

    SensorData sensors;
    object_detect_result_list detections{};

    // 上报周期设置：可通过环境变量 HUAWEI_REPORT_MS 配置，默认 200ms (5Hz)
    int reportIntervalMs = 200;
    if (const char* envMs = std::getenv("HUAWEI_REPORT_MS")) {
        int parsed = std::atoi(envMs);
        if (parsed >= 50) reportIntervalMs = parsed;
    }
    std::cout << "[INFO] Huawei IoTDA report interval set to: " << reportIntervalMs << " ms ("
              << (1000.0 / reportIntervalMs) << " Hz)\n";

    // 动物告警时域防抖判定：连续检出达到 consecutiveTarget 帧才确认入侵告警
    int animalConsecutiveFrames = 0;
    const int CONSECUTIVE_ALERT_THRESHOLD = 3; 

    auto lastPublish = std::chrono::steady_clock::now() - std::chrono::milliseconds(reportIntervalMs);
    auto lastTelemetry = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    cv::Mat frame, rgb, annotated, display;
    char label[256];

    while (camera.read(frame) && !frame.empty()) {
        // 读取串口数据
        serial.poll(sensors);

        if (frame.channels() == 3) cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
        else if (frame.channels() == 4) cv::cvtColor(frame, rgb, cv::COLOR_BGRA2RGB);
        else if (frame.channels() == 1) cv::cvtColor(frame, rgb, cv::COLOR_GRAY2RGB);
        else continue;

        if (!rgb.isContinuous()) rgb = rgb.clone();
        annotated = rgb.clone();

        image_buffer_t src{};
        src.width = rgb.cols; src.height = rgb.rows;
        src.width_stride = rgb.cols; src.height_stride = rgb.rows;
        src.format = IMAGE_FORMAT_RGB888;
        src.virt_addr = rgb.data;
        src.size = (int)(rgb.total() * rgb.elemSize());
        src.fd = -1;

        image_buffer_t draw = src;
        draw.virt_addr = annotated.data;
        draw.size = (int)(annotated.total() * annotated.elemSize());

        std::memset(&detections, 0, sizeof(detections));
        if (inference_yolo11_model(&ctx, &src, &detections) != 0) {
            std::cerr << "YOLO inference failed\n";
            break;
        }

        // 筛选小动物目标与提取告警状态
        AlarmState currentAlarm;
        std::ostringstream alarmsJson;
        alarmsJson << "[";
        bool firstAlarm = true;

        for (int i = 0; i < detections.count; ++i) {
            const auto& d = detections.results[i];
            bool isAnimal = isAnimalClass(d.cls_id);

            // 框选绘制：如果是动物用高亮红色框加重标记，其他常规目标蓝色框
            int x = std::max(0, d.box.left);
            int y = std::max(0, d.box.top);
            int w = std::max(1, std::min(rgb.cols - 1, d.box.right) - x);
            int h = std::max(1, std::min(rgb.rows - 1, d.box.bottom) - y);

            if (isAnimal) {
                draw_rectangle(&draw, x, y, w, h, COLOR_RED, 4);
                std::snprintf(label, sizeof(label), "[ALARM] %s %.1f%%", coco_cls_to_name(d.cls_id), d.prop * 100.0f);
                draw_text(&draw, label, x, std::max(0, y - 20), COLOR_RED, 12);

                currentAlarm.animalCount++;
                if (d.prop > currentAlarm.maxConfidence) {
                    currentAlarm.maxConfidence = d.prop;
                    currentAlarm.primaryAnimal = coco_cls_to_name(d.cls_id);
                }

                if (!firstAlarm) alarmsJson << ",";
                firstAlarm = false;
                alarmsJson << "{\"type\":\"animal_intrusion\",\"label\":" << jsonEscape(coco_cls_to_name(d.cls_id))
                           << ",\"confidence\":" << d.prop << ",\"box\":{\"left\":" << d.box.left << ",\"top\":" << d.box.top
                           << ",\"right\":" << d.box.right << ",\"bottom\":" << d.box.bottom << "}}";
            } else {
                draw_rectangle(&draw, x, y, w, h, COLOR_BLUE, 2);
                std::snprintf(label, sizeof(label), "%s %.1f%%", coco_cls_to_name(d.cls_id), d.prop * 100.0f);
                draw_text(&draw, label, x, std::max(0, y - 20), COLOR_BLUE, 10);
            }
        }
        alarmsJson << "]";
        currentAlarm.detectionsJson = alarmsJson.str();

        // 时域平滑防抖滤波
        if (currentAlarm.animalCount > 0) {
            animalConsecutiveFrames++;
        } else {
            if (animalConsecutiveFrames > 0) animalConsecutiveFrames--;
        }
        currentAlarm.hasAnimal = (animalConsecutiveFrames >= CONSECUTIVE_ALERT_THRESHOLD);

        cv::cvtColor(annotated, display, cv::COLOR_RGB2BGR);

        // 1. 目标检测数量与动物告警状态
        std::string statusText = "Objects: " + std::to_string(detections.count);
        if (currentAlarm.hasAnimal) {
            statusText += " [ALERT: ANIMAL INTRUSION - " + currentAlarm.primaryAnimal + " (" + std::to_string(currentAlarm.animalCount) + ")]";
            cv::putText(display, statusText, {10, 25}, cv::FONT_HERSHEY_SIMPLEX, 0.65, {0, 0, 255}, 2);
        } else {
            cv::putText(display, statusText, {10, 25}, cv::FONT_HERSHEY_SIMPLEX, 0.65, {0, 255, 0}, 2);
        }

        // 2. 四路光敏值及均值 (1:左上, 2:右上, 3:左下, 4:右下)
        char lightStr[128];
        const int avg = sensors.valid ? (sensors.l1 + sensors.l2 + sensors.l3 + sensors.l4) / 4 : 0;
        std::snprintf(lightStr, sizeof(lightStr), "Light TL:%d TR:%d BL:%d BR:%d (Avg:%d)",
                      sensors.l1, sensors.l2, sensors.l3, sensors.l4, avg);
        cv::putText(display, lightStr, {10, 50}, cv::FONT_HERSHEY_SIMPLEX, 0.55, {0, 255, 255}, 2);

        // 3. 云台与追踪状态 (CH0:俯仰角, CH1:方位角, TRACK:追光状态)
        char angleStr[128];
        std::snprintf(angleStr, sizeof(angleStr), "Pitch(CH0):%.1f  Yaw(CH1):%.1f  Track:%s",
                      sensors.elevation, sensors.azimuth, (sensors.valid && sensors.tracking) ? "ON" : "OFF");
        cv::putText(display, angleStr, {10, 75}, cv::FONT_HERSHEY_SIMPLEX, 0.55, {255, 180, 0}, 2);

        // 4. 温湿度状态
        char envStr[128];
        std::snprintf(envStr, sizeof(envStr), "Temp:%.1f C  Humi:%.1f %%  Serial:%s",
                      sensors.temperature, sensors.humidity, sensors.valid ? "OK" : "WAIT");
        cv::putText(display, envStr, {10, 100}, cv::FONT_HERSHEY_SIMPLEX, 0.55, sensors.valid ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255), 2);

        if (std::getenv("DISPLAY")) {
            cv::imshow("Solar Vision - YOLO11", display);
            int key = cv::waitKey(1) & 0xff;
            if (key == 27 || key == 'q' || key == 'Q') break;
        }

        auto now = std::chrono::steady_clock::now();
        // 按照配置的频率（默认 200ms / 5Hz）上报华为云 IoTDA
        if (mqtt.connected() && std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPublish).count() >= reportIntervalMs) {
            mqtt.publish(properties(sensors, currentAlarm));
            lastPublish = now;
        }

        // 终端每 2 秒打印一次遥测摘要
        if (now - lastTelemetry >= std::chrono::seconds(2)) {
            std::cout << "[Telemetry] Detections=" << detections.count
                      << " Animals=" << currentAlarm.animalCount
                      << " Alert=" << (currentAlarm.hasAnimal ? "TRIGGERED" : "NORMAL")
                      << " TL=" << sensors.l1 << " TR=" << sensors.l2
                      << " BL=" << sensors.l3 << " BR=" << sensors.l4
                      << " Avg=" << avg
                      << " Pitch=" << sensors.elevation
                      << " Yaw=" << sensors.azimuth
                      << " Track=" << (sensors.tracking ? 1 : 0)
                      << " Serial=" << (sensors.valid ? "VALID" : "WAIT")
                      << " MQTT=" << (mqtt.connected() ? "CONNECTED" : "DISCONNECTED")
                      << std::endl;
            lastTelemetry = now;
        }
    }

    camera.release();
    if (std::getenv("DISPLAY")) {
        cv::destroyAllWindows();
    }
    release_yolo11_model(&ctx);
    deinit_post_process();
    return 0;
}
