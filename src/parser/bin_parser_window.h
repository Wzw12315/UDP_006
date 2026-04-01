#pragma once
#include <QWidget>
#include <vector>
#include <map>
#include <QThread>
#include <QElapsedTimer>
#include <QFile>  // 添加QFile头文件引用
#include "protocol.h"

struct ParsedPacket {
    uint32_t seq;               // 32位序号
    uint16_t crc_received;
    uint16_t crc_calculated;
    bool crc_match;
    std::vector<int16_t> data;
};

namespace Ui {
class BinParserWindow;
}

class ParseWorker : public QObject {
    Q_OBJECT
public:
    ParseWorker(const QString& file_path, std::vector<ParsedPacket>& data,
                int max_chunk_size = 10000)  // 增大单次解析块大小
        : file_path_(file_path), data_(data), max_chunk_size_(max_chunk_size),
          abort_(false) {}

    void abort() { abort_ = true; }

public slots:
    void doWork();

signals:
    void finished(bool success);
    void progressUpdated(int percent);
    void logMessage(QString msg);
    void chunkParsed(int count);  // 新增：通知解析了多少个包

private:
    QString file_path_;
    std::vector<ParsedPacket>& data_;
    int max_chunk_size_;
    bool abort_;
    bool parse_chunk(QFile& file, qint64 file_size, qint64& read_size, int& parsed_count);
};

class BinParserWindow : public QWidget {
    Q_OBJECT

public:
    explicit BinParserWindow(QWidget *parent = nullptr);
    ~BinParserWindow() override;

private slots:
    void on_selectFileButton_clicked();
    void on_selectFile2Button_clicked();
    void on_parseButton_clicked();
    void on_verifyButton_clicked();
    void on_compareButton_clicked();
    void on_convertToCsvButton_clicked();
    void on_prevPageButton_clicked();
    void on_nextPageButton_clicked();
    void on_cancelParseButton_clicked();  // 新增：取消解析按钮

    void handleParseFinished(bool success);
    void updateProgress(int percent);
    void appendLog(QString msg);
    void handleChunkParsed(int count);  // 新增：处理解析块完成

private:
    Ui::BinParserWindow *ui;
    QString current_file_path_;
    QString current_file2_path_;
    std::vector<ParsedPacket> parsed_data_;
    std::vector<ParsedPacket> parsed_data2_;
    QThread* parse_thread_ = nullptr;
    ParseWorker* parse_worker_ = nullptr;  // 新增：保存worker指针用于取消操作
    int current_page_ = 0;
    const int PAGE_SIZE = 50;   // 每页显示行数
    bool is_parsing_ = false;   // 新增：解析状态标记

    void display_parsed_data(int page);
    bool convert_to_csv(const QString &bin_file_path, const std::vector<ParsedPacket>& data);
    void compare_files();
    void verify_crc_and_highlight();
    void reset_parse_state();   // 新增：重置解析状态
};
