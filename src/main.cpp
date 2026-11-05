#include <QApplication>
#include <QSurfaceFormat>

#include "ui/MainWindow.h"
#include "viewer/DiagnosticsLog.h"

int main(int argc, char** argv) {
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8); // 단면 캡(H) - 스텐실 패리티 기법으로 solid 단면을 채움
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    // § 앱 진단 기록(2026-11-05) - 화면 멈춤/비정상 종료/느린 그리기와 실행 환경을 <exe 폴더>/diag_logs/app_log.txt에
    // 남긴다(도면 좌표/치수는 안 남김). 회사PC에서만 생기는 문제를 원인까지 따라가려고.
    viewer::diag::InstallCrashAndHangReporter();

    ui::MainWindow window;
    // 기본 크기로 뜨면 하단 비교 테이블 도크(고정 320px)가 3D 뷰어 공간을 거의 다
    // 잡아먹어서 도면이 안 보이는 것처럼 보이는 문제가 있었다(사용자 리포트) - 처음부터
    // 최대화해서 뜨게 해 이 문제를 근본적으로 피한다.
    window.showMaximized();

    return app.exec();
}
