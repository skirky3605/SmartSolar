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

namespace {
std::string jsonEscape(const std::string& s) {
    std::string out="\"";
    for(unsigned char c:s) { if(c=='"'||c=='\\') { out+='\\'; out+=(char)c; }
        else if(c<0x20) { char b[7]; std::snprintf(b,sizeof(b),"\\u%04x",c); out+=b; }
        else out+=(char)c; }
    return out+'"';
}
std::string properties(const SensorData& s, const object_detect_result_list& detections, bool tracking) {
    const int avg=s.valid?(s.l1+s.l2+s.l3+s.l4)/4:0;
    std::ostringstream alarms; alarms<<"[";
    for(int i=0;i<detections.count;++i) {
        const auto& d=detections.results[i]; if(i) alarms<<",";
        alarms<<"{\"type\":\"vision_detection\",\"label\":"<<jsonEscape(coco_cls_to_name(d.cls_id))
              <<",\"confidence\":"<<d.prop<<",\"box\":{\"left\":"<<d.box.left<<",\"top\":"<<d.box.top
              <<",\"right\":"<<d.box.right<<",\"bottom\":"<<d.box.bottom<<"}}";
    }
    alarms<<"]";
    std::ostringstream out; out<<"{\"services\":[{\"service_id\":\"solar\",\"properties\":{";
    out<<"\"light_avg\":"<<avg<<",\"light_tl\":"<<(s.valid?s.l1:0)<<",\"light_tr\":"<<(s.valid?s.l2:0)
       <<",\"light_bl\":"<<(s.valid?s.l3:0)<<",\"light_br\":"<<(s.valid?s.l4:0)
       <<",\"elevation\":"<<s.elevation<<",\"azimuth\":"<<s.azimuth
       <<",\"humidity\":"<<s.humidity<<",\"temperature\":"<<s.temperature
       <<",\"weather\":"<<jsonEscape(std::getenv("WEATHER_VALUE")?std::getenv("WEATHER_VALUE"):"unknown")
       <<",\"target_azimuth\":"<<s.azimuth<<",\"target_elevation\":"<<s.elevation
       <<",\"tracking\":"<<(tracking?"true":"false")<<",\"warning\":"<<(detections.count>0?"true":"false")
       <<",\"alarm_list\":{\"detections\":"<<alarms.str()<<"}}]}";
    return out.str();
}
}

