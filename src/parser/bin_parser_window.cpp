#include "bin_parser_window.h"
#include "ui_bin_parser_window.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <fstream>
#include <arpa/inet.h>
#include <set>
#include <QFile>
#include <QTextStream>
#include <QApplication>
#include <QProgressDialog>
#include <QMutex>
#include <unordered_set>
#include <unordered_map>
BinParserWindow::BinParserWindow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::BinParserWindow) {
    ui->setupUi(this);
    setWindowTitle("UDP Bin文件解析工具");

    // 初始化表格
    ui->dataTableWidget->setColumnCount(5);
    ui->dataTableWidget->setHorizontalHeaderLabels({
        "序号", "接收CRC", "计算CRC", "CRC匹配", "数据预览"
    });
    ui->dataTableWidget->horizontalHeader()->setStretchLastSection(true);

    // 初始化对比表格
    ui->compareTableWidget->setColumnCount(4);
    ui->compareTableWidget->setHorizontalHeaderLabels({
        "序号", "文件1状态", "文件2状态", "对比结果"
    });
    ui->compareTableWidget->horizontalHeader()->setStretchLastSection(true);

    // 分页控件
    connect(ui->prevPageButton, &QPushButton::clicked, this, &BinParserWindow::on_prevPageButton_clicked);
    connect(ui->nextPageButton, &QPushButton::clicked, this, &BinParserWindow::on_nextPageButton_clicked);
}

BinParserWindow::~BinParserWindow() {
    reset_parse_state();
    delete ui;
}

void BinParserWindow::reset_parse_state() {
    // 保护临界区，避免并发访问
    static QMutex mutex;
    QMutexLocker locker(&mutex);

    if (parse_thread_ && parse_thread_->isRunning()) {
        // 通知工作线程终止
        if (parse_worker_) {
            parse_worker_->abort();
        }
        // 尝试优雅退出
        parse_thread_->quit();
        // 等待5秒，超时则强制终止
        if (!parse_thread_->wait(5000)) {
            parse_thread_->terminate();
            parse_thread_->wait();
        }
    }

    // 重置指针和状态
    parse_thread_ = nullptr;
    parse_worker_ = nullptr;
    is_parsing_ = false;

    // 恢复UI状态
    if (ui) {
        ui->parseButton->setEnabled(true);
        ui->cancelParseButton->setEnabled(false);
    }
}
void BinParserWindow::on_selectFileButton_clicked() {
    QString file_path = QFileDialog::getOpenFileName(
        this, "选择第一个bin文件", "./", "BIN文件 (*.bin);;所有文件 (*)"
    );
    if (!file_path.isEmpty()) {
        current_file_path_ = file_path;
        ui->filePathLineEdit->setText(file_path);
        parsed_data_.clear();
        ui->dataTableWidget->setRowCount(0);
        ui->statusLabel->setText("已选择第一个文件，请点击解析");
    }
}

void BinParserWindow::on_selectFile2Button_clicked() {
    QString file_path = QFileDialog::getOpenFileName(
        this, "选择第二个bin文件", "./", "BIN文件 (*.bin);;所有文件 (*)"
    );
    if (!file_path.isEmpty()) {
        current_file2_path_ = file_path;
        ui->filePathLineEdit2->setText(file_path);
        parsed_data2_.clear();
        ui->statusLabel->setText("已选择第二个文件，请点击解析");
    }
}

