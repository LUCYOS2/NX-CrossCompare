#include "ui/MainWindow.h"

#include <QBrush>
#include <QColor>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "geometry/MockGeometryAdapter.h"
#include "rule/BuiltInCatalog.h"
#include "rule/RuleEngine.h"
#include "ui/RuleEditorDialog.h"
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
    // 회사PC 전환 지점: geometry::MockGeometryAdapter -> geometry::NxJtGeometryAdapter로
    // 교체하면 이 아래 UI/RuleEngine/DB 코드는 손대지 않고 그대로 재사용된다 (§9).
    : QMainWindow(parent),
      adapter_(std::make_unique<geometry::MockGeometryAdapter>()),
      db_("nx_crosscompare.db") {
    setWindowTitle("NX CrossCompare");

    db_.EnsureSchema();
    projectId_ = db_.FindOrCreateProject("Default");

    setupLeftPanel();
    setupRightPanel();
    setupCentralViewer();
    setupComparisonTable();
}

void MainWindow::setupLeftPanel() {
    auto* dock = new QDockWidget("프로젝트 / 규칙 관리", this);
    auto* container = new QWidget(dock);
    auto* layout = new QVBoxLayout(container);

    auto* list = new QListWidget(container);
    list->addItems({"프로젝트", "규칙 관리", "화면 설정", "가져오기/내보내기", "옵션"});

    auto* addRuleButton = new QPushButton("+ 새 규칙 추가", container);
    connect(addRuleButton, &QPushButton::clicked, this, &MainWindow::onAddRuleClicked);

    layout->addWidget(list);
    layout->addWidget(addRuleButton);

    dock->setWidget(container);
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
    comparisonTable_ = new QTableWidget(this);
    comparisonTable_->verticalHeader()->setVisible(false);

    auto* exportButton = new QPushButton("Excel Export (CSV)", this);
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportComparisonCsv);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->addWidget(comparisonTable_);
    layout->addWidget(exportButton);

    auto* dock = new QDockWidget("치수 비교 테이블", this);
    dock->setWidget(container);
    addDockWidget(Qt::BottomDockWidgetArea, dock);

    refreshComparisonTable();
}

void MainWindow::refreshComparisonTable() {
    std::map<int, geometry::ModelHandle> handlesByInch;
    for (const auto& [inch, file] : InchModelList()) {
        handlesByInch[inch] = adapter_->LoadModel(file);
    }

    // 내장 규칙(기본 세팅) + 사용자가 추가한 규칙(DB 저장) 순서로 합쳐서 보여준다.
    auto rules = rule::BuiltInRules();
    const auto userRules = db_.LoadRulesForProject(projectId_);
    rules.insert(rules.end(), userRules.begin(), userRules.end());

    comparisonTable_->clear();
    comparisonTable_->setRowCount(static_cast<int>(rules.size()));
    comparisonTable_->setColumnCount(static_cast<int>(handlesByInch.size()) + 1);

    QStringList headers;
    headers << "규칙";
    for (const auto& [inch, handle] : handlesByInch) {
        headers << QString("%1\"").arg(inch);
    }
    comparisonTable_->setHorizontalHeaderLabels(headers);

    lastReports_.clear();
    for (size_t row = 0; row < rules.size(); ++row) {
        const auto& r = rules[row];
        comparisonTable_->setItem(
            static_cast<int>(row), 0, new QTableWidgetItem(QString::fromStdString(r.name)));

        std::vector<rule::InchResult> results;
        try {
            results = rule::RuleEngine::Evaluate(*adapter_, handlesByInch, r);
        } catch (const std::exception& e) {
            comparisonTable_->setItem(
                static_cast<int>(row), 1, new QTableWidgetItem(QString("오류: %1").arg(e.what())));
            continue;
        }

        for (size_t col = 0; col < results.size(); ++col) {
            const auto& result = results[col];
            auto* item = new QTableWidgetItem(QString::number(result.value, 'f', 3));
            item->setBackground(QBrush(result.withinTolerance ? QColor(200, 255, 200) : QColor(255, 200, 200)));
            comparisonTable_->setItem(static_cast<int>(row), static_cast<int>(col) + 1, item);
        }
        lastReports_.push_back(report::RuleReport{r.name, results});
    }
    comparisonTable_->resizeColumnsToContents();
}

void MainWindow::onAddRuleClicked() {
    RuleEditorDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const rule::Rule newRule = dialog.BuildRule();
    try {
        db_.SaveRule(projectId_, newRule);
        refreshComparisonTable();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "규칙 저장 실패", QString::fromStdString(e.what()));
    }
}

void MainWindow::exportComparisonCsv() {
    const QString path = QFileDialog::getSaveFileName(this, "비교 결과 내보내기", "comparison_export.csv", "CSV (*.csv)");
    if (path.isEmpty()) {
        return;
    }

    try {
        report::ExportComparisonCsv(path.toStdString(), lastReports_);
        QMessageBox::information(this, "내보내기 완료",
            "CSV로 저장했습니다:\n" + path +
            "\n\n엑셀 서식이 필요하면 src/python/export_excel.py로 변환하세요.");
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "내보내기 실패", QString::fromStdString(e.what()));
    }
}

} // namespace ui
