/* 
filename: gui.cpp
author: Linductor
date: 2025/05/04
*/
#include "gui/gui.h"
#include <SFML/Graphics.hpp>
#include <iostream>
#include <atomic>
#include <mutex>
#include <deque>
#include <chrono>

// 字体文件路径（需实际存在）
#define FONT_PATH "res/SweiSansCJKjp-Medium.ttf"

// 全局资源定义
ServerListCache server_cache;
std::atomic<bool> ui_running{false};
std::deque<sf::Texture> video_frames;
std::mutex frame_mutex;

VideoClientUI::VideoClientUI() 
    : server_list_widget_(net_manager_, server_cache) { // 初始化列表传递参数
    // 其他成员变量初始化（若有）
}

VideoClientUI::~VideoClientUI() {
    // 确保资源正确释放
    if (window.isOpen()) window.close();
}

// 初始化UI资源
bool VideoClientUI::init() {
    if (!font.loadFromFile(FONT_PATH)) {
        std::cerr << "错误：无法加载字体文件，请检查路径: " << FONT_PATH << std::endl;
        return false;
    }else {
        std::cout << "字体加载成功: " << FONT_PATH << std::endl;
        std::cout << "字体信息: " << font.getInfo().family << std::endl;
    }

    window.create(sf::VideoMode(1280, 720), sf::String::fromUtf8(std::begin("视频客户端"), std::end("视频客户端")), sf::Style::Close);
    window.setFramerateLimit(60);
    
    // 初始化网络组件
    net_manager_.setServerListCallback(
        [this](const auto& servers){ this->updateServerListUI(servers); });
    // net_manager_.setStatusCallback(
    //     [this](bool conn, const std::string& msg){ this->onConnectionStatus(conn, msg); });
    // 添加网络状态绑定
    net_manager_.setStatusCallback([this](bool conn, const std::string& msg){
        this->onConnectionStatus(conn, msg);
    });
    
    // 添加摄像头选择回调
    net_manager_.setCameraListCallback([this](const std::vector<int>& cams){
        if (!cams.empty()) {
            showCameraSelection(cams);
        }else {
            status_text.setString("错误：无可用摄像头");
        }
    });
    
    // 初始化服务器列表组件
    if (!server_list_widget_.init(font)) {
        std::cerr << "初始化服务器列表失败" << std::endl;
        return false;
    }
    server_list_widget_.setPosition(20, 80);
    server_list_widget_.setSelectCallback(
        [this](const Json::Value& server){ this->onServerSelected(server); });
    
    // 初始化其他UI组件
    initVideoPanel();
    initStatusBar();
    
    return true;
}

// 视频显示面板初始化
void VideoClientUI::initVideoPanel() {
    video_border.setSize({900, 600});
    video_border.setPosition(340, 80);
    video_border.setFillColor(sf::Color(30, 30, 40));
}

// 状态栏初始化
void VideoClientUI::initStatusBar() {
    status_bar.setSize({1240, 30});
    status_bar.setPosition(20, 680);
    status_bar.setFillColor(sf::Color(40, 40, 50));
    
    status_text.setFont(font);
    status_text.setCharacterSize(18);
    status_text.setPosition(30, 685);

    status_text.setFillColor(sf::Color(220, 220, 220)); // 亮灰色
    status_text.setOutlineColor(sf::Color::Black);       // 可选：添加黑色描边
    status_text.setOutlineThickness(1);
}

// 处理输入事件
void VideoClientUI::handleEvents() {
    sf::Event event;
    while (window.pollEvent(event)) {
        if (is_modal_open_) {
            // 拦截所有非模态处理的事件
            if (event.type == sf::Event::MouseButtonPressed && show_camera_options_) {
                sf::Vector2f mouse_pos(event.mouseButton.x, event.mouseButton.y);
                for (size_t i = 0; i < camera_options_.size(); ++i) {
                    if (camera_options_[i].getGlobalBounds().contains(mouse_pos)) {
                        onCameraSelected(camera_ids_[i]);
                        show_camera_options_ = false; // 选择后隐藏
                        break;
                    }
                }
            }
            continue; // 模态打开时跳过其他处理
        }
        if (event.type == sf::Event::Closed) {
            window.close();
        }
        
        // 传递事件给服务器列表组件
        server_list_widget_.handleEvent(event);

        if (!camera_options_.empty() && 
            event.type == sf::Event::MouseButtonPressed) {
            for (size_t i = 0; i < camera_options_.size(); ++i) {
                if (camera_options_[i].getGlobalBounds().contains(
                    event.mouseButton.x, 
                    event.mouseButton.y)
                ) {
                    int cam_id = camera_ids_[i];
                    onCameraSelected(cam_id);
                    camera_options_.clear();
                    break;
                }
            }
        } 
    }
}

// 更新视频帧显示（线程安全）
void VideoClientUI::updateVideoFrame() {
    std::lock_guard<std::mutex> lock(frame_mutex);
    if (!video_frames.empty()) {
        video_sprite.setTexture(video_frames.front(), true);
        video_frames.pop_front();

        // 自适应缩放
        auto tex_size = video_sprite.getTexture()->getSize();
        float scale = std::min(
            860.0f / tex_size.x, 
            580.0f / tex_size.y
        );
        video_sprite.setScale(scale, scale);
    }
}

