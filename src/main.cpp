#include "gui/gui.h"
#include <iostream>

int main() {
    VideoClientUI client_ui;
    
    // 初始化用户界面
    if (!client_ui.init()) {
        std::cerr << "UI初始化失败，请检查资源路径和依赖项" << std::endl;
        return EXIT_FAILURE;
    }

    // 设置全局运行状态并启动主循环
    ui_running.store(true);
    client_ui.update();

    return EXIT_SUCCESS;
}