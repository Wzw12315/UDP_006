// protocol.cpp
#include "protocol.h"
#include "crc16.h"
#include <cstring>
#include <arpa/inet.h>

using namespace std;

vector<uint8_t> Packet::serialize() const {
    vector<uint8_t> bytes;
    bytes.reserve(sizeof(Packet) + data_body.size() * sizeof(int16_t));

    // 序列化包头（网络字节序）
    uint16_t magic_net = htons(magic);
    bytes.insert(bytes.end(), (uint8_t*)&magic_net, (uint8_t*)&magic_net + 2);

    bytes.push_back((uint8_t)cmd);

    uint32_t ts_net = htonl(timestamp);
    bytes.insert(bytes.end(), (uint8_t*)&ts_net, (uint8_t*)&ts_net + 4);

    uint16_t cnt_net = htons(data_count);
    bytes.insert(bytes.end(), (uint8_t*)&cnt_net, (uint8_t*)&cnt_net + 2);

    // 新代码：
    uint32_t seq_net = htonl(seq);  // 32位字节序转换
    bytes.insert(bytes.end(), (uint8_t*)&seq_net, (uint8_t*)&seq_net + 4);  // 长度改为4字节

    uint16_t crc_net = htons(crc);
    bytes.insert(bytes.end(), (uint8_t*)&crc_net, (uint8_t*)&crc_net + 2);

    // 序列化数据体（网络字节序）
    for (int16_t val : data_body) {
        int16_t val_net = htons(val);
        bytes.insert(bytes.end(), (uint8_t*)&val_net, (uint8_t*)&val_net + 2);
    }
    return bytes;
}

bool Packet::deserialize(const vector<uint8_t>& bytes) {
    if (bytes.size() < 13) return false; // 最小包头长度
    size_t pos = 0;

    // 解析包头
    uint16_t magic_net;
    memcpy(&magic_net, &bytes[pos], 2);
    magic = ntohs(magic_net);
    pos += 2;

    cmd = (UdpCmd)bytes[pos++];

    uint32_t ts_net;
    memcpy(&ts_net, &bytes[pos], 4);
    timestamp = ntohl(ts_net);
    pos += 4;

    uint16_t cnt_net;
    memcpy(&cnt_net, &bytes[pos], 2);
    data_count = ntohs(cnt_net);
    pos += 2;

    // 新代码：
    uint32_t seq_net;
    memcpy(&seq_net, &bytes[pos], 4);  // 读取4字节
    seq = ntohl(seq_net);  // 32位字节序转换
    pos += 4;

    uint16_t crc_net;
    memcpy(&crc_net, &bytes[pos], 2);
    crc = ntohs(crc_net);
    pos += 2;

    // 解析数据体
    data_body.resize(data_count);
    for (int i = 0; i < data_count; i++) {
        if (pos + 2 > bytes.size()) return false;
        int16_t val_net;
        memcpy(&val_net, &bytes[pos], 2);
        data_body[i] = ntohs(val_net);
        pos += 2;
    }
    return true;
}

uint16_t ProtocolUtil::calculate_crc(const vector<int16_t>& data) {
    return Crc16::calculate(data);
}

bool ProtocolUtil::check_magic(uint16_t magic) {
    return magic == 0xAA55;
}
