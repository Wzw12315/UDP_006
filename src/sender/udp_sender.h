// udp_sender.h
#pragma once
#include "udp_socket.h"
#include "protocol.h"
#include "logger.h"
#include <QObject>
#include <atomic>
#include <fstream>
#include <string>

class UdpSender : public QObject {
    Q_OBJECT
public:
    UdpSender(Logger& logger) : logger_(logger), is_paused_(false), is_running_(false) {}

    // 初始化保存文件
    bool init_save_file();
    // 关闭保存文件
    void close_save_file();

    // 定时发送（参数：IP、端口、时长(秒)、通道数、每秒包数）
    bool start_timed(const std::string& ip, uint16_t port, int duration_sec,
                    int channels, int packets_per_sec);
    // 连续发送（参数：IP、端口、通道数、每秒包数）
    bool start_continuous(const std::string& ip, uint16_t port,
                        int channels, int packets_per_sec);
    void pause_continuous();    // 暂停连续发送
    void resume_continuous();   // 恢复连续发送
    void send_terminate(const std::string& ip, uint16_t port);
    // 文件传输（参数：IP、端口、文件路径、通道数、每秒包数）
    bool start_file_transfer(const std::string& ip, uint16_t port,
                           const std::string& file_path, int channels, int packets_per_sec);
signals:
    void finished();
    void log_message(const QString& msg);

private:
    Logger& logger_;
    UdpSocket socket_;
    std::ofstream send_file_;   // 保存文件流
    std::string save_file_path_; // 保存文件路径
    // 修正：生成数据包的序号参数改为uint32_t
    Packet generate_data_packet(uint32_t seq, int channels);
    bool send_command(UdpCmd cmd);
    std::atomic<bool> is_paused_;
    std::atomic<bool> is_running_;
    // 修正：序号类型从uint16_t改为uint32_t
    uint32_t current_seq_ = 0;
    int current_channels_ = 1;        // 当前通道数
    int current_packets_per_sec_ = 1000;  // 当前每秒包数

    // 保存数据包到文件
    void save_packet_to_file(const Packet& pkt);
};
