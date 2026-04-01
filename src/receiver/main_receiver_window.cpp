// main_receiver_window.cpp
#include "main_receiver_window.h"

MainReceiverWindow::MainReceiverWindow(QWidget* parent)
    : QWidget(parent), ui(new Ui::MainReceiverWindow),
      logger_(Logger::Console | Logger::File | Logger::UI, "receive") {
    ui->setupUi(this);
    ui->portSpinBox->setValue(8888);
    ui->portSpinBox->setRange(1024, 65535);
    connect(&logger_, &Logger::ui_log, this, &MainReceiverWindow::appendLog);
}

MainReceiverWindow::~MainReceiverWindow() {
    if (worker_thread_.isRunning()) {
        worker_thread_.quit();
        worker_thread_.wait(5000);
    }
    delete ui;
}

void MainReceiverWindow::on_startButton_clicked() {
    if (worker_thread_.isRunning()) {
        worker_thread_.quit();
        if (!worker_thread_.wait(3000)) {
            worker_thread_.terminate();
            worker_thread_.wait();
        }
    }

    ReceiverConfig config;
    config.port = ui->portSpinBox->value();

    auto* worker = new ReceiverWorker(config, logger_);
    worker->moveToThread(&worker_thread_);

    connect(&worker_thread_, &QThread::started, worker, &ReceiverWorker::doWork);
    // 接收包含uint32_t序号的信息并显示
    connect(worker, &ReceiverWorker::packetReceived, this, &MainReceiverWindow::updatePacketInfo);
    connect(worker, &ReceiverWorker::finished, worker, &QObject::deleteLater);
    connect(worker, &ReceiverWorker::finished, &worker_thread_, &QThread::quit);

    worker_thread_.start();
    appendLog("接收端已启动，监听端口: " + QString::number(config.port));
}

// 显示包含uint32_t序号的数据包信息
void MainReceiverWindow::updatePacketInfo(const QString& info) {
    ui->packetTextEdit->append(info);
    ui->packetTextEdit->moveCursor(QTextCursor::End);
}

void MainReceiverWindow::appendLog(const QString& msg) {
    ui->logTextEdit->append(msg);
    ui->logTextEdit->moveCursor(QTextCursor::End);
}
