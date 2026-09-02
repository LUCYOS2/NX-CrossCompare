#include <QApplication>
#include <QSurfaceFormat>

#include "ui/MainWindow.h"

int main(int argc, char** argv) {
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8); // 단면 캡(H) - 스텐실 패리티 기법으로 solid 단면을 채움
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);

    ui::MainWindow window;
    // 기본 크기로 뜨면 하단 비교 테이블 도크(고정 320px)가 3D 뷰어 공간을 거의 다
    // 잡아먹어서 도면이 안 보이는 것처럼 보이는 문제가 있었다(사용자 리포트) - 처음부터
    // 최대화해서 뜨게 해 이 문제를 근본적으로 피한다.
    window.showMaximized();

    return app.exec();
}
