// logger.cpp
#include "logger.h"
#include <iostream>
#include <ctime>
#include <iomanip>
#include <QMutex>
#include <sys/stat.h>   // 用于创建目录
#include <sys/types.h>  // 用于创建目录
#ifdef _WIN32
#include <direct.h>     // Windows系统创建目录
#define mkdir(path, mode) _mkdir(path)  // Windows兼容宏
#endif

static QMutex log_mutex;

Logger::Logger(int targets, const std::string& file_prefix)
    : targets_(targets), file_prefix_(file_prefix) {}

Logger::~Logger() {
    QMutexLocker locker(&log_mutex);
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

void Logger::create_new_log_file() {
    QMutexLocker locker(&log_mutex);
    if (targets_ & File) {
        if (log_file_.is_open()) {
            log_file_.close();
        }

        // 确保log文件夹存在
        const std::string log_dir = "log";
        #ifdef _WIN32
            // Windows系统创建目录（不检查是否存在，直接创建）
            mkdir(log_dir.c_str());
        #else
            // Linux/Mac系统创建目录（权限0755，仅当目录不存在时）
            struct stat st;
            if (stat(log_dir.c_str(), &st) == -1) {
                mkdir(log_dir.c_str(), 0755);  // 0755: 所有者读写执行，其他用户读执行
            }
        #endif

        // 生成带时间戳的文件名，路径为log/前缀_时间.txt
        auto now = std::time(nullptr);
        std::tm tm = *std::localtime(&now);
        char filename[64];
        std::strftime(filename, sizeof(filename), "%Y%m%d_%H%M%S", &tm);
        current_filename_ = log_dir + "/" + file_prefix_ + "_" + filename + ".txt";  // 保存到log文件夹
        log_file_.open(current_filename_, std::ios::app);
    }
}

void Logger::log(const std::string& message) {
    QMutexLocker locker(&log_mutex);
    auto now = std::time(nullptr);
    std::tm tm = *std::localtime(&now);
    char time_buf[20];
    std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm);
    std::string time_str = "[" + std::string(time_buf) + "] ";

    if (targets_ & Console) {
        std::cout << time_str << message << std::endl;
    }
    if (log_file_.is_open()) {
        log_file_ << time_str << message << std::endl;
    }
    if (targets_ & UI) {
        emit ui_log(QString::fromStdString(time_str + message));
    }
}

void Logger::log_packet(const std::string& packet_info) {
    log("[PACKET] " + packet_info);
}
