#include "ui/MainWindow.h"

#include <QBrush>
#include <QColor>
#include <QDockWidget>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "geometry/MockGeometryAdapter.h"
#include "rule/BuiltInCatalog.h"
#include "rule/RuleEngine.h"
#include "viewer/MultiViewportPanel.h"

namespace ui {

namespace {

// Phase3에서 SQLite 프로젝트 데이터로 대체될 하드코딩 인치 목록.
// (inch, 파일명) 쌍으로 둬서 중앙 Viewer와 하단 비교 테이블이 같은 인치 집합을 쓴다.
const std::vector<std::pair<int, std::string>>& InchModelList() {
    static const std::vector<std::pair<int, std::string>> kList = {
        {43, "43inch.jt"}, {50, "50inch.jt"}, {55, "55inch.jt"},
        {65, "65inch.jt"}, {75, "75inch.jt"}, {85, "85inch.jt"},
    };
    return kList;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), adapter_(std::make_unique<geometry::MockGeometryAdapter>()) {
    setWindowTitle("NX CrossCompare");

    setupLeftPanel();
    setupRightPanel();
    setupCentralViewer();
    setupComparisonTable();
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
    std::vector<std::string> modelFiles;
    for (const auto& [inch, file] : InchModelList()) {
        modelFiles.push_back(file);
    }
    auto* panel = new viewer::MultiViewportPanel(adapter_.get(), modelFiles, this);
    setCentralWidget(panel);
}

void MainWindow::setupComparisonTable() {
    std::map<int, geometry::ModelHandle> handlesByInch;
    for (const auto& [inch, file] : InchModelList()) {
        handlesByInch[inch] = adapter_->LoadModel(file);
    }

    const auto rules = rule::BuiltInPointRules();

    auto* table = new QTableWidget(static_cast<int>(rules.size()),
                                    static_cast<int>(handlesByInch.size()) + 1, this);
    QStringList headers;
    headers << "규칙";
    for (const auto& [inch, handle] : handlesByInch) {
        headers << QString("%1\"").arg(inch);
    }
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);

    for (size_t row = 0; row < rules.size(); ++row) {
        const auto& r = rules[row];
        table->setItem(static_cast<int>(row), 0, new QTableWidgetItem(QString::fromStdString(r.name)));

        const auto results = rule::RuleEngine::Evaluate(*adapter_, handlesByInch, r);
        for (size_t col = 0; col < results.size(); ++col) {
            const auto& result = results[col];
            auto* item = new QTableWidgetItem(QString::number(result.value, 'f', 3));
            item->setBackground(QBrush(result.withinTolerance ? QColor(200, 255, 200) : QColor(255, 200, 200)));
            table->setItem(static_cast<int>(row), static_cast<int>(col) + 1, item);
        }
    }
    table->resizeColumnsToContents();

    auto* dock = new QDockWidget("치수 비교 테이블", this);
    dock->setWidget(table);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
}

} // namespace ui
