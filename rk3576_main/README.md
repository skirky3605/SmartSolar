# Solar Vision

C++ application combining the repository's YOLO11/RKNN camera inference with a four sensor solar tracker, UART telemetry, and Huawei Cloud IoTDA MQTT reporting.

## Build

Build on the Rockchip device (or a matching SDK/sysroot), with CMake, OpenCV, `libmosquitto-dev`, OpenSSL, and the RKNN runtime available:

```sh
cmake -S vision -B vision/build -DTARGET_SOC=rk3588
cmake --build vision/build -j
```

For another supported board, set `TARGET_SOC` accordingly. The build reuses `rknn_model_zoo/examples/yolo11/cpp` and its sibling repository libraries. Supply a compatible `.rknn` model and camera device:

```sh
export HUAWEI_IOT_HOST='your IoTDA MQTT endpoint'
export HUAWEI_DEVICE_ID='your registered device id'
export HUAWEI_DEVICE_SECRET='your device secret'
# Optional: HUAWEI_IOT_PORT (default 8883), WEATHER_VALUE (default unknown)
vision/build/solar_vision vision/model/yolo11.rknn /dev/video25 /dev/ttyS10
```

MQTT uses TLS on port 8883 and publishes to `$oc/devices/{device_id}/sys/properties/report`, with service ID `solar`. Configure the IoTDA product's service ID to `solar`, and configure the device-secret authentication method to match the product. The app publishes all listed properties once per second. Missing MQTT settings disable cloud reporting while local inference and serial control continue.

## UART protocol

UART is configured as 115200 8N1. It accepts `$DATA,L1:...,L2:...,L3:...,L4:...,TEMP:...,HUMI:...,CH0:...,CH1:...` lines. `L1..L4` map to top-left, top-right, bottom-left, bottom-right; `CH0/CH1` map to elevation/azimuth. The mean of four light sensors maps to `light_avg`. A proportional balance controller sends `#POS:<pitch>,<yaw>` at most twice per second when sensor imbalance exceeds its dead band. `TEMP` and `HUMI` are passed through as reported.

YOLO detections become `alarm_list` entries (`type`, `label`, `confidence`, bounding box); `warning` is true while the current inference contains any detection. Weather enum values are product-specific, so set `WEATHER_VALUE` to one allowed by the IoTDA product model. Tune the controller gain/dead band for the sensor and mechanism before unattended operation.