// 工作线程解析实现
bool ParseWorker::parse_chunk(QFile& file, qint64 file_size, qint64& read_size, int& parsed_count) {
    parsed_count = 0;
    // 每次解析max_chunk_size_个包作为一个块
    for (int i = 0; i < max_chunk_size_; ++i) {
        if (abort_) return false;
        if (file.atEnd()) return false;

        // 读取序号（4字节）
        uint32_t seq_net;
        qint64 read = file.read(reinterpret_cast<char*>(&seq_net), sizeof(seq_net));
        if (read != sizeof(seq_net)) return false;
        read_size += read;
        uint32_t seq = ntohl(seq_net);

        // 读取数据数量（2字节）
        uint16_t data_count_net;
        read = file.read(reinterpret_cast<char*>(&data_count_net), sizeof(data_count_net));
        if (read != sizeof(data_count_net)) return false;
        read_size += read;
        size_t data_size = ntohs(data_count_net);

        // 读取数据体
        std::vector<int16_t> packet_data(data_size);
        bool read_success = true;
        for (size_t j = 0; j < data_size; ++j) {
            int16_t val_net;
            read = file.read(reinterpret_cast<char*>(&val_net), sizeof(val_net));
            if (read != sizeof(val_net)) {
                read_success = false;
                break;
            }
            read_size += read;
            packet_data[j] = ntohs(val_net);
        }
        if (!read_success) return false;

        // 计算CRC
        uint16_t crc_calculated = ProtocolUtil::calculate_crc(packet_data);
        ParsedPacket pkt{seq, crc_calculated, crc_calculated, true, packet_data};
        data_.push_back(pkt);
        parsed_count++;

        // 定期更新进度，避免频繁触发UI更新
        if (i % 100 == 0) {
            int progress = static_cast<int>((read_size * 100) / file_size);
            emit progressUpdated(progress);
        }
    }
    return true;
}

void ParseWorker::doWork() {
    QFile file(file_path_);
    if (!file.open(QIODevice::ReadOnly)) {
        emit logMessage("无法打开文件: " + file.errorString());
        emit finished(false);
        return;
    }

    qint64 file_size = file.size();
    qint64 read_size = 0;
    data_.clear();

    try {
        while (!file.atEnd() && !abort_) {
            int parsed_count = 0;
            if (!parse_chunk(file, file_size, read_size, parsed_count)) {
                // 检查是否正常结束
                if (read_size == file_size) break;
                emit logMessage("文件格式错误，解析中断");
                file.close();
                emit finished(false);
                return;
            }

            // 通知主线程已解析的包数量
            if (parsed_count > 0) {
                emit chunkParsed(parsed_count);
            }

            // 允许UI处理事件，降低CPU占用
            QThread::msleep(10);
        }

        file.close();

        if (abort_) {
            emit logMessage("解析已取消");
            emit finished(false);
        } else {
            emit logMessage(QString("解析完成，共%1个数据包").arg(data_.size()));
            emit finished(true);
        }
    } catch (...) {
        file.close();
        emit logMessage("解析过程发生异常");
        emit finished(false);
    }
}

void BinParserWindow::on_parseButton_clicked() {
    if (current_file_path_.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select the first bin file first");
        return;
    }

    // 停止现有线程（确保资源释放）
    reset_parse_state();

    // 创建新线程和工作对象
    is_parsing_ = true;
    parse_thread_ = new QThread;
    parse_worker_ = new ParseWorker(current_file_path_, parsed_data_, 20000);  // 单次解析20000个包
    parse_worker_->moveToThread(parse_thread_);

    // 线程安全的信号槽连接（修正lambda连接方式）
    connect(parse_thread_, &QThread::started, parse_worker_, &ParseWorker::doWork, Qt::QueuedConnection);
    connect(parse_worker_, &ParseWorker::finished, this, &BinParserWindow::handleParseFinished, Qt::QueuedConnection);
    connect(parse_worker_, &ParseWorker::progressUpdated, this, &BinParserWindow::updateProgress, Qt::QueuedConnection);
    connect(parse_worker_, &ParseWorker::logMessage, this, &BinParserWindow::appendLog, Qt::QueuedConnection);
    connect(parse_worker_, &ParseWorker::chunkParsed, this, &BinParserWindow::handleChunkParsed, Qt::QueuedConnection);

    // 工作对象完成后自动删除
    connect(parse_worker_, &ParseWorker::finished, parse_worker_, &QObject::deleteLater, Qt::DirectConnection);
    // 工作对象完成后退出线程
    connect(parse_worker_, &ParseWorker::finished, parse_thread_, &QThread::quit, Qt::DirectConnection);
    // 线程完成后自动删除
    connect(parse_thread_, &QThread::finished, parse_thread_, &QObject::deleteLater, Qt::DirectConnection);

    // 修正：使用QObject::connect，显式指定上下文对象（this）
    connect(parse_thread_, &QThread::finished, this, [this]() {
        parse_thread_ = nullptr;
        parse_worker_ = nullptr;
    }, Qt::QueuedConnection);

    // 更新UI状态
    ui->parseButton->setEnabled(false);
    ui->cancelParseButton->setEnabled(true);
    ui->statusLabel->setText("Parsing file...");
    parse_thread_->start();
}

