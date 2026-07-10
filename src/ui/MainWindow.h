#pragma once

#include <QMainWindow>

#include <memory>

#include "geometry/IGeometryAdapter.h"

namespace ui {

// Phase2: 중앙 영역은 MultiViewportPanel(Mock 데이터, 6개 인치)로 채워짐.
// 개발계획_v2.md §10 UI 구조 참고: 좌측/중앙/우측/하단 4분할.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void setupLeftPanel();
    void setupRightPanel();
    void setupCentralViewer();
    void setupBottomTablePlaceholder();

    std::unique_ptr<geometry::IGeometryAdapter> adapter_;
};

} // namespace ui
