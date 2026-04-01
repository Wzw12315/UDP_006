// sender_main.cpp
#include "main_sender_window.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication a(argc, argv);
    MainSenderWindow w;
    w.show();
    return a.exec();
}