void BinParserWindow::on_cancelParseButton_clicked() {
    if (is_parsing_ && parse_worker_) {
        ui->statusLabel->setText("正在取消解析...");
        parse_worker_->abort();
        ui->cancelParseButton->setEnabled(false);
    }
}

void BinParserWindow::handleChunkParsed(int count) {
    // 每解析一定数量的包后更新状态，但不刷新整个表格，避免UI卡顿
    ui->statusLabel->setText(QString("已解析 %1 个数据包...").arg(parsed_data_.size()));
    QApplication::processEvents();
}

void BinParserWindow::handleParseFinished(bool success) {
    is_parsing_ = false;
    ui->parseButton->setEnabled(true);
    ui->cancelParseButton->setEnabled(false);

    if (success) {
        current_page_ = 0;
        display_parsed_data(current_page_);

        // 自动解析第二个文件（如果已选择且未解析）
        if (!current_file2_path_.isEmpty() && parsed_data2_.empty()) {
            QThread* thread2 = new QThread;
            ParseWorker* worker2 = new ParseWorker(current_file2_path_, parsed_data2_);
            worker2->moveToThread(thread2);

            connect(thread2, &QThread::started, worker2, &ParseWorker::doWork);
            connect(worker2, &ParseWorker::finished, worker2, &QObject::deleteLater);
            connect(worker2, &ParseWorker::finished, thread2, &QThread::quit);
            connect(thread2, &QThread::finished, thread2, &QObject::deleteLater);
            connect(worker2, &ParseWorker::logMessage, this, &BinParserWindow::appendLog);

            thread2->start();
        }
    } else {
        ui->statusLabel->setText("解析失败");
    }
}

void BinParserWindow::updateProgress(int percent) {
    ui->statusLabel->setText(QString("解析中... %1%").arg(percent));
    QApplication::processEvents(); // 刷新UI
}

void BinParserWindow::appendLog(QString msg) {
    // 限制日志数量，避免占用过多内存
    if (ui->logTextEdit->document()->blockCount() > 1000) {
        QTextCursor cursor = ui->logTextEdit->textCursor();
        cursor.movePosition(QTextCursor::Start);
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.removeSelectedText();
        cursor.deleteChar(); // 删除空行
    }
    ui->logTextEdit->append(msg);
    ui->logTextEdit->moveCursor(QTextCursor::End);
}

