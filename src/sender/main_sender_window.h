// main_sender_window.h
#pragma once
#include <QWidget>
#include "udp_sender.h"
#include <QThread>
#include "ui_main_sender_window.h"

// 发送模式枚举
enum class SendMode {
    Timed,      // 定时长模式
    Continuous, // 连续模式
    FileTransfer // 文件传输模式
};

class SenderWorker : public QObject {
    Q_OBJECT
public:
    SenderWorker(UdpSender* sender, const std::string& ip, uint16_t port, int duration,
                int channels, int packetsPerSec, bool terminate, SendMode mode, bool saveData)
        : sender_(sender), ip_(ip), port_(port), duration_(duration),
          channels_(channels), packetsPerSec_(packetsPerSec),
          terminate_(terminate), mode_(mode), saveData_(saveData) {}

public slots:
    void doWork() {
        if (terminate_) {
            sender_->send_terminate(ip_, port_);
        } else {
            // 初始化保存文件（如果需要）
            if (saveData_) {
                sender_->init_save_file();
            }

            if (mode_ == SendMode::Timed) {
                sender_->start_timed(ip_, port_, duration_, channels_, packetsPerSec_);
            } else {
                sender_->start_continuous(ip_, port_, channels_, packetsPerSec_);
            }

            // 关闭保存文件
            if (saveData_) {
                sender_->close_save_file();
            }
        }
    }

signals:
    void finished();

private:
    UdpSender* sender_;
    std::string ip_;
    uint16_t port_;
    int duration_;
    int channels_;
    int packetsPerSec_;
    bool terminate_;
    SendMode mode_;
    bool saveData_; // 是否保存数据
};

class MainSenderWindow : public QWidget {
    Q_OBJECT
public:
    MainSenderWindow(QWidget* parent = nullptr);
    ~MainSenderWindow() override;

private slots:
    void on_sendButton_clicked();
    void on_terminateButton_clicked();
    void on_pauseButton_clicked();
    void appendLog(const QString& msg);
    void on_modeComboBox_currentIndexChanged(int index);
    void on_selectFileButton_clicked();
    void on_fileTransferButton_clicked();

private:
    Ui::MainSenderWindow* ui;
    Logger logger_;
    QThread* worker_thread_ = nullptr;
    UdpSender* current_sender_ = nullptr;
    bool is_paused_ = false;
    QString selected_file_path_;
};
