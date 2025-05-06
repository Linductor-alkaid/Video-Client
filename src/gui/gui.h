/* 
filename: gui.h
author: Linductor
date: 2025/05/04
*/

#ifndef VIDEO_CLIENT_UI_H
#define VIDEO_CLIENT_UI_H

#include <SFML/Graphics.hpp>
#include <json/json.h>
#include <mutex>
#include <vector>
#include <atomic>
#include "gui/widgets/server_list.h"
#include "core/network/network_manager.h"

struct RawVideoFrame {
    int width;
    int height;
    std::vector<uint8_t> pixels;

    // 添加构造函数
    RawVideoFrame(int w, int h, std::vector<uint8_t> pix)
        : width(w), height(h), pixels(std::move(pix)) {}
    
    // 删除默认构造函数（按需可选）
    RawVideoFrame() = delete; 
};

class VideoClientUI {
public:
    VideoClientUI();
    ~VideoClientUI();
    bool init();
    void update();

    // 视频帧处理接口
    void pushVideoFrame(int width, int height, std::vector<uint8_t> pixels);
    
private:
    void handleEvents();
    void updateVideoFrame();
    void updateStatusText();
    void initVideoPanel();
    void initStatusBar();
    void onRefreshClicked();
    void onServerSelected(const Json::Value& server);
    void onConnectionStatus(bool connected, const std::string& msg);
    void updateServerListUI(const std::vector<Json::Value>& servers);
    void showCameraSelection(const std::vector<int>& cameras);
    void onCameraSelected(int index);

    // UI组件
    sf::RenderWindow window;
    sf::Font font;
    NetworkManager net_manager_;
    ServerListWidget server_list_widget_;
    sf::RectangleShape video_border;
    sf::Sprite video_sprite;
    sf::RectangleShape status_bar;
    sf::Text status_text;
    sf::RectangleShape camera_modal_;
    std::vector<sf::Text> camera_options_;
    sf::Texture video_texture;

    std::mutex status_mutex_;
    std::chrono::system_clock::time_point last_connected_time_;
    bool needs_redraw_ = false;

    // 状态
    std::string current_server;
    std::vector<int> camera_ids_; // 存储当前摄像头选项的ID列表
    bool is_connected = false;
    bool is_connecting = false;
    bool is_modal_open_ = false;
    bool show_camera_options_ = false;
};

extern ServerListCache server_cache;
extern std::atomic<bool> ui_running;
extern std::deque<RawVideoFrame> raw_frames;
extern std::mutex frame_mutex;

#endif // VIDEO_CLIENT_UI_H