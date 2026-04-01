// protocol.h
#pragma once
#include <cstdint>
#include <vector>
#include <tuple>

enum class UdpCmd : uint8_t {
    Prepare = 0,
    Data = 1,
    Finish = 2,
    Terminate = 3
};

struct Packet {
    uint16_t magic = 0xAA55;       // 魔术字
    UdpCmd cmd;                    // 指令类型
    uint32_t timestamp = 0;        // 时间戳(秒)
    uint16_t data_count = 1;       // 数据个数(通道数)，默认为1
    uint32_t seq = 0;              // 改为32位无符号整数
    uint16_t crc = 0;              // CRC校验值
    std::vector<int16_t> data_body; // 数据体

    std::vector<uint8_t> serialize() const;
    bool deserialize(const std::vector<uint8_t>& bytes);
};

namespace ProtocolUtil {
    uint16_t calculate_crc(const std::vector<int16_t>& data);
    bool check_magic(uint16_t magic);
}
