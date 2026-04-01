// logger.h
#pragma once
#include <string>
#include <fstream>
#include <QObject>

class Logger : public QObject {
    Q_OBJECT
public:
    enum OutputTarget { Console = 1 << 0, File = 1 << 1, UI = 1 << 2 };

    Logger(int targets = Console, const std::string& file_prefix = "log");
    ~Logger() override;

    void log(const std::string& message);
    void log_packet(const std::string& packet_info);
    void create_new_log_file();

signals:
    void ui_log(const QString& message);

private:
    int targets_;
    std::ofstream log_file_;
    std::string file_prefix_;
    std::string current_filename_;
};
