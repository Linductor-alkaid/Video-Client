/* 
file: src/gui/widgets/server_list.h
author: Linductor
data: 2025/05/05
*/
#ifndef SERVER_LIST_WIDGET_H
#define SERVER_LIST_WIDGET_H

#include <SFML/Graphics.hpp>
#include <json/json.h> 
#include <functional>
#include <vector>
#include <string>
#include "core/network/network_manager.h"

struct ServerListCache {
    std::mutex mtx;
    std::vector<Json::Value> servers;
    time_t last_update = 0;
    
    void update(const std::vector<Json::Value>& new_servers);
    std::vector<Json::Value> get();
};

class ServerListWidget {
public:
    using SelectCallback = std::function<void(const Json::Value& server)>;
    using StatusCallback = std::function<void(const std::string& message)>;

    ServerListWidget(NetworkManager& net_mgr, ServerListCache& cache);
    
    // 初始化方法
    bool init(sf::Font& font);
    void setPosition(float x, float y);
    
    void updateList(const std::vector<Json::Value>& servers);

    // 事件处理
    void handleEvent(const sf::Event& event);
    
    // 绘制方法
    void draw(sf::RenderWindow& window) const;
    
    // 回调设置
    void setSelectCallback(SelectCallback cb) { select_callback_ = cb; }
    void setStatusCallback(StatusCallback cb) { status_callback_ = cb; }

private:
    void drawItems(sf::RenderWindow& window) const;
    void checkItemClick(const sf::Vector2f& pos);
    void onRefreshClick();

    // 依赖组件
    NetworkManager& net_manager_;
    ServerListCache& server_cache_;
    const sf::Font* font_ = nullptr;
    
    std::vector<Json::Value> displayed_servers_;
    
    // UI元素
    sf::RectangleShape panel_;
    sf::Text title_;
    sf::RectangleShape refresh_btn_;
    sf::Text refresh_text_;
    
    // 状态管理
    int selected_index_ = -1;
    sf::Vector2f position_;
    
    // 回调函数
    SelectCallback select_callback_;
    StatusCallback status_callback_;
};

#endif // SERVER_LIST_WIDGET_H