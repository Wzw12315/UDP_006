// main_receiver_window.h
#pragma once
#include <QWidget>
#include "udp_receiver.h"
#include <QThread>
#include "ui_main_receiver_window.h"

class ReceiverWorker : public QObject {
    Q_OBJECT
public:
    explicit ReceiverWorker(const ReceiverConfig& config, Logger& logger)
        : config_(config), receiver_(logger) {
        // 信号传递时保持序号为uint32_t
        connect(&receiver_, &UdpReceiver::packet_received, this, &ReceiverWorker::packetReceived);
    }

public slots:
    void doWork() {
        receiver_.start(config_);
    }

signals:
    void packetReceived(const QString& info);  // 信息中包含uint32_t序号
    void finished();

private:
    ReceiverConfig config_;
    UdpReceiver receiver_;
};

class MainReceiverWindow : public QWidget {
    Q_OBJECT
public:
    MainReceiverWindow(QWidget* parent = nullptr);
    ~MainReceiverWindow() override;

private slots:
    void on_startButton_clicked();
    void updatePacketInfo(const QString& info);  // 处理包含uint32_t序号的信息
    void appendLog(const QString& msg);

private:
    Ui::MainReceiverWindow* ui;
    Logger logger_;
    QThread worker_thread_;
};
