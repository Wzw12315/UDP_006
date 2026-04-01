#ifndef CRC16_H
#define CRC16_H

#include <cstdint>
#include <vector>
#include <cstddef>  // 新增：包含size_t的定义
class Crc16 {
public:
    // 计算CRC16校验值（Modbus协议标准）
    static uint16_t calculate(const uint8_t* data, size_t length) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < length; ++i) {
            crc ^= static_cast<uint16_t>(data[i]);
            for (int j = 0; j < 8; ++j) {
                if (crc & 0x0001) {
                    crc >>= 1;
                    crc ^= 0xA001;
                } else {
                    crc >>= 1;
                }
            }
        }
        return crc;
    }

    // 对int16_t向量计算CRC
    static uint16_t calculate(const std::vector<int16_t>& data) {
        return calculate(reinterpret_cast<const uint8_t*>(data.data()),
                        data.size() * sizeof(int16_t));
    }
};

#endif // CRC16_H
