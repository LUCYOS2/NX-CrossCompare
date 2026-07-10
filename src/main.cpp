#include <QApplication>

#include "ui/MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    ui::MainWindow window;
    window.resize(1280, 800);
    window.show();

    return app.exec();
}
