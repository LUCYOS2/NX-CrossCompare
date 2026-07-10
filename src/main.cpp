#include <QApplication>
#include <QSurfaceFormat>

#include "ui/MainWindow.h"

int main(int argc, char** argv) {
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);

    ui::MainWindow window;
    window.resize(1280, 800);
    window.show();

    return app.exec();
}