int main(int argc,char** argv) {
    if(argc<2||argc>4) { std::cerr<<"Usage: "<<argv[0]<<" <model.rknn> [camera_device] [serial_device]\n"; return 2; }
    const char* model=argv[1]; const char* cameraPath=argc>=3?argv[2]:"/dev/video25";
    const char* serialPath=argc>=4?argv[3]:"/dev/ttyS10";
    SerialSensors serial(serialPath,115200);
    if(!serial.isOpen()) std::cerr<<"Warning: cannot open serial port "<<serialPath<<"\n";
    HuaweiMqtt mqtt; mqtt.connectFromEnvironment();
    rknn_app_context_t ctx{}; init_post_process();
    int rc=init_yolo11_model(model,&ctx); if(rc) { std::cerr<<"YOLO model init failed: "<<rc<<"\n"; deinit_post_process(); return 1; }
    cv::VideoCapture camera(cameraPath,cv::CAP_V4L2);
    if(!camera.isOpened()) { std::cerr<<"Cannot open camera: "<<cameraPath<<"\n"; release_yolo11_model(&ctx); deinit_post_process(); return 1; }
    camera.set(cv::CAP_PROP_FOURCC,cv::VideoWriter::fourcc('Y','U','Y','V'));
    camera.set(cv::CAP_PROP_FRAME_WIDTH,640); camera.set(cv::CAP_PROP_FRAME_HEIGHT,480);
    SensorData sensors; object_detect_result_list detections{};
    auto lastPublish=std::chrono::steady_clock::now()-std::chrono::seconds(2);
    auto lastServo=lastPublish; auto lastTelemetry=lastPublish;
    cv::Mat frame,rgb,annotated,display; char label[256];
    while(camera.read(frame)&&!frame.empty()) {
        serial.poll(sensors);
        if(sensors.valid && std::chrono::steady_clock::now()-lastServo>std::chrono::milliseconds(500)) {
            const double horizontal=((sensors.l1+sensors.l3)-(sensors.l2+sensors.l4))/2.0;
            const double vertical=((sensors.l1+sensors.l2)-(sensors.l3+sensors.l4))/2.0;
            const double yaw=std::clamp(sensors.azimuth+std::clamp(horizontal*0.02,-3.0,3.0),0.0,180.0);
            const double pitch=std::clamp(sensors.elevation+std::clamp(vertical*0.02,-3.0,3.0),0.0,180.0);
            if(std::abs(horizontal)>10||std::abs(vertical)>10) { serial.sendPosition(pitch,yaw); sensors.azimuth=yaw; sensors.elevation=pitch; }
            lastServo=std::chrono::steady_clock::now();
        }
        if(frame.channels()==3) cv::cvtColor(frame,rgb,cv::COLOR_BGR2RGB);
        else if(frame.channels()==4) cv::cvtColor(frame,rgb,cv::COLOR_BGRA2RGB);
        else if(frame.channels()==1) cv::cvtColor(frame,rgb,cv::COLOR_GRAY2RGB);
        else continue;
        if(!rgb.isContinuous()) rgb=rgb.clone(); annotated=rgb.clone();
        image_buffer_t src{}; src.width=rgb.cols; src.height=rgb.rows; src.width_stride=rgb.cols; src.height_stride=rgb.rows;
        src.format=IMAGE_FORMAT_RGB888; src.virt_addr=rgb.data; src.size=(int)(rgb.total()*rgb.elemSize()); src.fd=-1;
        image_buffer_t draw=src; draw.virt_addr=annotated.data; draw.size=(int)(annotated.total()*annotated.elemSize());
        std::memset(&detections,0,sizeof(detections));
        if(inference_yolo11_model(&ctx,&src,&detections)!=0) { std::cerr<<"YOLO inference failed\n"; break; }
        for(int i=0;i<detections.count;++i) {
            const auto& d=detections.results[i]; int x=std::max(0,d.box.left),y=std::max(0,d.box.top);
            int w=std::max(1,std::min(rgb.cols-1,d.box.right)-x),h=std::max(1,std::min(rgb.rows-1,d.box.bottom)-y);
            draw_rectangle(&draw,x,y,w,h,COLOR_BLUE,3);
            std::snprintf(label,sizeof(label),"%s %.1f%%",coco_cls_to_name(d.cls_id),d.prop*100.0f);
            draw_text(&draw,label,x,std::max(0,y-20),COLOR_RED,10);
        }
        cv::cvtColor(annotated,display,cv::COLOR_RGB2BGR);
        cv::putText(display,"Objects: "+std::to_string(detections.count),{10,30},cv::FONT_HERSHEY_SIMPLEX,0.7,{0,255,0},2);
        cv::imshow("Solar Vision - YOLO11",display);
        int key=cv::waitKey(1)&0xff; if(key==27||key=='q'||key=='Q') break;
        auto now=std::chrono::steady_clock::now();
        if(mqtt.connected()&&now-lastPublish>=std::chrono::seconds(1)) { mqtt.publish(properties(sensors,detections,sensors.valid)); lastPublish=now; }
        if(now-lastTelemetry>=std::chrono::seconds(5)) { std::cout<<"detections="<<detections.count<<" light_avg="<<(sensors.valid?(sensors.l1+sensors.l2+sensors.l3+sensors.l4)/4:0)<<"\n"; lastTelemetry=now; }
    }
    camera.release(); cv::destroyAllWindows(); release_yolo11_model(&ctx); deinit_post_process(); return 0;
}
