// udp_receiver.h
#pragma once
#include "udp_socket.h"
#include "protocol.h"
#include "logger.h"
#include <QObject>
#include <QMutex>
#include <map>

struct ReceiverConfig {
    uint16_t port = 8888;
};

class UdpReceiver : public QObject {
    Q_OBJECT
public:
    UdpReceiver(Logger& logger) : logger_(logger) {}

    void start(const ReceiverConfig& config);

signals:
    void packet_received(const QString& info);
    void finished();

private:
    Logger& logger_;
    UdpSocket socket_;
    bool handle_packet(const Packet& pkt);
    // 序号改为uint32_t，存储映射同步更新
    std::map<uint32_t, std::vector<int16_t>> received_data_;
    QMutex data_mutex_;
    void save_received_data();  // 保存时需处理uint32_t序号
    void clear_data_cache();
};
