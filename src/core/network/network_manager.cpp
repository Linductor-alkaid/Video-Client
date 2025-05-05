/*
file: src/core/network/network_manager.cpp
author: Linductor
date: 2025-05-05
*/
#include "network_manager.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <json/json.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>

#define DISCOVERY_TIMEOUT 1

using namespace std::chrono_literals;
static constexpr auto HEARTBEAT_INTERVAL = 500ms;

NetworkManager::NetworkManager() {
    gst_init(nullptr, nullptr);
    discovery_running_.store(false);
    is_connected_.store(false);
}

NetworkManager::~NetworkManager() {
    disconnect();
    stopDiscovery();
}

// 服务发现模块
void NetworkManager::startDiscovery() {
    if (discovery_running_) return;

    discovery_running_.store(true);
    discovery_thread_ = std::thread([this]() {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) return;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(DISCOVERY_PORT);
        addr.sin_addr.s_addr = INADDR_ANY;
        bind(sock, (sockaddr*)&addr, sizeof(addr));

        timeval tv{DISCOVERY_TIMEOUT, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        char buffer[1024];
        while (discovery_running_) {
            sockaddr_in from{};
            socklen_t from_len = sizeof(from);
            int n = recvfrom(sock, buffer, sizeof(buffer), 0,
                           (sockaddr*)&from, &from_len);

            if (n > 0) {
                Json::Value server_info;
                if (Json::Reader().parse(buffer, buffer + n, server_info)) {
                    server_info["ip"] = inet_ntoa(from.sin_addr);
                    
                    std::lock_guard<std::mutex> lock(servers_mutex_);
                    if (std::none_of(servers_.begin(), servers_.end(),
                        [&](const auto& s) {
                            return s["ip"] == server_info["ip"] && 
                                   s["heartbeat_port"] == server_info["heartbeat_port"];
                        })) {
                        servers_.push_back(server_info);
                        if (server_list_callback_) {
                            server_list_callback_(servers_);
                        }
                    }
                }
            }
        }
        close(sock);
    });
}

void NetworkManager::stopDiscovery() {
    discovery_running_.store(false);
    if (discovery_thread_.joinable()) {
        discovery_thread_.join();
    }
}

