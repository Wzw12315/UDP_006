// main_sender_window.cpp
#include "main_sender_window.h"
#include <QMessageBox>
#include <QFileDialog>

MainSenderWindow::MainSenderWindow(QWidget* parent)
    : QWidget(parent), ui(new Ui::MainSenderWindow),
      logger_(Logger::Console | Logger::File | Logger::UI, "send") {
    ui->setupUi(this);
    ui->ipLineEdit->setText("127.0.0.1");
    ui->portSpinBox->setValue(8888);
    ui->portSpinBox->setRange(1, 65535);
    ui->durationSpinBox->setValue(2);
    ui->durationSpinBox->setRange(1, 65535);

    // 添加通道数和每秒包数控制
    ui->channelsSpinBox->setValue(28);
    ui->channelsSpinBox->setRange(1, 2048);
    ui->packetsPerSecSpinBox->setValue(1000);
    ui->packetsPerSecSpinBox->setRange(1, 100000);

    // 初始化模式选择下拉框
    ui->modeComboBox->addItem("定时长模式");
    ui->modeComboBox->addItem("连续模式");
    ui->modeComboBox->addItem("文件传输模式");
    ui->durationSpinBox->setEnabled(true);

    // 初始化保存数据复选框状态
    ui->saveDataCheckBox->setEnabled(true);
    ui->saveDataCheckBox->setChecked(false);

    connect(&logger_, &Logger::ui_log, this, &MainSenderWindow::appendLog);
}

MainSenderWindow::~MainSenderWindow() {
    if (worker_thread_ && worker_thread_->isRunning()) {
        worker_thread_->quit();
        worker_thread_->wait();
    }
    delete ui;
}

void MainSenderWindow::on_modeComboBox_currentIndexChanged(int index) {
    bool isTimed = (index == 0);
    bool isContinuous = (index == 1);
    bool isFileTransfer = (index == 2);

    ui->durationSpinBox->setEnabled(isTimed);
    ui->pauseButton->setEnabled(isContinuous);
    ui->sendButton->setEnabled(isTimed || isContinuous);
    ui->channelsSpinBox->setEnabled(isTimed || isContinuous);
    ui->packetsPerSecSpinBox->setEnabled(isTimed || isContinuous);
    ui->saveDataCheckBox->setEnabled(isTimed || isContinuous || isFileTransfer); // 所有模式都可保存

    // 显示/隐藏文件传输相关控件
    ui->selectFileButton->setVisible(isFileTransfer);
    ui->filePathLineEdit->setVisible(isFileTransfer);
    ui->fileTransferButton->setVisible(isFileTransfer);
}

void MainSenderWindow::on_sendButton_clicked() {
    std::string ip = ui->ipLineEdit->text().toStdString();
    uint16_t port = ui->portSpinBox->value();
    int duration = ui->durationSpinBox->value();
    int channels = ui->channelsSpinBox->value();
    int packetsPerSec = ui->packetsPerSecSpinBox->value();
    bool saveData = ui->saveDataCheckBox->isChecked(); // 获取保存状态
    SendMode mode = (ui->modeComboBox->currentIndex() == 0) ? SendMode::Timed : SendMode::Continuous;

    // 处理连续模式的暂停/继续
    if (mode == SendMode::Continuous && is_paused_ && current_sender_) {
        appendLog("继续发送数据...");
        current_sender_->resume_continuous();
        is_paused_ = false;
        return;
    }

    // 停止当前运行的线程（如果存在）
    if (worker_thread_ && worker_thread_->isRunning()) {
        worker_thread_->quit();
        if (!worker_thread_->wait(3000)) {
            worker_thread_->terminate();
            worker_thread_->wait();
        }
        delete worker_thread_;
        worker_thread_ = nullptr;
        delete current_sender_;
        current_sender_ = nullptr;
    }

    // 创建新的日志文件
    logger_.create_new_log_file();

    // 创建新的线程和发送器
    worker_thread_ = new QThread(this);
    current_sender_ = new UdpSender(logger_);
    auto* worker = new SenderWorker(current_sender_, ip, port, duration, channels, packetsPerSec, false, mode, saveData);
    worker->moveToThread(worker_thread_);

    connect(worker_thread_, &QThread::started, worker, &SenderWorker::doWork);
    connect(worker, &SenderWorker::finished, worker, &QObject::deleteLater);
    connect(worker, &SenderWorker::finished, [this]() {
        current_sender_ = nullptr;
        is_paused_ = false;
    });
    connect(worker, &SenderWorker::finished, worker_thread_, &QThread::quit);
    connect(worker_thread_, &QThread::finished, worker_thread_, &QObject::deleteLater);

    worker_thread_->start();
    appendLog(mode == SendMode::Timed ? "开始定时发送数据..." : "开始连续发送数据...");
    appendLog(QString("通道数: %1, 每秒包数: %2").arg(channels).arg(packetsPerSec));
    if (saveData) {
        appendLog("已启用发送数据本地保存功能");
    }
    is_paused_ = false;
}

