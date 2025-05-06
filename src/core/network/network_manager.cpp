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
    stopDiscovery();
    if (discovery_running_) return;

    discovery_running_.store(true);
    discovery_thread_ = std::thread([this]() {
        auto start_time = std::chrono::steady_clock::now();
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
        while (discovery_running_ && 
            std::chrono::steady_clock::now() - start_time < DISCOVERY_DURATION) {
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

        discovery_running_.store(false); // 超时后停止
        close(sock);
    });
}

void NetworkManager::stopDiscovery() {
    discovery_running_.store(false);
    if (discovery_thread_.joinable()) {
        discovery_thread_.join();
    }
    discovery_thread_ = std::thread(); // 重置线程对象
}

// 连接管理模块
void NetworkManager::connectToServer(const std::string& ip, int port) {
    if (is_connected_.load() || is_connecting_.exchange(true)) {
        if (connection_status_callback_) {
            connection_status_callback_(false, "Already connecting/connected");
        }
        return;
    }
    cancel_connect_.store(false);

    std::thread([this, ip, port]() {
        bool success = false;
        std::string message;
        int sock = -1; // 提前声明所有变量
        sockaddr_in addr {};
        struct timeval tv;
        char buffer[1024] = {0};
        int n = 0;
        Json::Value cam_list;
        std::vector<int> cameras;
        const std::string handshake = "CLIENT_HANDSHAKE";

        sock = socket(AF_INET, SOCK_STREAM, 0);

        if (sock < 0) {
            message = "Create socket failed";
            goto cleanup;
        }

        // 设置连接超时
        // struct timeval tv;
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        // sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        // 非阻塞连接
        if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
            if (errno == EINPROGRESS) {
                fd_set set;
                FD_ZERO(&set);
                FD_SET(sock, &set);
                timeval timeout{tv.tv_sec, tv.tv_usec};

                if (select(sock + 1, nullptr, &set, nullptr, &timeout) <= 0) {
                    message = "Connection timeout";
                    goto cleanup;
                }

                int so_error;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error != 0) {
                    message = "Connection failed: " + std::string(strerror(so_error));
                    goto cleanup;
                }
            } else {
                message = "Connect error: " + std::string(strerror(errno));
                goto cleanup;
            }
        }

        // 发送握手
        // const std::string handshake = "CLIENT_HANDSHAKE";
        if (send(sock, handshake.c_str(), handshake.size(), 0) <= 0) {
            message = "Handshake failed";
            goto cleanup;
        }

        // 接收摄像头列表（非阻塞）
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        // char buffer[1024];
        n = recv(sock, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            message = (n == 0) ? "Server closed" : "Receive timeout";
            goto cleanup;
        }

        // Json::Value cam_list;
        if (!Json::Reader().parse(buffer, buffer + n, cam_list) || !cam_list.isMember("cameras")) {
            message = "Invalid server response";
            goto cleanup;
        }

        // std::vector<int> cameras;
        for (const auto& cam : cam_list["cameras"]) {
            cameras.push_back(cam.asInt());
        }

        // 连接成功
        {
            std::lock_guard<std::mutex> lock(servers_mutex_);
            current_server_ip_ = ip;
            heartbeat_socket_ = sock;
            is_connected_.store(true);
            success = true;
        }

        // 触发摄像头选择回调
        if (camera_select_callback_) {
            camera_select_callback_(cameras);
        }

        // 启动心跳和视频线程
        heartbeat_thread_ = std::thread(&NetworkManager::handleHeartbeat, this);
        startVideoReception();

        message = "Connected to " + ip;

    cleanup:
        if (!success) {
            if (sock != -1) close(sock);
            if (cancel_connect_.load()) {
                message = "Connection canceled";
            }
        }
        if (connection_status_callback_) {
            connection_status_callback_(success, message);
        }
        is_connecting_.store(false);
    }).detach();
}

void NetworkManager::cancelConnect() {
    if (is_connecting_.load()) {
        cancel_connect_.store(true);
    }
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