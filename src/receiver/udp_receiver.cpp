// udp_receiver.cpp
#include "udp_receiver.h"
#include <chrono>
#include <thread>
#include <iomanip>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

using namespace std;

void UdpReceiver::start(const ReceiverConfig& config) {
    if (!socket_.init_receiver(config.port)) {
        logger_.log("receiver init failed on port: " + to_string(config.port));
        emit finished();
        return;
    }

    logger_.log("receiver started on port: " + to_string(config.port));
    // 缓冲区大小：15字节包头 + 最大数据体（2048通道*2字节）
    vector<uint8_t> buffer(15 + 2048 * 2);
    int state = 0; // 0:等待准备, 1:接收数据, 2:单次结束
    bool running = true;

    while (running) {
        string sender_ip;
        uint16_t sender_port;
        ssize_t len = socket_.receive(buffer, sender_ip, sender_port);

        if (len <= 0) continue;

        Packet pkt;
        if (!pkt.deserialize(buffer)) {
            logger_.log("invalid packet from " + sender_ip + ":" + to_string(sender_port));
            continue;
        }

        if (!ProtocolUtil::check_magic(pkt.magic)) {
            logger_.log("invalid magic number");
            continue;
        }

        switch (pkt.cmd) {
            case UdpCmd::Prepare:
                logger_.log("received prepare command");
                logger_.create_new_log_file();
                state = 1;
                clear_data_cache();
                break;
            case UdpCmd::Data:
                if (state == 1) {
                    if (handle_packet(pkt)) {
                        // 日志和信号中显示uint32_t序号
                        emit packet_received(QString("序号: %1, 时间戳: %2, 通道数: %3")
                            .arg((uint32_t)pkt.seq)  // 显式转换确保正确
                            .arg(pkt.timestamp)
                            .arg(pkt.data_count));
                    }
                }
                break;
            case UdpCmd::Finish:
                logger_.log("received finish command");
                state = 2;
                save_received_data();
                state = 0;
                logger_.log("ready for next data transmission");
                break;
            case UdpCmd::Terminate:
                logger_.log("received terminate command");
                running = false;
                save_received_data();
                break;
        }
    }

    socket_.close_socket();
    emit finished();
}

bool UdpReceiver::handle_packet(const Packet& pkt) {
    // CRC校验
    uint16_t calc_crc = ProtocolUtil::calculate_crc(pkt.data_body);
    if (calc_crc != pkt.crc) {
        logger_.log("CRC mismatch, seq: " + to_string((uint32_t)pkt.seq));  // 适配uint32_t
        return false;
    }

    QMutexLocker locker(&data_mutex_);
    received_data_[pkt.seq] = pkt.data_body;  // 直接存储uint32_t序号

    logger_.log_packet("seq: " + to_string((uint32_t)pkt.seq) +  // 日志输出uint32_t
                     ", data count: " + to_string(pkt.data_count) +
                     ", last data: " + to_string(pkt.data_body.back()));
    return true;
}

// 修正：保存时以uint32_t网络字节序写入序号
void UdpReceiver::save_received_data() {
    QMutexLocker locker(&data_mutex_);
    if (received_data_.empty()) {
        logger_.log("No data to save");
        return;
    }

#ifdef _WIN32
    CreateDirectoryA("receive_data", nullptr);
#else
    mkdir("receive_data", 0755);
#endif

    auto now = std::time(nullptr);
    std::tm tm = *std::localtime(&now);
    char filename[128];
    std::strftime(filename, sizeof(filename), "receive_data/received_data_%Y%m%d_%H%M%S.bin", &tm);

    std::ofstream outfile(filename, std::ios::binary);
    if (!outfile.is_open()) {
        logger_.log("Failed to open file for saving data: " + std::string(filename));
        return;
    }

    // 遍历所有数据包并写入文件（序号为uint32_t）
    for (const auto& [seq, data] : received_data_) {
        // 写入序号（uint32_t -> 网络字节序，使用htonl）
        uint32_t seq_net = htonl(seq);  // 关键修正：从htons改为htonl
        outfile.write(reinterpret_cast<const char*>(&seq_net), sizeof(seq_net));  // 长度4字节

        // 写入数据数量（网络字节序）
        uint16_t data_count_net = htons(static_cast<uint16_t>(data.size()));
        outfile.write(reinterpret_cast<const char*>(&data_count_net), sizeof(data_count_net));

        // 写入数据（保持不变）
        for (const auto& val : data) {
            int16_t val_net = htons(val);
            outfile.write(reinterpret_cast<const char*>(&val_net), sizeof(val_net));
        }
    }

    outfile.close();
    logger_.log("Data saved to " + std::string(filename) +
               " (" + std::to_string(received_data_.size()) + " packets)");
}

void UdpReceiver::clear_data_cache() {
    QMutexLocker locker(&data_mutex_);
    received_data_.clear();
    logger_.log("data cache cleared");
}