// 文件传输按钮点击事件
void MainSenderWindow::on_fileTransferButton_clicked() {
    if (selected_file_path_.isEmpty()) {
        QMessageBox::warning(this, tr("警告"), tr("请先选择要发送的文件"));
        return;
    }

    std::string ip = ui->ipLineEdit->text().toStdString();
    uint16_t port = ui->portSpinBox->value();
    int channels = ui->channelsSpinBox->value();
    int packetsPerSec = ui->packetsPerSecSpinBox->value();
    bool saveData = ui->saveDataCheckBox->isChecked(); // 文件传输模式也支持保存

    // 停止当前运行的线程
    if (worker_thread_ && worker_thread_->isRunning()) {
        worker_thread_->quit();
        if (!worker_thread_->wait(3000)) {
            worker_thread_->terminate();
            worker_thread_->wait();
        }
        delete worker_thread_;
        worker_thread_ = nullptr;
        delete current_sender_;
        current_sender_ = nullptr;
    }

    // 创建新的日志文件
    logger_.create_new_log_file();

    // 创建新的线程和发送器
    worker_thread_ = new QThread(this);
    current_sender_ = new UdpSender(logger_);

    // 初始化保存文件（如果需要）
    if (saveData) {
        current_sender_->init_save_file();
    }

    auto* worker = new QObject;
    connect(worker, &QObject::destroyed, worker, &QObject::deleteLater);
    connect(worker_thread_, &QThread::started, worker, [=]() {
        current_sender_->start_file_transfer(ip, port, selected_file_path_.toStdString(), channels, packetsPerSec);

        // 关闭保存文件
        if (saveData) {
            current_sender_->close_save_file();
        }
    });

    connect(worker, &QObject::destroyed, worker_thread_, &QThread::quit);
    connect(worker_thread_, &QThread::finished, worker_thread_, &QObject::deleteLater);
    connect(worker_thread_, &QThread::finished, [this]() {
        current_sender_ = nullptr;
        is_paused_ = false;
    });

    worker->moveToThread(worker_thread_);
    worker_thread_->start();
    appendLog("开始发送文件: " + selected_file_path_);
    appendLog(QString("通道数: %1, 每秒包数: %2").arg(channels).arg(packetsPerSec));
    if (saveData) {
        appendLog("已启用发送数据本地保存功能");
    }
}

void MainSenderWindow::on_terminateButton_clicked() {
    std::string ip = ui->ipLineEdit->text().toStdString();
    uint16_t port = ui->portSpinBox->value();
    bool saveData = ui->saveDataCheckBox->isChecked(); // 获取保存状态
    // 发送终止指令
    logger_.create_new_log_file();
    QThread* worker_thread = new QThread(this);
    auto* sender = new UdpSender(logger_);
    auto* worker = new SenderWorker(sender, ip, port, 0, 0, 0, true, SendMode::Timed,saveData);
    worker->moveToThread(worker_thread);

    connect(worker_thread, &QThread::started, worker, &SenderWorker::doWork);
    connect(worker, &SenderWorker::finished, worker, &QObject::deleteLater);
    connect(worker, &SenderWorker::finished, sender, &QObject::deleteLater);
    connect(worker, &SenderWorker::finished, worker_thread, &QThread::quit);
    connect(worker_thread, &QThread::finished, worker_thread, &QObject::deleteLater);
    connect(worker_thread, &QThread::finished, this, [this]() {
        if (worker_thread_ && worker_thread_->isRunning()) {
            worker_thread_->quit();
            worker_thread_->wait(5000);
        }
        is_paused_ = false;
        current_sender_ = nullptr;
        appendLog("终止指令已处理完成");
    });

    worker_thread->start();
    appendLog("发送终止指令...");
}

void MainSenderWindow::on_pauseButton_clicked() {
    if (current_sender_ && !is_paused_) {
        current_sender_->pause_continuous();
        is_paused_ = true;
        appendLog("已暂停发送数据");
    }
}

void MainSenderWindow::appendLog(const QString& msg) {
    ui->logTextEdit->append(msg);
    ui->logTextEdit->moveCursor(QTextCursor::End);
}

void MainSenderWindow::on_selectFileButton_clicked() {
    QString file_path = QFileDialog::getOpenFileName(
        this,
        tr("选择BIN文件"),
        "",
        tr("BIN Files (*.bin);;All Files (*)")
    );

    if (!file_path.isEmpty()) {
        selected_file_path_ = file_path;
        ui->filePathLineEdit->setText(file_path);
    }
}