// 分页显示实现
void BinParserWindow::display_parsed_data(int page) {
    const int total_pages = (parsed_data_.size() + PAGE_SIZE - 1) / PAGE_SIZE;
    if (page < 0) page = 0;
    if (page >= total_pages && total_pages > 0) page = total_pages - 1;
    current_page_ = page;

    const int start = page * PAGE_SIZE;
    const int end = qMin(start + PAGE_SIZE, (int)parsed_data_.size());

    // 优化表格更新：先清除现有内容
    ui->dataTableWidget->setRowCount(0);
    ui->dataTableWidget->setRowCount(end - start);

    for (int i = start; i < end; ++i) {
        const auto &pkt = parsed_data_[i];
        const int row = i - start;

        // 使用QTableWidgetItem的优化构造方式
        auto *seq_item = new QTableWidgetItem(QString::number(pkt.seq));
        seq_item->setFlags(seq_item->flags() & ~Qt::ItemIsEditable);
        ui->dataTableWidget->setItem(row, 0, seq_item);

        auto *rcv_crc_item = new QTableWidgetItem(QString::number(pkt.crc_received, 16));
        rcv_crc_item->setFlags(rcv_crc_item->flags() & ~Qt::ItemIsEditable);
        ui->dataTableWidget->setItem(row, 1, rcv_crc_item);

        auto *calc_crc_item = new QTableWidgetItem(QString::number(pkt.crc_calculated, 16));
        calc_crc_item->setFlags(calc_crc_item->flags() & ~Qt::ItemIsEditable);
        ui->dataTableWidget->setItem(row, 2, calc_crc_item);

        auto *match_item = new QTableWidgetItem(pkt.crc_match ? "是" : "否");
        match_item->setBackground(pkt.crc_match ? Qt::green : Qt::red);
        match_item->setFlags(match_item->flags() & ~Qt::ItemIsEditable);
        ui->dataTableWidget->setItem(row, 3, match_item);

        // 优化数据预览生成
        QString data_preview = QString("总数: %1, 数据: ").arg(pkt.data.size());
        const int preview_count = qMin(3, (int)pkt.data.size());
        for (int j = 0; j < preview_count; ++j) {
            data_preview += QString::number(pkt.data[j]);
            if (j < preview_count - 1) data_preview += ", ";
        }
        if (pkt.data.size() > 3) data_preview += "...";

        auto *data_item = new QTableWidgetItem(data_preview);
        data_item->setFlags(data_item->flags() & ~Qt::ItemIsEditable);
        ui->dataTableWidget->setItem(row, 4, data_item);
    }

    ui->pageInfoLabel->setText(QString("第 %1/%2 页").arg(page + 1).arg(total_pages));
    ui->prevPageButton->setEnabled(page > 0);
    ui->nextPageButton->setEnabled(page < total_pages - 1);
}

void BinParserWindow::on_prevPageButton_clicked() {
    display_parsed_data(current_page_ - 1);
}

void BinParserWindow::on_nextPageButton_clicked() {
    display_parsed_data(current_page_ + 1);
}

// CRC验证
void BinParserWindow::verify_crc_and_highlight() {
    if (parsed_data_.empty()) {
        QMessageBox::warning(this, "提示", "请先解析文件");
        return;
    }

    // 添加进度显示
    QProgressDialog progress("正在验证CRC...", "取消", 0, parsed_data_.size(), this);
    progress.setWindowTitle("验证中");
    progress.setWindowModality(Qt::WindowModal);
    progress.setValue(0);

    int error_count = 0;
    for (size_t i = 0; i < parsed_data_.size(); ++i) {
        if (progress.wasCanceled()) break;

        auto &pkt = parsed_data_[i];
        pkt.crc_calculated = ProtocolUtil::calculate_crc(pkt.data);
        pkt.crc_match = (pkt.crc_received == pkt.crc_calculated);
        if (!pkt.crc_match) error_count++;

        // 定期更新进度
        if (i % 100 == 0) {
            progress.setValue(i);
            QApplication::processEvents();
        }
    }
    progress.setValue(parsed_data_.size());

    display_parsed_data(current_page_); // 刷新当前页显示

    if (error_count == 0) {
        QMessageBox::information(this, "验证结果", "所有数据包CRC校验通过");
    } else {
        QMessageBox::warning(this, "验证结果",
            QString("发现 %1 个CRC校验错误的数据包").arg(error_count));
    }
}

// 其他函数实现...
void BinParserWindow::on_verifyButton_clicked() {
    verify_crc_and_highlight();
}

void BinParserWindow::on_compareButton_clicked() {
    compare_files();
}

