cmake_minimum_required(VERSION 3.15)
project(video-client LANGUAGES CXX)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/utf-8)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖
find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# GStreamer配置
pkg_check_modules(GSTREAMER REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-base-1.0
    gstreamer-video-1.0
    gstreamer-rtp-1.0
)

# JSON库
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

# 源文件
file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/widgets/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils/*.cpp"
)

# 排除测试文件
list(FILTER SOURCES EXCLUDE REGEX ".*_test.cpp$")

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GSTREAMER_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
)

# 可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接库
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    sfml-graphics
    sfml-window
    sfml-system
    ${CMAKE_THREAD_LIBS_INIT}
    ${GSTREAMER_LIBRARIES}
    ${JSONCPP_LIBRARIES}
)

# Linux下的运行时路径设置
if(UNIX AND NOT APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "-Wl,-rpath,${GSTREAMER_LIBRARY_DIRS}"
    )
endif()

# 资源文件复制
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/res"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/res"
    COMMENT "复制资源文件到构建目录"
)

# 安装配置
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

install(DIRECTORY res/
    DESTINATION res
    PATTERN "*.ttf"
)