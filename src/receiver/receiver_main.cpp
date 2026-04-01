// receiver_main.cpp
#include "main_receiver_window.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication a(argc, argv);
    MainReceiverWindow w;
    w.show();
    return a.exec();
}
