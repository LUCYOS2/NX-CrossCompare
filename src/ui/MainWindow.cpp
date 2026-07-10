#include "ui/MainWindow.h"

#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QTableWidget>

#include <string>
#include <vector>

#include "geometry/MockGeometryAdapter.h"
#include "viewer/MultiViewportPanel.h"

namespace ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), adapter_(std::make_unique<geometry::MockGeometryAdapter>()) {
    setWindowTitle("NX CrossCompare");

    setupLeftPanel();
    setupRightPanel();
    setupCentralViewer();
    setupBottomTablePlaceholder();
}

void MainWindow::setupLeftPanel() {
    auto* dock = new QDockWidget("프로젝트 / 규칙 관리", this);
    auto* list = new QListWidget(dock);
    list->addItems({"프로젝트", "규칙 관리", "화면 설정", "가져오기/내보내기", "옵션"});
    dock->setWidget(list);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::setupRightPanel() {
    auto* dock = new QDockWidget("포인트 / 규칙", this);
    auto* list = new QListWidget(dock);
    list->addItems({"포인트 그룹", "포인트 검색", "규칙 관리"});
    dock->setWidget(list);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::setupCentralViewer() {
    // Phase3에서 SQLite 프로젝트 데이터로 대체될 하드코딩 인치 목록
    const std::vector<std::string> modelFiles = {
        "43inch.jt", "50inch.jt", "55inch.jt", "65inch.jt", "75inch.jt", "85inch.jt"
    };
    auto* panel = new viewer::MultiViewportPanel(adapter_.get(), modelFiles, this);
    setCentralWidget(panel);
}

void MainWindow::setupBottomTablePlaceholder() {
    auto* dock = new QDockWidget("치수 비교 테이블", this);
    auto* table = new QTableWidget(0, 0, dock);
    dock->setWidget(table);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
}

} // namespace ui