// 状态栏文本更新
void VideoClientUI::updateStatusText() {
    std::string status;
    
    if (is_connecting) {
        status = "正在连接...";
    } else if (net_manager_.isDiscovering()) {
        status = "正在搜索服务器...";
    } else if (!is_connected) {
        status = "未连接 | 发现" + 
            std::to_string(server_cache.get().size()) + "个服务器";
    } else {
        status = "已连接至 " + current_server + 
            " | 缓冲帧:" + std::to_string(video_frames.size()) + 
            " | 网络状态:" + std::to_string(net_manager_.getReceiverStatus());
    }
    
    status_text.setString(sf::String::fromUtf8(std::begin(status), std::end(status)));
}

void VideoClientUI::onConnectionStatus(bool connected, const std::string& msg) {
    is_connecting = false;
    static auto last_connected_time_ = std::chrono::system_clock::now();
    
    std::lock_guard<std::mutex> lock(status_mutex_); // 添加互斥锁
    auto now = std::chrono::system_clock::now();
    is_connected = connected;
    status_text.setString(msg);
    
    // 根据状态更新UI颜色
    // const auto now = std::chrono::steady_clock::now();
    if (connected) {
        status_bar.setFillColor(sf::Color(0, 150, 0));
        last_connected_time_ = now;
    } else {
        // 红色闪烁逻辑
        if (now - last_connected_time_ < std::chrono::seconds(2)) {
            status_bar.setFillColor((now.time_since_epoch().count() / 500000000) % 2 ? 
                sf::Color(150, 0, 0) : sf::Color(80, 0, 0));
        } else {
            status_bar.setFillColor(sf::Color(150, 0, 0));
        }
    }
    
    // 重绘状态栏
    needs_redraw_ = true;
}

// 从网络线程接收视频帧（线程安全）
void VideoClientUI::pushVideoFrame(sf::Texture frame) {
    std::lock_guard<std::mutex> lock(frame_mutex);
    if (video_frames.size() < 10) { // 限制缓冲数量
        video_frames.emplace_back(std::move(frame));
    }
}

// 按钮点击处理
void VideoClientUI::onRefreshClicked() {
    server_cache.update({});
    server_list_widget_.updateList({});
    status_text.setString(sf::String::fromUtf8(std::begin("正在搜索服务器..."), std::end("正在搜索服务器...")));
    net_manager_.refreshServerList();
}

void VideoClientUI::onServerSelected(const Json::Value& server) {
    current_server = server["ip"].asString();
    is_connecting = true;
    status_text.setString(sf::String::fromUtf8(std::begin("正在连接..."), std::end("正在连接...")));
    
    // 获取并保存原始回调
    auto original_cam_callback = net_manager_.getCameraListCallback();
    
    // 设置新的状态回调（注意捕获original_cam_callback）
    net_manager_.setStatusCallback([this, original_cam_callback](bool conn, const std::string& msg) {
        is_connecting = false;
        if (conn) {
            // 恢复原始回调
            net_manager_.setCameraListCallback(original_cam_callback);
        }
        onConnectionStatus(conn, msg);
    });

    // 发起连接
    net_manager_.connectToServer(server["ip"].asString(), server["heartbeat_port"].asInt());
}

// 渲染主循环
void VideoClientUI::update() {
    while (window.isOpen()) {
        handleEvents();
        window.clear(sf::Color(25, 25, 35));
        
        server_list_widget_.draw(window);

        if (is_modal_open_) {
            window.draw(camera_modal_);
            for (auto& opt : camera_options_) window.draw(opt);
        }
        
        // 绘制视频区域
        
        updateVideoFrame();
        if (video_sprite.getTexture()) {
            window.draw(video_sprite);
        }
        if (show_camera_options_) {
            for (auto& opt : camera_options_) window.draw(opt);
        }
        window.draw(video_border);

        // 状态栏
        window.draw(status_bar);
        updateStatusText();
        window.draw(status_text);
        window.display();

        // 检查窗口状态
        if (!window.isOpen()) break;
    }
}

void VideoClientUI::updateServerListUI(const std::vector<Json::Value>& servers) {
    server_cache.update(servers);
    server_list_widget_.updateList(servers); 
}

void VideoClientUI::showCameraSelection(const std::vector<int>& cameras) {
    if (cameras.empty()) return;
    camera_ids_ = cameras;

    is_modal_open_ = true; // 进入模态状态
    camera_options_.clear();
    // 创建模态选择框
    // 设置位置到视频区域顶部
    float start_x = video_border.getPosition().x + 20;
    float start_y = video_border.getPosition().y + 20;
    
    // 创建选项
    camera_options_.clear();
    for (size_t i = 0; i < cameras.size(); ++i) {
        sf::Text option;
        option.setFont(font);
        option.setString(sf::String::fromUtf8(std::begin("摄像头 " + std::to_string(cameras[i])), std::end("摄像头 " + std::to_string(cameras[i]))));
        option.setPosition(start_x, start_y + i * 40);
        camera_options_.push_back(option);
    }
}

void VideoClientUI::onCameraSelected(int index) {
    try {
        if (index >= 0 && index < camera_ids_.size()) {
            net_manager_.selectCamera(index);
            status_text.setString(sf::String::fromUtf8(std::begin("已选择摄像头 " + std::to_string(index)), std::end("已选择摄像头 " + std::to_string(index))));
        }
    }catch (const std::exception& e) {
        std::cerr << "摄像头选择错误: " << e.what() << std::endl;
        status_text.setString(sf::String::fromUtf8(std::begin("选择摄像头失败"), std::end("选择摄像头失败")));
    }
}