cmake_minimum_required(VERSION 3.16)
project(solar_vision LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TARGET_SOC "${TARGET_SOC}" CACHE STRING "Rockchip target: rk3588, rk3576, rk356x, rv1106, etc.")
if(NOT TARGET_SOC)
    set(TARGET_SOC "rk3576")
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui videoio)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED IMPORTED_TARGET libmosquitto)

# Backend selection based on TARGET_SOC
set(YOLO_BACKEND "${CMAKE_CURRENT_SOURCE_DIR}/rknpu2/yolo11.cc")
if(TARGET_SOC STREQUAL "rk1808" OR TARGET_SOC STREQUAL "rv1109" OR TARGET_SOC STREQUAL "rv1126")
    set(YOLO_BACKEND "${CMAKE_CURRENT_SOURCE_DIR}/rknpu1/yolo11.cc")
    add_compile_definitions(RKNPU1)
elseif(TARGET_SOC STREQUAL "rv1106" OR TARGET_SOC STREQUAL "rv1103")
    set(YOLO_BACKEND "${CMAKE_CURRENT_SOURCE_DIR}/rknpu2/yolo11_rv1106_1103.cc")
    add_compile_definitions(RV1106_1103)
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/allocator/dma")
endif()

# Self-contained 3rdparty and utils inside vision
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/3rdparty" "${CMAKE_CURRENT_BINARY_DIR}/3rdparty")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/utils" "${CMAKE_CURRENT_BINARY_DIR}/utils")

set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")

add_executable(solar_vision
    main.cc
    serial_sensors.cc
    huawei_mqtt.cc
    postprocess.cc
    ${YOLO_BACKEND}
)

target_include_directories(solar_vision PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/utils"
    ${LIBRKNNRT_INCLUDES}
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(solar_vision PRIVATE
    imageutils
    fileutils
    imagedrawing
    ${LIBRKNNRT}
    ${OpenCV_LIBS}
    PkgConfig::MOSQUITTO
    OpenSSL::Crypto
    dl
    pthread
)

install(TARGETS solar_vision RUNTIME DESTINATION bin)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/model/bus.jpg DESTINATION model OPTIONAL)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/model/coco_80_labels_list.txt DESTINATION model OPTIONAL)
file(GLOB RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/model/*.rknn")
install(FILES ${RKNN_FILES} DESTINATION model OPTIONAL)
