/*
file: src/core/network/network_manager.h
author: Linductor
date: 2025-05-05
*/
#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <functional>
#include <vector>
#include <string>
#include <json/json.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>

class NetworkManager {
public:
    // 状态回调类型
    using FrameCallback = std::function<void(const uint8_t* data, size_t size)>;
    using StatusCallback = std::function<void(bool connected, const std::string& message)>;
    using CameraListCallback = std::function<void(const std::vector<int>& cameras)>;

    NetworkManager();
    ~NetworkManager();

    // 服务发现接口
    void startDiscovery();
    void stopDiscovery();
    std::vector<Json::Value> getDiscoveredServers() const;

    // 连接管理接口
    void connectToServer(const std::string& ip, int port);
    void cancelConnect();
    void disconnect();
    void selectCamera(int index);

    // 回调设置接口
    void setFrameCallback(FrameCallback callback) { frame_callback_ = callback; }
    void setStatusCallback(StatusCallback callback) { connection_status_callback_ = callback; }
    void setCameraListCallback(CameraListCallback callback) { camera_select_callback_ = callback; }
    void setServerListCallback(std::function<void(std::vector<Json::Value>)> callback) { 
        server_list_callback_ = callback; 
    }

    int getReceiverStatus() const { 
        return receiver_status_.load(); 
    }
    void refreshServerList() {
        {
            std::lock_guard<std::mutex> lock(servers_mutex_);
            servers_.clear();  // 清空旧服务器列表
        } 
        startDiscovery(); 
    }
    bool isDiscovering() const { 
        return discovery_running_.load(); 
    }
    CameraListCallback getCameraListCallback() const { return camera_select_callback_; }

private:
    void handleHeartbeat();
    void startVideoReception();

    // 网络状态
    mutable std::mutex servers_mutex_;
    std::vector<Json::Value> servers_;
    std::atomic<bool> discovery_running_{false};
    std::atomic<bool> is_connected_{false};
    std::atomic<int> receiver_status_{200};  // 200=正常，300=拥塞
    std::atomic<bool> camera_selected_{false}; 
    std::chrono::steady_clock::time_point last_heartbeat_;
    static constexpr std::chrono::seconds DISCOVERY_DURATION{5};

    // 网络资源
    int heartbeat_socket_ = -1;
    std::string current_server_ip_;

    // 线程管理
    std::thread discovery_thread_;
    std::thread heartbeat_thread_;
    std::thread video_thread_;
    std::atomic<bool> is_connecting_{false};
    std::atomic<bool> cancel_connect_{false};

    // 回调函数
    FrameCallback frame_callback_;
    StatusCallback connection_status_callback_;
    CameraListCallback camera_select_callback_;
    std::function<void(std::vector<Json::Value>)> server_list_callback_;

    // GStreamer参数
    static constexpr int DISCOVERY_PORT = 37020;
    static constexpr int VIDEO_PORT = 5000;
};

#endif // NETWORK_MANAGER_H