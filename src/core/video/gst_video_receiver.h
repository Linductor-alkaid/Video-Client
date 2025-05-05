/* 
file: src/core/video/gst_video_receiver.h
author: Linductor
data: 2025/05/05
*/
#ifndef GST_VIDEO_RECEIVER_H
#define GST_VIDEO_RECEIVER_H

#include <functional>
#include <mutex>
#include <thread>
#include <atomic> 
#include <gst/gst.h>
#include <gst/app/gstappsink.h> 
#include <gst/video/video.h> 

enum VideoErrorType {
    GST_VIDEO_ERROR_DECODE,
    GST_VIDEO_ERROR_NETWORK,
    GST_VIDEO_ERROR_EOS
};

struct VideoFrame {
    int width;
    int height;
    const uint8_t* data;
    size_t size;
    GstVideoFormat format;
};

class GstVideoReceiver {
public:
    using FrameCallback = std::function<void(const VideoFrame&)>;
    using ErrorCallback = std::function<void(const std::string&, int)>;

    GstVideoReceiver();
    ~GstVideoReceiver();

    // 初始化视频接收器
    bool initialize(int port = 5000);
    
    // 控制接口
    void start();
    void stop();
    
    // 状态获取
    int getReceiverStatus() const { return receiver_status_.load(); }
    
    // 回调设置
    void setFrameCallback(FrameCallback callback) { frame_callback_ = callback; }
    void setErrorCallback(ErrorCallback callback) { error_callback_ = callback; }

private:
    void processSample(GstSample* sample);
    void handleBusMessages(GstBus* bus);

    GstElement* pipeline_;
    GstAppSink* appsink_;
    
    std::mutex pipeline_mutex_;
    std::thread worker_thread_;
    std::atomic<bool> running_;
    std::atomic<int> receiver_status_; // 200=正常，300=拥塞

    // 回调函数
    FrameCallback frame_callback_;
    ErrorCallback error_callback_;
};

#endif // GST_VIDEO_RECEIVER_H