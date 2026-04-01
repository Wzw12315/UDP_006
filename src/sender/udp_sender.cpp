// udp_sender.cpp
#include "udp_sender.h"
#include <chrono>
#include <thread>
#include <random>
#include <fstream>
#include <ctime>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

// 初始化保存文件
bool UdpSender::init_save_file() {
    // 创建保存目录
#ifdef _WIN32
    _mkdir("send_data");
#else
    mkdir("send_data", 0755);
#endif

    // 生成带时间戳的文件名
    auto now = std::time(nullptr);
    std::tm tm = *std::localtime(&now);
    char filename[128];
    std::strftime(filename, sizeof(filename), "send_data/sent_data_%Y%m%d_%H%M%S.bin", &tm);

    send_file_.open(filename, std::ios::binary);
    if (!send_file_.is_open()) {
        logger_.log("Failed to create send file: " + std::string(filename));
        return false;
    }
    save_file_path_ = filename;
    logger_.log("Send data will be saved to: " + save_file_path_);
    return true;
}

// 关闭保存文件
void UdpSender::close_save_file() {
    if (send_file_.is_open()) {
        send_file_.close();
        logger_.log("Send data saved to: " + save_file_path_);
    }
}

// 保存数据包到文件
void UdpSender::save_packet_to_file(const Packet& pkt) {
    if (!send_file_.is_open()) return;

    // 修正：序号类型改为uint32_t，使用htonl转换字节序
    uint32_t seq_net = htonl(pkt.seq);  // 32位字节序转换
    send_file_.write(reinterpret_cast<const char*>(&seq_net), sizeof(seq_net));  // 写入4字节

    // 写入数据数量（网络字节序）
    uint16_t data_count_net = htons(static_cast<uint16_t>(pkt.data_body.size()));
    send_file_.write(reinterpret_cast<const char*>(&data_count_net), sizeof(data_count_net));

    // 写入数据
    for (const auto& val : pkt.data_body) {
        int16_t val_net = htons(val);
        send_file_.write(reinterpret_cast<const char*>(&val_net), sizeof(val_net));
    }
}

using namespace std;
using namespace chrono;

bool UdpSender::start_timed(const string& ip, uint16_t port, int duration_sec,
                           int channels, int packets_per_sec) {
    if (!socket_.init_sender(ip, port)) {
        logger_.log("sender init failed");
        return false;
    }

    current_channels_ = channels;
    current_packets_per_sec_ = packets_per_sec;

    // 发送准备指令
    if (!send_command(UdpCmd::Prepare)) {
        logger_.log("send prepare command failed");
        return false;
    }
    this_thread::sleep_for(seconds(1));

    logger_.log("start sending data for " + to_string(duration_sec) + "s");
    logger_.log("channels: " + to_string(channels) + ", packets per second: " + to_string(packets_per_sec));

    auto start_time = steady_clock::now();
    auto end_time = start_time + seconds(duration_sec);  // 精确计算结束时间点
    // 修正：序号类型从uint16_t改为uint32_t，移除取模操作
    uint32_t seq = 0;
    uint32_t total_packets = 0;
    is_running_ = true;

    // 计算每包间隔（纳秒）
    int64_t interval_ns = 1000000000LL / packets_per_sec;

    while (is_running_) {
        // 检查是否已超过总时长
        if (steady_clock::now() > end_time) {
            break;
        }

        // 生成并发送数据包
        Packet pkt = generate_data_packet(seq, channels);
        auto data = pkt.serialize();
        ssize_t sent = socket_.send(data);
        if (sent > 0) {
            total_packets++;
            save_packet_to_file(pkt); // 保存数据包
        }
        // 修正：序号直接递增（uint32_t自动溢出）
        seq++;

        // 计算下一包的理论发送时间
        auto next_send_time = start_time + nanoseconds(total_packets * interval_ns);
        auto now = steady_clock::now();

        // 若未到下一包发送时间则等待，否则直接发送（追赶模式）
        if (next_send_time > now) {
            this_thread::sleep_for(next_send_time - now);
        }

        // 二次检查是否超时
        if (steady_clock::now() > end_time) {
            break;
        }
    }

    // 补充逻辑：确保发送数量准确
    uint32_t expected_packets = static_cast<uint32_t>(packets_per_sec) * duration_sec;
    if (total_packets < expected_packets) {
        Packet pkt = generate_data_packet(seq, channels);
        auto data = pkt.serialize();
        if (socket_.send(data) > 0) {
            total_packets++;
        }
    }

    // 发送结束指令
    send_command(UdpCmd::Finish);
    logger_.log("send completed, total packets: " + to_string(total_packets));
    is_running_ = false;
    emit finished();
    return true;
}

