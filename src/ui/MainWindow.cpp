#include "ui/MainWindow.h"

#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QTableWidget>

namespace ui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("NX CrossCompare");

    setupLeftPanel();
    setupRightPanel();
    setupCentralViewerPlaceholder();
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

void MainWindow::setupCentralViewerPlaceholder() {
    auto* label = new QLabel("다중 Viewer 영역 (Phase2에서 구현)", this);
    label->setAlignment(Qt::AlignCenter);
    setCentralWidget(label);
}

void MainWindow::setupBottomTablePlaceholder() {
    auto* dock = new QDockWidget("치수 비교 테이블", this);
    auto* table = new QTableWidget(0, 0, dock);
    dock->setWidget(table);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
}

} // namespace ui