bool BinParserWindow::convert_to_csv(const QString &bin_file_path, const std::vector<ParsedPacket>& data) {
    if (data.empty()) {
        appendLog("No data to export to CSV");
        return false;
    }

    // 获取实际通道数（从第一个有效数据包提取）
    int channel_count = 0;
    for (const auto& pkt : data) {
        if (!pkt.data.empty()) {
            channel_count = pkt.data.size();
            break;
        }
    }
    if (channel_count <= 0) {
        appendLog("Invalid channel count, cannot export CSV");
        return false;
    }

    // 提取原始BIN文件名作为CSV预设名称（保持路径一致）
    QFileInfo bin_file_info(bin_file_path);
    QString default_csv_path = bin_file_info.dir().absoluteFilePath(bin_file_info.baseName() + ".csv");

    // 打开文件保存对话框（预设文件名）
    QString csv_path = QFileDialog::getSaveFileName(
        this, "Export to CSV", default_csv_path,
        "CSV Files (*.csv);;All Files (*.*)"
    );

    if (csv_path.isEmpty()) {
        appendLog("CSV export cancelled");
        return false;
    }

    // 打开CSV文件准备写入
    QFile csv_file(csv_path);
    if (!csv_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        appendLog("Failed to open CSV file: " + csv_file.errorString());
        return false;
    }

    QTextStream out(&csv_file);
    out.setCodec("UTF-8");  // 设置编码，避免中文乱码（虽然要求无中文，但确保兼容性）

    // 写入表头：chan_1,chan_2,...,chan_n（无中文）
    for (int i = 0; i < channel_count; ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "chan_" << (i + 1);
    }
    out << "\n";  // 换行

    // 分批写入数据（避免大文件占用过多内存）
    const int batch_size = 2000;
    int total_written = 0;
    for (size_t i = 0; i < data.size(); i += batch_size) {
        // 检查是否需要取消（如果正在解析，终止导出）
        if (is_parsing_) {
            appendLog("CSV export interrupted: Parsing in progress");
            csv_file.close();
            return false;
        }

        // 计算当前批次的结束位置
        size_t batch_end = qMin(i + batch_size, data.size());
        for (size_t j = i; j < batch_end; ++j) {
            const auto& pkt = data[j];
            // 确保当前数据包通道数与表头一致
            if (pkt.data.size() != channel_count) {
                appendLog(QString("Warning: Packet %1 channel count mismatch, skipped").arg(pkt.seq));
                continue;
            }

            // 写入当前行数据
            for (size_t k = 0; k < pkt.data.size(); ++k) {
                if (k > 0) {
                    out << ",";
                }
                out << pkt.data[k];
            }
            out << "\n";
            total_written++;
        }

        // 更新进度（每批更新一次，避免频繁UI操作）
        int progress = static_cast<int>((total_written * 100.0) / data.size());
        ui->statusLabel->setText(QString("Exporting CSV... %1%").arg(progress));
        QApplication::processEvents();  // 允许UI刷新
    }

    // 完成写入并关闭文件
    csv_file.close();
    ui->statusLabel->setText("CSV export completed");
    appendLog(QString("CSV export successful: %1 (%2 rows)").arg(csv_path).arg(total_written));
    return true;
}

