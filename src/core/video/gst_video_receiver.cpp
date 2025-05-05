/* 
file: src/core/video/gst_video_receiver.cpp
author: Linductor
data: 2025/05/05
*/
#include "gst_video_receiver.h"
#include <gst/app/gstappsink.h>
#include <gst/video/video.h> 
#include <iostream>
#include <mutex>
#include <atomic>

GstVideoReceiver::GstVideoReceiver() 
    : pipeline_(nullptr), appsink_(nullptr), frame_callback_(nullptr),
      receiver_status_(200), running_(false) {
    gst_init(nullptr, nullptr);
}

GstVideoReceiver::~GstVideoReceiver() {
    stop();
}

// 初始化GStreamer管道
bool GstVideoReceiver::initialize(int port) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    
    const std::string pipeline_str = 
        "udpsrc port=" + std::to_string(port) + " ! "
        "application/x-rtp,media=video,encoding-name=H264 ! "
        "rtpjitterbuffer latency=100 ! "
        "rtph264depay ! avdec_h264 ! "
        "videoconvert ! videoscale ! video/x-raw,format=RGBA ! "
        "appsink name=sink emit-signals=true";

    GError* error = nullptr;
    pipeline_ = gst_parse_launch(pipeline_str.c_str(), &error);
    
    if (error) {
        std::cerr << "GStreamer初始化失败: " << error->message << std::endl;
        g_error_free(error);
        return false;
    }

    appsink_ = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline_), "sink"));
    
    // 配置appsink参数
    gst_app_sink_set_emit_signals(appsink_, true);
    gst_app_sink_set_drop(appsink_, true);
    gst_app_sink_set_max_buffers(appsink_, 5);
    
    return true;
}

// 启动视频接收线程
void GstVideoReceiver::start() {
    if (running_) return;
    
    running_ = true;
    worker_thread_ = std::thread([this]() {
        gst_element_set_state(pipeline_, GST_STATE_PLAYING);
        
        GstBus* bus = gst_element_get_bus(pipeline_);
        while (running_) {
            // 处理视频帧
            GstSample* sample = gst_app_sink_pull_sample(appsink_);
            if (sample) {
                processSample(sample);
                gst_sample_unref(sample);
            }
            
            // 处理总线消息
            handleBusMessages(bus);
        }
        
        // 清理资源
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(bus);
    });
}

// 停止接收
void GstVideoReceiver::stop() {
    running_ = false;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    if (pipeline_) {
        gst_object_unref(appsink_);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
}

// 处理视频采样数据
void GstVideoReceiver::processSample(GstSample* sample) {
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        if (frame_callback_) {
            VideoFrame frame {
                .width = 0,
                .height = 0,
                .data = map.data,
                .size = map.size,
                .format = GST_VIDEO_FORMAT_UNKNOWN  // 先初始化为未知格式
            };
            
            // 正确从 sample 的 caps 中提取视频格式
            GstCaps* caps = gst_sample_get_caps(sample);
            if (caps) {
                GstVideoInfo info;
                gst_video_info_init(&info);
                if (gst_video_info_from_caps(&info, caps)) {
                    frame.width = info.width;
                    frame.height = info.height;
                    frame.format = info.finfo->format;
                }
            }
            
            frame_callback_(frame);
        }
        gst_buffer_unmap(buffer, &map);
    }
}

// 处理总线消息
void GstVideoReceiver::handleBusMessages(GstBus* bus) {
    GstMessage* msg = gst_bus_pop_filtered(bus, 
        static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_QOS));
    
    if (!msg) return;

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_QOS: {
            guint64 timestamp;
            gst_message_parse_qos(msg, nullptr, nullptr, nullptr, &timestamp, nullptr);
            
            static guint64 last_timestamp = 0;
            receiver_status_.store((timestamp - last_timestamp > 20000000) ? 300 : 200);
            last_timestamp = timestamp;
            break;
        }
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            
            if (error_callback_) {
                error_callback_(err->message, GST_VIDEO_ERROR_DECODE);
            }
            
            g_error_free(err);
            g_free(debug);
            break;
        }
        case GST_MESSAGE_EOS:
            if (error_callback_) {
                error_callback_("视频流意外终止", GST_VIDEO_ERROR_EOS);
            }
            break;
        default:
            break;
    }
    
    gst_message_unref(msg);
}