bool UdpSender::start_continuous(const string& ip, uint16_t port,
                                int channels, int packets_per_sec) {
    if (!socket_.init_sender(ip, port)) {
        logger_.log("sender init failed");
        return false;
    }

    current_channels_ = channels;
    current_packets_per_sec_ = packets_per_sec;

    // 发送准备指令
    if (!send_command(UdpCmd::Prepare)) {
        logger_.log("send prepare command failed");
        return false;
    }
    this_thread::sleep_for(seconds(1));

    logger_.log("start continuous data sending");
    logger_.log("channels: " + to_string(channels) + ", packets per second: " + to_string(packets_per_sec));

    uint32_t total_packets = 0;
    is_running_ = true;
    is_paused_ = false;

    // 计算每包间隔（纳秒）
    int64_t interval_ns = 1000000000LL / packets_per_sec;
    auto start_time = steady_clock::now();

    while (is_running_) {
        // 处理暂停逻辑
        while (is_paused_ && is_running_) {
            this_thread::sleep_for(milliseconds(100));
            // 暂停后恢复时校准起始时间，避免集中发送
            start_time = steady_clock::now() - nanoseconds(total_packets * interval_ns);
        }
        if (!is_running_) break;

        // 计算当前应发包数
        auto now = steady_clock::now();
        int64_t elapsed_ns = duration_cast<nanoseconds>(now - start_time).count();
        uint64_t expected_packets = static_cast<uint64_t>(elapsed_ns / interval_ns);

        // 发送滞后的包
        while (total_packets < expected_packets && is_running_ && !is_paused_) {
            Packet pkt = generate_data_packet(current_seq_, channels);
            auto data = pkt.serialize();
            ssize_t sent = socket_.send(data);

            if (sent <= 0) {
                logger_.log("send failed, seq: " + to_string(current_seq_));
            } else {
                total_packets++;
                save_packet_to_file(pkt); // 保存数据包
            }
            // 修正：序号直接递增（uint32_t自动溢出）
            current_seq_++;
        }

        // 计算下一包发送时间并等待
        auto next_send_time = start_time + nanoseconds((total_packets + 1) * interval_ns);
        auto sleep_time = next_send_time - now;
        if (sleep_time > nanoseconds::zero()) {
            this_thread::sleep_for(sleep_time);
        }
    }

    // 发送结束指令
    send_command(UdpCmd::Finish);
    logger_.log("continuous send stopped, total packets: " + to_string(total_packets));
    emit finished();
    return true;
}

void UdpSender::pause_continuous() {
    if (is_running_) {
        is_paused_ = true;
        logger_.log("continuous sending paused");
    }
}

void UdpSender::resume_continuous() {
    is_paused_ = false;
    logger_.log("continuous sending resumed");
}

void UdpSender::send_terminate(const string& ip, uint16_t port) {
    UdpSocket temp_socket;
    if (temp_socket.init_sender(ip, port)) {
        Packet pkt;
        pkt.cmd = UdpCmd::Terminate;
        temp_socket.send(pkt.serialize());
        logger_.log("terminate command sent");
    } else {
        logger_.log("failed to send terminate command");
    }
    is_running_ = false;
    emit finished();
}