// 导出CSV按钮点击事件（确保与UI按钮对象名匹配）
void BinParserWindow::on_convertToCsvButton_clicked() {
    if (parsed_data_.empty()) {
        QMessageBox::warning(this, "Warning", "Please parse the file first before exporting CSV");
        return;
    }

    // 使用当前解析的BIN文件路径作为基础生成CSV文件名
    convert_to_csv(current_file_path_, parsed_data_);
}
void BinParserWindow::compare_files() {
    // 检查两个文件是否都已解析
    if (parsed_data_.empty() || parsed_data2_.empty()) {
        QMessageBox::warning(this, "Warning", "Please parse both files before comparison");
        return;
    }

    // 清空对比表格
    ui->compareTableWidget->setRowCount(0);
    ui->statusLabel->setText("Comparing files...");
    QApplication::processEvents();

    // 构建序号到数据包的映射（提高查询效率，O(1)查询）
    std::unordered_map<uint32_t, const ParsedPacket*> file1_map;
    for (const auto& pkt : parsed_data_) {
        file1_map[pkt.seq] = &pkt;
    }

    std::unordered_map<uint32_t, const ParsedPacket*> file2_map;
    for (const auto& pkt : parsed_data2_) {
        file2_map[pkt.seq] = &pkt;
    }

    // 收集所有唯一序号（用于完整对比）
    std::unordered_set<uint32_t> all_seqs;
    for (const auto& pair : file1_map) {
        all_seqs.insert(pair.first);
    }
    for (const auto& pair : file2_map) {
        all_seqs.insert(pair.first);
    }

    // 开始对比并填充表格
    int row = 0;
    int match_count = 0;
    int mismatch_count = 0;
    const int batch_update = 500;  // 每500行更新一次表格，避免UI卡顿

    for (uint32_t seq : all_seqs) {
        const ParsedPacket* pkt1 = (file1_map.count(seq) > 0) ? file1_map[seq] : nullptr;
        const ParsedPacket* pkt2 = (file2_map.count(seq) > 0) ? file2_map[seq] : nullptr;

        // 插入新行
        ui->compareTableWidget->insertRow(row);

        // 1. 序号列
        QTableWidgetItem* seq_item = new QTableWidgetItem(QString::number(seq));
        seq_item->setFlags(seq_item->flags() & ~Qt::ItemIsEditable);
        ui->compareTableWidget->setItem(row, 0, seq_item);

        // 2. 文件1数据列（显示前3个数据预览）
        QString file1_data;
        if (pkt1) {
            file1_data = QString("Count: %1, Data: ").arg(pkt1->data.size());
            int preview_cnt = qMin(3, static_cast<int>(pkt1->data.size()));
            for (int i = 0; i < preview_cnt; ++i) {
                file1_data += QString::number(pkt1->data[i]);
                if (i < preview_cnt - 1) file1_data += ",";
            }
            if (pkt1->data.size() > 3) file1_data += "...";
        } else {
            file1_data = "Missing";
        }
        QTableWidgetItem* file1_item = new QTableWidgetItem(file1_data);
        file1_item->setFlags(file1_item->flags() & ~Qt::ItemIsEditable);
        ui->compareTableWidget->setItem(row, 1, file1_item);

        // 3. 文件2数据列（显示前3个数据预览）
        QString file2_data;
        if (pkt2) {
            file2_data = QString("Count: %1, Data: ").arg(pkt2->data.size());
            int preview_cnt = qMin(3, static_cast<int>(pkt2->data.size()));
            for (int i = 0; i < preview_cnt; ++i) {
                file2_data += QString::number(pkt2->data[i]);
                if (i < preview_cnt - 1) file2_data += ",";
            }
            if (pkt2->data.size() > 3) file2_data += "...";
        } else {
            file2_data = "Missing";
        }
        QTableWidgetItem* file2_item = new QTableWidgetItem(file2_data);
        file2_item->setFlags(file2_item->flags() & ~Qt::ItemIsEditable);
        ui->compareTableWidget->setItem(row, 2, file2_item);

        // 4. 对比结果列
        QString result;
        QColor text_color;
        if (!pkt1 || !pkt2) {
            result = "Sequence Mismatch";
            text_color = Qt::red;
            mismatch_count++;
        } else if (pkt1->data.size() != pkt2->data.size()) {
            result = "Data Length Mismatch";
            text_color = Qt::red;
            mismatch_count++;
        } else if (pkt1->data != pkt2->data) {
            result = "Data Mismatch";
            text_color = Qt::red;
            mismatch_count++;
        } else if (pkt1->crc_calculated != pkt2->crc_calculated) {
            result = "CRC Mismatch";
            // 修正：用 RGB 值定义橙色，替代 Qt::orange
                text_color = QColor(255, 165, 0);
            mismatch_count++;
        } else {
            result = "Perfect Match";
            text_color = Qt::green;
            match_count++;
        }
        QTableWidgetItem* result_item = new QTableWidgetItem(result);
        result_item->setForeground(QBrush(text_color));
        result_item->setFlags(result_item->flags() & ~Qt::ItemIsEditable);
        ui->compareTableWidget->setItem(row, 3, result_item);

        row++;

        // 分批更新UI，避免卡顿
        if (row % batch_update == 0) {
            ui->statusLabel->setText(QString("Comparing... %1/%2 sequences").arg(row).arg(all_seqs.size()));
            QApplication::processEvents();
        }
    }

    // 对比完成，更新状态
    QString final_status = QString("Comparison completed: Total %1, Matched %2, Mismatched %3")
        .arg(all_seqs.size()).arg(match_count).arg(mismatch_count);
    ui->statusLabel->setText(final_status);
    appendLog(final_status);

    // 调整表格列宽
    for (int col = 0; col < ui->compareTableWidget->columnCount(); ++col) {
        ui->compareTableWidget->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);
    }
}

