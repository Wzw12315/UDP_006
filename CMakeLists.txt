cmake_minimum_required(VERSION 3.10)
project(udp_006 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt相关配置
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt5 COMPONENTS Widgets REQUIRED)
find_package(Threads REQUIRED)

# 公共源文件
set(COMMON_SOURCES
    src/protocol/protocol.cpp
    src/network/udp_socket.cpp
    src/logger/logger.cpp
    src/common/crc16.h
)

# 头文件目录
include_directories(
    src/protocol
    src/network
    src/logger
    src/sender
    src/receiver
    src/common
)

# 发送端配置
set(SENDER_SOURCES
    ${COMMON_SOURCES}
    src/sender/udp_sender.cpp
    src/sender/main_sender_window.ui
    src/sender/main_sender_window.cpp
    src/sender/sender_main.cpp
)

add_executable(udp_sender ${SENDER_SOURCES})
target_link_libraries(udp_sender
    PRIVATE Qt5::Widgets
    PRIVATE Threads::Threads
)

# 接收端配置
set(RECEIVER_SOURCES
    ${COMMON_SOURCES}
    src/receiver/udp_receiver.cpp
    src/receiver/main_receiver_window.ui
    src/receiver/main_receiver_window.cpp
    src/receiver/receiver_main.cpp
)

add_executable(udp_receiver ${RECEIVER_SOURCES})
target_link_libraries(udp_receiver
    PRIVATE Qt5::Widgets
    PRIVATE Threads::Threads
)

# 解析工具配置
set(PARSER_SOURCES
    ${COMMON_SOURCES}
    src/parser/bin_parser_window.ui
    src/parser/bin_parser_window.cpp
    src/parser/parser_main.cpp
)

add_executable(bin_parser ${PARSER_SOURCES})
target_link_libraries(bin_parser
    PRIVATE Qt5::Widgets
    PRIVATE Threads::Threads
)
