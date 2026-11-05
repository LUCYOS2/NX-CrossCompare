#pragma once

#include <QDialog>

class QPlainTextEdit;

namespace ui {

// § 뷰어·키 입력 점검(2026-11-05) - "단축키가 하나도 안 먹는다", "형상 프리셋에서 조작하다 멈추고 꺼진다"(회사PC)
// 리포트를 회사PC에서 직접 확인하는 창. 메뉴 "검증 → 뷰어·키 입력 점검...".
//   - 위: 실행 환경(빌드 종류, 원격 접속 여부, 화면 배율, 그래픽 드라이버/소프트웨어 렌더링 여부, 메모리)
//   - 아래: 이 창을 띄운 동안 앱이 받은 키(키 이름, Windows 가상 키 코드, 입력 글자)와 뷰어 단축키 실행 기록 -
//     키가 앱까지 아예 안 오는지(노트북 Fn 잠금/원격 접속 프로그램/다른 프로그램이 가로챔), 와도 단축키로
//     안 이어지는지를 가른다.
//   - "결과 복사" -> 메신저 등에 붙여넣기. 도면 좌표/치수는 들어가지 않는다.
// 비모달이라 띄워 둔 채 메인 화면의 3D 화면을 클릭하고 키를 눌러 보면 된다.
class ViewerDiagnosticsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ViewerDiagnosticsDialog(QWidget* parent = nullptr);
    ~ViewerDiagnosticsDialog() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refreshEnvironment();
    void appendKeyLine(const QString& line);

    QPlainTextEdit* environmentText_ = nullptr;
    QPlainTextEdit* keyLog_ = nullptr;
    int keyLines_ = 0;
};

} // namespace ui
