/* 
file: src/gui/widgets/server_list.cpp
author: Linductor
data: 2025/05/05
*/
#include "server_list.h"
#include <json/json.h>
#include <SFML/Graphics.hpp>
#include <iostream>
#include <string>

void ServerListCache::update(const std::vector<Json::Value>& new_servers) {
    std::lock_guard<std::mutex> lock(mtx);
    servers = new_servers;
    last_update = time(nullptr);
}

std::vector<Json::Value> ServerListCache::get() {
    std::lock_guard<std::mutex> lock(mtx);
    return servers;
}

ServerListWidget::ServerListWidget(NetworkManager& net_mgr, ServerListCache& cache)
    : net_manager_(net_mgr), server_cache_(cache), displayed_servers_() {
    std::string title_name = "可用服务器";
    std::string refresh_name = "刷新列表";
    
    // 初始化UI组件
    panel_.setSize({300, 600});
    panel_.setFillColor(sf::Color(50, 50, 60));
    
    title_.setFont(*font_);
    title_.setString(sf::String::fromUtf8(std::begin(title_name), std::end(title_name)));
    title_.setCharacterSize(24);
    
    refresh_btn_.setSize({120, 40});
    refresh_btn_.setFillColor(sf::Color(80, 80, 90));
    refresh_text_.setFont(*font_);
    refresh_text_.setString(sf::String::fromUtf8(std::begin(refresh_name), std::end(refresh_name)));
    refresh_text_.setCharacterSize(24);
}

bool ServerListWidget::init(sf::Font& font) {
    font_ = &font;
    title_.setFont(font);
    refresh_text_.setFont(font);
    
    // 设置初始位置（由外部布局控制）
    // setPosition(20, 80);
    return true;
}

void ServerListWidget::setPosition(float x, float y) {
    position_ = {x, y};
    panel_.setPosition(x, y);
    title_.setPosition(x + 10, y - 45);
    refresh_btn_.setPosition(x + panel_.getSize().x - 130, y - 50);
    refresh_text_.setPosition(refresh_btn_.getPosition().x + 10, y - 45);
}

void ServerListWidget::updateList(const std::vector<Json::Value>& servers) {
    // 更新显示的服务器列表
    displayed_servers_ = servers; 
    // 重置选中状态
    selected_index_ = -1; 
    // 触发重新布局（下次绘制时自动处理）
}

void ServerListWidget::handleEvent(const sf::Event& event) {
    if (event.type == sf::Event::MouseButtonPressed) {
        auto mouse_pos = sf::Vector2f(event.mouseButton.x, event.mouseButton.y);
        
        // 处理刷新按钮点击
        if (refresh_btn_.getGlobalBounds().contains(mouse_pos)) {
            onRefreshClick();
            return;
        }
        
        // 处理服务器项点击
        checkItemClick(mouse_pos);
    }
}

void ServerListWidget::draw(sf::RenderWindow& window) const {
    window.draw(panel_);
    window.draw(title_);
    window.draw(refresh_btn_);
    window.draw(refresh_text_);
    drawItems(window);
}

void ServerListWidget::drawItems(sf::RenderWindow& window) const {
    const auto& servers = displayed_servers_; 
    float y = position_.y + 20;
    
    for (size_t i = 0; i < servers.size(); ++i) {
        // 背景框
        sf::RectangleShape bg({panel_.getSize().x - 20, 60});
        bg.setPosition(position_.x + 10, y);
        bg.setFillColor(i == selected_index_ ? 
            sf::Color(70, 70, 90) : sf::Color(60, 60, 70));
        
        // 服务器信息
        sf::Text name;
        name.setFont(*font_);
        name.setString(servers[i]["name"].asString());
        name.setPosition(position_.x + 20, y + 10);
        
        sf::Text ip;
        ip.setFont(*font_);
        ip.setString(servers[i]["ip"].asString());
        ip.setCharacterSize(14);
        ip.setPosition(position_.x + 20, y + 35);
        
        window.draw(bg);
        window.draw(name);
        window.draw(ip);
        
        y += 70;
    }
}

void ServerListWidget::checkItemClick(const sf::Vector2f& pos) {
    const auto& servers = displayed_servers_;
    float start_y = position_.y + 20;
    
    for (size_t i = 0; i < servers.size(); ++i) {
        sf::FloatRect item_rect(
            position_.x + 10,
            start_y,
            panel_.getSize().x - 20,
            60
        );
        
        if (item_rect.contains(pos)) {
            selected_index_ = i;
            if (select_callback_) {
                select_callback_(servers[i]);
            }
            break;
        }
        start_y += 70;
    }
}

void ServerListWidget::onRefreshClick() {
    net_manager_.startDiscovery(); // 触发网络发现
    server_cache_.update({}); // 清空缓存
    selected_index_ = -1;
    
    if (status_callback_) {
        status_callback_("正在搜索服务器...");
    }
}
