#include "udp_socket.h"
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>

UdpSocket::~UdpSocket() {
    close_socket();
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : sockfd_(other.sockfd_), target_addr_(other.target_addr_), last_error_(other.last_error_) {
    other.sockfd_ = -1; // 转移所有权后，原对象不再持有socket
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close_socket(); // 释放当前资源
        sockfd_ = other.sockfd_;
        target_addr_ = other.target_addr_;
        last_error_ = other.last_error_;
        other.sockfd_ = -1;
    }
    return *this;
}

bool UdpSocket::init_sender(const std::string& ip, uint16_t port) {
    reset_error();
    close_socket(); // 关闭已有socket

    // 创建UDP socket
    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ == -1) {
        set_error(errno);
        return false;
    }

    // 初始化目标地址
    memset(&target_addr_, 0, sizeof(target_addr_));
    target_addr_.sin_family = AF_INET;
    target_addr_.sin_port = htons(port);

    // 转换IP地址
    if (inet_pton(AF_INET, ip.c_str(), &target_addr_.sin_addr) <= 0) {
        set_error(errno);
        close_socket();
        return false;
    }

    return true;
}

bool UdpSocket::init_receiver(uint16_t port, int timeout_ms) {
    reset_error();
    close_socket(); // 关闭已有socket

    // 创建UDP socket
    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ == -1) {
        set_error(errno);
        return false;
    }

    // 绑定本地地址
    struct sockaddr_in local_addr {};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY; // 监听所有网卡
    local_addr.sin_port = htons(port);

    if (bind(sockfd_, (struct sockaddr*)&local_addr, sizeof(local_addr)) == -1) {
        set_error(errno);
        close_socket();
        return false;
    }

    // 设置接收超时
    if (timeout_ms >= 0) {
        struct timeval timeout;
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        if (setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            set_error(errno);
            close_socket();
            return false;
        }
    }

    return true;
}

ssize_t UdpSocket::send(const std::vector<uint8_t>& data) {
    if (!is_valid()) {
        set_error(EBADF);
        return -1;
    }
    reset_error();

    ssize_t sent = sendto(
        sockfd_,
        data.data(),
        data.size(),
        0,
        (struct sockaddr*)&target_addr_,
        sizeof(target_addr_)
    );

    if (sent == -1) {
        set_error(errno);
    }
    return sent;
}

ssize_t UdpSocket::send_to(const std::vector<uint8_t>& data, const std::string& ip, uint16_t port) {
    if (!is_valid()) {
        set_error(EBADF);
        return -1;
    }
    reset_error();

    struct sockaddr_in temp_addr {};
    temp_addr.sin_family = AF_INET;
    temp_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &temp_addr.sin_addr) <= 0) {
        set_error(errno);
        return -1;
    }

    ssize_t sent = sendto(
        sockfd_,
        data.data(),
        data.size(),
        0,
        (struct sockaddr*)&temp_addr,
        sizeof(temp_addr)
    );

    if (sent == -1) {
        set_error(errno);
    }
    return sent;
}

ssize_t UdpSocket::receive(std::vector<uint8_t>& buffer, std::string& sender_ip, uint16_t& sender_port) {
    if (!is_valid() || buffer.empty()) {
        set_error(EBADF);
        return -1;
    }
    reset_error();

    struct sockaddr_in sender_addr {};
    socklen_t addr_len = sizeof(sender_addr);
    ssize_t len = recvfrom(
        sockfd_,
        buffer.data(),
        buffer.size(),
        0,
        (struct sockaddr*)&sender_addr,
        &addr_len
    );

    if (len > 0) {
        sender_ip = inet_ntoa(sender_addr.sin_addr);
        sender_port = ntohs(sender_addr.sin_port);
    } else if (len == -1) {
        set_error(errno);
    }

    return len;
}

bool UdpSocket::set_receive_buffer_size(int size) {
    if (!is_valid() || size <= 0) {
        set_error(EBADF);
        return false;
    }
    reset_error();

    if (setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) < 0) {
        set_error(errno);
        return false;
    }
    return true;
}

bool UdpSocket::set_send_buffer_size(int size) {
    if (!is_valid() || size <= 0) {
        set_error(EBADF);
        return false;
    }
    reset_error();

    if (setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) < 0) {
        set_error(errno);
        return false;
    }
    return true;
}

bool UdpSocket::enable_broadcast(bool enable) {
    if (!is_valid()) {
        set_error(EBADF);
        return false;
    }
    reset_error();

    int opt = enable ? 1 : 0;
    if (setsockopt(sockfd_, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        set_error(errno);
        return false;
    }
    return true;
}

bool UdpSocket::join_multicast_group(const std::string& group_ip, const std::string& iface_ip) {
    if (!is_valid()) {
        set_error(EBADF);
        return false;
    }
    reset_error();

    struct ip_mreq mreq {};
    // 设置多播组IP
    if (inet_pton(AF_INET, group_ip.c_str(), &mreq.imr_multiaddr) <= 0) {
        set_error(errno);
        return false;
    }

    // 设置本地网卡IP（为空则使用默认网卡）
    if (iface_ip.empty()) {
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    } else {
        if (inet_pton(AF_INET, iface_ip.c_str(), &mreq.imr_interface) <= 0) {
            set_error(errno);
            return false;
        }
    }

    // 加入多播组
    if (setsockopt(sockfd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        set_error(errno);
        return false;
    }
    return true;
}

void UdpSocket::close_socket() {
    if (sockfd_ != -1) {
        ::close(sockfd_);
        sockfd_ = -1;
    }
}
