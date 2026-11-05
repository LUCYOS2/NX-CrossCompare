#pragma once

// § 앱 진단 기록(2026-11-05) - "3D 뷰어 단축키가 하나도 안 먹는다", "형상 프리셋에서 줌/회전하다 멈추고 창이
// 꺼진다"(회사PC 리포트). 개발PC에서는 실제 키/마우스 입력을 흉내 낸 점검(diag_viewer_input)으로도 재현되지
// 않아서, 회사PC에서 무슨 일이 있었는지 앱이 스스로 남기게 한다.
//   - 로그 파일: <exe 폴더>/diag_logs/app_log.txt (build 폴더 아래라 저장소에 안 올라간다)
//   - 남기는 것: 실행 환경(그래픽 드라이버/원격 접속 여부/화면 배율/빌드 종류), 화면 멈춤(4초 이상 응답 없음)
//     때의 호출 위치, 비정상 종료(예외/abort/CRT 힙 오류) 때의 호출 위치, 느린 화면 그리기, 메모리 사용량.
//   - 남기지 않는 것: 좌표/치수/파일 내용 등 도면 정보 - 함수 이름과 소스 줄 번호, 시간, 개수만 쓴다.

#include <QObject>
#include <QString>

namespace viewer::diag {

// main()에서 QApplication을 만든 직후 한 번 부른다(UI 스레드에서). 두 번째 호출부터는 아무 일도 안 한다.
void InstallCrashAndHangReporter();

QString LogFilePath();
// 시간 도장을 붙여 로그 파일에 한 줄 덧붙인다(스레드 안전).
void Log(const QString& line);

struct GlInfo {
    QString vendor;
    QString renderer;
    QString version;
    bool software = false; // 그래픽 가속 없이 CPU로 그림(llvmpipe/GDI Generic/Basic Render 등) - 큰 도면에서 매우 느림
    bool valid = false;
};
// ModelViewport::initializeGL이 처음 한 번 채운다(프로세스 전체에서 첫 값만 남김).
void SetGlInfo(const GlInfo& info);
GlInfo CurrentGlInfo();

bool IsRemoteSession(); // 원격 데스크톱 등으로 접속한 세션인지(운영체제 판단)
QString MemoryUsageText(); // "앱 메모리 1234MB(작업 집합 567MB), 시스템 남은 메모리 8.1/16.0GB"
// 진단 창/로그 머리말용 여러 줄 요약 - 빌드 종류, Qt 버전, 화면, 원격 여부, 그래픽 드라이버, 메모리.
QString EnvironmentSummary();

// 뷰어 단축키가 실제로 실행될 때마다 알리는 허브 - 진단 창이 "키가 앱까지 왔는지/단축키가 실행됐는지"를
// 실시간으로 보여주는 데 쓴다. via: "단축키"(QShortcut) 또는 "보조 처리"(QShortcut이 놓친 키를 직접 처리).
class Hub : public QObject {
    Q_OBJECT
public:
    static Hub* instance();
    void NotifyShortcut(const QString& keyName, const QString& action, const QString& via) {
        emit viewerShortcutTriggered(keyName, action, via);
    }
    void NotifyGlInfoReady() { emit glInfoReady(); }

signals:
    void viewerShortcutTriggered(const QString& keyName, const QString& action, const QString& via);
    void glInfoReady();
};

} // namespace viewer::diag