// 修正：参数seq类型从uint16_t改为uint32_t
Packet UdpSender::generate_data_packet(uint32_t seq, int channels) {
    Packet pkt;
    pkt.cmd = UdpCmd::Data;
    pkt.timestamp = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    pkt.seq = seq;  // 直接使用32位序号
    pkt.data_count = channels;

    // 生成随机数据
    static mt19937 rng(system_clock::now().time_since_epoch().count());
    static uniform_int_distribution<int16_t> dist(-32768, 32767);
    pkt.data_body.resize(channels);
    for (auto& val : pkt.data_body) {
        val = dist(rng);
    }

    pkt.crc = ProtocolUtil::calculate_crc(pkt.data_body);
    return pkt;
}

bool UdpSender::send_command(UdpCmd cmd) {
    Packet pkt;
    pkt.cmd = cmd;
    pkt.timestamp = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    auto data = pkt.serialize();
    return socket_.send(data) == (ssize_t)data.size();
}

bool UdpSender::start_file_transfer(const std::string& ip, uint16_t port,
                                   const std::string& file_path, int channels, int packets_per_sec) {
    if (!socket_.init_sender(ip, port)) {
        logger_.log("sender init failed");
        return false;
    }

    current_channels_ = channels;
    current_packets_per_sec_ = packets_per_sec;

    // 发送准备指令
    if (!send_command(UdpCmd::Prepare)) {
        logger_.log("send prepare command failed");
        return false;
    }
    this_thread::sleep_for(seconds(1));

    // 尝试打开文件
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        logger_.log("Failed to open file: " + file_path);
        return false;
    }

    logger_.log("Starting file transfer: " + file_path);
    logger_.log("channels: " + to_string(channels) + ", packets per second: " + to_string(packets_per_sec));

    is_running_ = true;
    is_paused_ = false;
    uint32_t total_packets = 0;
    current_seq_ = 0;  // 修正：使用uint32_t序号

    // 计算每包间隔（纳秒）
    int64_t interval_ns = 1000000000LL / packets_per_sec;
    auto start_time = steady_clock::now();

    // 文件传输逻辑（读取文件并分包发送）
    std::vector<int16_t> buffer(channels);
    while (is_running_ && !is_paused_) {
        // 读取文件数据（每次读取channels个int16_t）
        file.read(reinterpret_cast<char*>(buffer.data()), channels * sizeof(int16_t));
        std::streamsize read_count = file.gcount() / sizeof(int16_t);

        if (read_count <= 0) break;  // 文件读取完毕

        // 生成数据包
        Packet pkt;
        pkt.cmd = UdpCmd::Data;
        pkt.timestamp = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
        pkt.seq = current_seq_++;  // 修正：32位序号递增
        pkt.data_count = static_cast<uint16_t>(read_count);
        pkt.data_body = std::vector<int16_t>(buffer.begin(), buffer.begin() + read_count);
        pkt.crc = ProtocolUtil::calculate_crc(pkt.data_body);

        // 发送数据包
        auto data = pkt.serialize();
        ssize_t sent = socket_.send(data);
        if (sent > 0) {
            total_packets++;
            save_packet_to_file(pkt);  // 保存数据包
        } else {
            logger_.log("file transfer send failed, seq: " + to_string(pkt.seq));
        }

        // 控制发送速率
        auto now = steady_clock::now();
        auto next_send_time = start_time + nanoseconds(total_packets * interval_ns);
        if (next_send_time > now) {
            this_thread::sleep_for(next_send_time - now);
        }
    }

    // 发送结束指令
    send_command(UdpCmd::Finish);
    logger_.log("file transfer completed, total packets: " + to_string(total_packets));
    is_running_ = false;
    emit finished();
    return true;
}
