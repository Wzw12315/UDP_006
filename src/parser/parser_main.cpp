// parser_main.cpp
#include "bin_parser_window.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    // 初始化Qt应用
    QApplication a(argc, argv);
    
    // 创建并显示解析工具主窗口
    BinParserWindow w;
    w.resize(1000, 600);  // 设置初始窗口大小
    w.setWindowTitle("Bin文件解析与数据验证工具");
    w.show();
    
    // 进入应用事件循环
    return a.exec();
}