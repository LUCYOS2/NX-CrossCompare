#pragma once

#include <QMainWindow>

namespace ui {

// Phase1 UI 셸 — 레이아웃만 구성, 실제 기능(뷰어/규칙/DB 연동)은 이후 Phase에서 채운다.
// 개발계획_v2.md §10 UI 구조 참고: 좌측/중앙/우측/하단 4분할.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void setupLeftPanel();
    void setupRightPanel();
    void setupCentralViewerPlaceholder();
    void setupBottomTablePlaceholder();
};

} // namespace ui