// 连接管理模块
bool NetworkManager::connectToServer(const std::string& ip, int port) {
    if (is_connected_) disconnect();

    // 创建TCP socket
    heartbeat_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (heartbeat_socket_ < 0) {
        if (connection_status_callback_) {
            connection_status_callback_(false, "创建socket失败");
        }
        return false;
    }

    // 设置超时参数
    struct timeval tv;
    tv.tv_sec = 3;  // 3秒连接超时
    tv.tv_usec = 0;
    setsockopt(heartbeat_socket_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // 连接服务器
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (connect(heartbeat_socket_, (sockaddr*)&addr, sizeof(addr)) != 0) {
        close(heartbeat_socket_);
        heartbeat_socket_ = -1;
        
        if (connection_status_callback_) {
            connection_status_callback_(false, "连接服务器超时");
        }
        return false;
    }

    // 发送握手标识
    const std::string handshake = "CLIENT_HANDSHAKE";
    if (send(heartbeat_socket_, handshake.c_str(), handshake.size(), 0) <= 0) {
        close(heartbeat_socket_);
        heartbeat_socket_ = -1;
        
        if (connection_status_callback_) {
            connection_status_callback_(false, "握手信号发送失败");
        }
        return false;
    }

    // 设置接收超时
    tv.tv_sec = 5;  // 5秒接收超时
    setsockopt(heartbeat_socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 接收摄像头列表
    char buffer[1024];
    int n = recv(heartbeat_socket_, buffer, sizeof(buffer), 0);
    if (n <= 0) {
        close(heartbeat_socket_);
        heartbeat_socket_ = -1;
        
        std::string err = (n == 0) ? "连接被服务器关闭" : "接收摄像头列表超时";
        if (connection_status_callback_) {
            connection_status_callback_(false, err);
        }
        return false;
    }

    // 解析摄像头列表
    Json::Value cam_list;
    if (!Json::Reader().parse(buffer, buffer + n, cam_list) || 
        !cam_list.isMember("cameras")) {
        close(heartbeat_socket_);
        heartbeat_socket_ = -1;
        
        if (connection_status_callback_) {
            connection_status_callback_(false, "无效的服务器响应");
        }
        return false;
    }

    // 转换摄像头列表格式
    std::vector<int> cameras;
    try {
        for (const auto& cam : cam_list["cameras"]) {
            cameras.push_back(cam.asInt());
        }
    } catch (...) {
        close(heartbeat_socket_);
        heartbeat_socket_ = -1;
        
        if (connection_status_callback_) {
            connection_status_callback_(false, "摄像头数据解析失败");
        }
        return false;
    }

    // 更新连接状态
    current_server_ip_ = ip;
    is_connected_.store(true);

    // 触发UI回调
    if (connection_status_callback_) {
        connection_status_callback_(true, "成功连接到 " + ip);
    }
    
    // 触发摄像头选择（主线程执行）
    if (camera_select_callback_) {
        auto& callback = camera_select_callback_;
        std::thread([callback, cameras]() {
            callback(cameras);
        }).detach();
    }

    // 启动心跳线程
    heartbeat_thread_ = std::thread(&NetworkManager::handleHeartbeat, this);
    
    // 启动视频接收
    startVideoReception();

    return true;
}

// 修改selectCamera方法
void NetworkManager::selectCamera(int index) {
    if (!is_connected_) return;

    Json::Value response;
    response["camera_index"] = index;
    std::string json_str = Json::FastWriter().write(response);
    
    // 添加3次重试机制
    for (int retry = 0; retry < 3; ++retry) {
        if (send(heartbeat_socket_, json_str.c_str(), json_str.size(), 0) > 0) {
            // 等待服务端确认
            char ack = 0;
            if (recv(heartbeat_socket_, &ack, 1, 0) > 0 && ack == '1') {
                connection_status_callback_(true, "摄像头选择成功");
                return;
            }
        }
        std::this_thread::sleep_for(100ms);
    }
    
    connection_status_callback_(false, "摄像头选择失败");
    disconnect();
}

void NetworkManager::disconnect() {
    is_connected_.store(false);
    if (heartbeat_socket_ != -1) {
        close(heartbeat_socket_);
        heartbeat_socket_ = -1;
    }
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
}

// 心跳维护模块
void NetworkManager::handleHeartbeat() {
    char buffer[16];
    while (is_connected_) {
        int bytes_received = recv(heartbeat_socket_, buffer, sizeof(buffer), 0);
        
        
        if (!camera_selected_ || 
            std::chrono::steady_clock::now() - last_heartbeat_ > 3s) {
            connection_status_callback_(false, "心跳超时");
            break;
        }
        if (bytes_received <= 0) {
            if (connection_status_callback_) {
                connection_status_callback_(false, "连接已断开");
            }
            is_connected_.store(false);
            break;
        }

        std::string status = std::to_string(receiver_status_.load());
        if (send(heartbeat_socket_, status.c_str(), status.size(), 0) <= 0) {
            if (connection_status_callback_) {
                connection_status_callback_(false, "心跳发送失败");
            }
            is_connected_.store(false);
            break;
        }
        last_heartbeat_ = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(HEARTBEAT_INTERVAL);
        
    }
}

// 视频接收模块
void NetworkManager::startVideoReception() {
    video_thread_ = std::thread([this]() {
        GstElement *pipeline = gst_parse_launch(
            ("udpsrc port=" + std::to_string(VIDEO_PORT) + " ! "
            "application/x-rtp,media=video,encoding-name=H264 ! "
            "rtpjitterbuffer latency=100 ! "
            "rtph264depay ! avdec_h264 ! videoconvert ! appsink name=sink")
            .c_str(), nullptr);

        GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
        gst_app_sink_set_emit_signals(GST_APP_SINK(sink), true);
        gst_app_sink_set_drop(GST_APP_SINK(sink), true);
        gst_app_sink_set_max_buffers(GST_APP_SINK(sink), 5);

        GstBus *bus = gst_element_get_bus(pipeline);
        gst_element_set_state(pipeline, GST_STATE_PLAYING);

        while (is_connected_) {
            GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
            if (sample && frame_callback_) {
                GstBuffer *buffer = gst_sample_get_buffer(sample);
                GstMapInfo map;
                gst_buffer_map(buffer, &map, GST_MAP_READ);
                frame_callback_(map.data, map.size);
                gst_buffer_unmap(buffer, &map);
                gst_sample_unref(sample);
            }

            // 处理总线消息
            GstMessage *msg = gst_bus_pop_filtered(bus, 
                static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_QOS));
            if (msg) {
                // 处理错误和状态消息（同原逻辑）
                gst_message_unref(msg);
            }
        }

        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    });
}