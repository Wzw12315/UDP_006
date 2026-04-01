#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>  // 添加这行，包含strerror函数的声明
class UdpSocket {
public:
    // 构造函数默认初始化，析构函数自动关闭socket
    UdpSocket() = default;
    ~UdpSocket();

    // 禁用拷贝构造和赋值（避免socket描述符重复关闭）
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // 移动构造和赋值（支持资源转移）
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    /**
     * 初始化发送端
     * @param ip 目标IP地址
     * @param port 目标端口
     * @return 初始化成功返回true
     */
    bool init_sender(const std::string& ip, uint16_t port);

    /**
     * 初始化接收端
     * @param port 本地监听端口
     * @param timeout_ms 接收超时时间(毫秒，-1表示阻塞)
     * @return 初始化成功返回true
     */
    bool init_receiver(uint16_t port, int timeout_ms = 1000);

    /**
     * 发送数据
     * @param data 待发送的数据
     * @return 成功发送的字节数，失败返回-1
     */
    ssize_t send(const std::vector<uint8_t>& data);

    /**
     * 发送数据到指定地址（覆盖初始化时的目标地址）
     * @param data 待发送的数据
     * @param ip 目标IP
     * @param port 目标端口
     * @return 成功发送的字节数，失败返回-1
     */
    ssize_t send_to(const std::vector<uint8_t>& data, const std::string& ip, uint16_t port);

    /**
     * 接收数据
     * @param buffer 接收缓冲区（需预先分配大小）
     * @param sender_ip 发送端IP（输出参数）
     * @param sender_port 发送端端口（输出参数）
     * @return 接收的字节数，超时/失败返回-1
     */
    ssize_t receive(std::vector<uint8_t>& buffer, std::string& sender_ip, uint16_t& sender_port);

    /**
     * 设置接收缓冲区大小
     * @param size 缓冲区大小(字节)
     * @return 设置成功返回true
     */
    bool set_receive_buffer_size(int size);

    /**
     * 设置发送缓冲区大小
     * @param size 缓冲区大小(字节)
     * @return 设置成功返回true
     */
    bool set_send_buffer_size(int size);

    /**
     * 启用/禁用广播模式
     * @param enable true-启用，false-禁用
     * @return 操作成功返回true
     */
    bool enable_broadcast(bool enable);

    /**
     * 加入多播组
     * @param group_ip 多播组IP
     * @param iface_ip 本地网卡IP（为空则自动选择）
     * @return 操作成功返回true
     */
    bool join_multicast_group(const std::string& group_ip, const std::string& iface_ip = "");

    /**
     * 检查socket是否有效
     * @return 有效返回true
     */
    bool is_valid() const { return sockfd_ != -1; }

    /**
     * 关闭socket
     */
    void close_socket();

    /**
     * 获取最后一次错误码
     * @return 错误码（0表示无错误）
     */
    int get_last_error() const { return last_error_; }

    /**
     * 获取最后一次错误信息
     * @return 错误描述字符串
     */
    std::string get_error_msg() const { return std::string(strerror(last_error_)); }

private:
    int sockfd_ = -1;                  // socket描述符
    struct sockaddr_in target_addr_ {}; // 目标地址（用于send()方法）
    int last_error_ = 0;               // 最后一次错误码

    // 重置错误状态
    void reset_error() { last_error_ = 0; }

    // 保存错误码
    void set_error(int err) { last_error_ = err; }
};
