# Video-Client

![C++](https://img.shields.io/badge/C++-17-blue)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Unix--like-green)
[![CMake](https://img.shields.io/badge/Build-CMake-brightgreen)](https://cmake.org)

局域网视频客户端，基于SFML和GStreamer实现服务发现、视频流接收和GUI显示。

---

## ✨ 核心功能

### 🌐 网络服务
- **服务发现**：UDP广播自动探测局域网内视频服务器
- **连接管理**：TCP长连接支持心跳维护与超时机制

### 🎥 视频处理
- **流媒体解码**：基于GStreamer的RTP/H264实时解码管道
- **多线程架构**：独立网络通信、视频解码、UI渲染线程

### 🖥️ 图形界面
- 动态服务器列表展示与手动刷新
- 实时视频显示画布（支持点击交互）
- 设备选择对话框与状态信息栏
- 跨平台GUI框架（SFML 2.5+实现）

---

## 📦 依赖环境

### 基础组件
| 组件 | 最低版本 |
|------|---------|
| C++编译器 | GCC 9+/Clang 10+ |
| CMake | 3.15+ |
| SFML | 2.5+ |
| GStreamer | 1.0+ |
| JSONCPP | 1.9+ |

### GStreamer插件
```bash
gstreamer-1.0 gstreamer-app-1.0 gstreamer-base-1.0 
gstreamer-video-1.0 gstreamer-rtp-1.0
```

### 字体资源
```text
res/SweiSansCJKjp-Medium.ttf
```

---

## 🔧 安装指南

### 系统依赖
<details>
<summary>Ubuntu/Debian</summary>

```bash
sudo apt install build-essential cmake libsfml-dev \
libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
libjsoncpp-dev pkg-config
```
</details>

<details>
<summary>Fedora</summary>

```bash
sudo dnf install gcc-c++ cmake SFML-devel \
gstreamer1-devel gstreamer1-plugins-base-devel \
jsoncpp-devel pkgconf
```
</details>

<details>
<summary>Arch Linux</summary>

```bash
sudo pacman -S base-devel cmake sfml \
gstreamer gst-plugins-base jsoncpp
```
</details>

### 项目构建
```bash
git clone https://github.com/Linductor-alkaid/video-client.git
cd video-client && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## ⚙️ 配置说明

### 字体配置
1. 将中文字体放入`res/fonts/`
2. 修改`gui.cpp`中的`FONT_PATH`定义

### 网络参数
| 参数 | 值 | 配置文件 |
|------|----|---------|
| 服务发现端口 | 37020 | network_manager.h |
| 视频流端口 | 5000 | gst_video_receiver.h |

---

## 🚀 使用手册

### 启动客户端
```bash
# cd build
./video-client
```

### 界面操作
1. 主界面布局：
   - 左侧：服务器列表
   - 右侧：视频显示区
2. 功能操作：
   - 点击"刷新列表"扫描服务器
   - 点击服务器项建立连接
   - 点击视频区切换摄像头
   - 状态栏查看连接质量

---

## 📂 项目结构
```text
video-client/
├── CMakeLists.txt
├── res/
│   └── fonts/
├── src/
│   ├── core/
│   │   ├── network/
│   │   └── video/
│   ├── gui/
│   │   └── widgets/
│   ├── utils/
│   └── main.cpp
└── docs/
```