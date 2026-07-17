#include "ui/MainWindow.h"

#include <QAction>
#include <QBrush>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
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

    setupMenuBar();
    setupCentralViewer();
    setupComparisonTable();
}

void MainWindow::setupMenuBar() {
    // 좌/우 도킹 패널에 있던 메뉴를 전부 상단 메뉴바 한 줄로 통합 - 뷰어(도면) 영역을
    // 최대한 넓게 쓰기 위함. 평소엔 접혀있다가 클릭하면 펼쳐지는 메뉴바 특성을 활용.
    auto* bar = menuBar();

    bar->addMenu("프로젝트");

    auto* ruleMenu = bar->addMenu("규칙 관리");
    auto* addRuleAction = ruleMenu->addAction("+ 새 규칙 추가");
    connect(addRuleAction, &QAction::triggered, this, &MainWindow::onAddRuleClicked);

    bar->addMenu("화면 설정");
    bar->addMenu("가져오기/내보내기");
    bar->addMenu("옵션");
    bar->addMenu("포인트 그룹");
    bar->addMenu("포인트 검색");
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
    connect(comparisonTable_->horizontalHeader(), &QHeaderView::sectionClicked, this,
            &MainWindow::onRuleHeaderClicked);

    auto* exportButton = new QPushButton("Excel Export (CSV)", this);
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportComparisonCsv);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->addWidget(comparisonTable_);
    layout->addWidget(exportButton);

    auto* dock = new QDockWidget("치수 비교 테이블", this);
    dock->setWidget(container);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    // 시인성을 위해 기본 높이를 넉넉하게 확보 (사용자가 나중에 드래그로 조절 가능)
    resizeDocks({dock}, {320}, Qt::Vertical);

    refreshComparisonTable();
}

void MainWindow::refreshComparisonTable() {
    std::map<int, geometry::ModelHandle> handlesByInch;
    for (const auto& [inch, file] : InchModelList()) {
        handlesByInch[inch] = adapter_->LoadModel(file);
    }

    // 내장 규칙(기본 세팅) + 사용자가 추가한 규칙(DB 저장) 순서로 합쳐서 보여준다.
    // "규칙" 헤더 클릭으로 숨긴 항목(hiddenRuleNames_)은 제외한다.
    auto allRules = rule::BuiltInRules();
    const auto userRules = db_.LoadRulesForProject(projectId_);
    allRules.insert(allRules.end(), userRules.begin(), userRules.end());

    std::vector<rule::Rule> rules;
    for (auto& r : allRules) {
        if (hiddenRuleNames_.find(r.name) == hiddenRuleNames_.end()) {
            rules.push_back(r);
        }
    }

    comparisonTable_->clear();
    comparisonTable_->setRowCount(static_cast<int>(rules.size()));
    comparisonTable_->setColumnCount(static_cast<int>(handlesByInch.size()) + 1);

    QStringList headers;
    headers << "규칙 ▾"; // 클릭하면 표시 항목을 고를 수 있다는 힌트
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
    comparisonTable_->horizontalHeader()->setStretchLastSection(true);
}

void MainWindow::onRuleHeaderClicked(int section) {
    if (section != 0) {
        return;
    }

    auto allRules = rule::BuiltInRules();
    const auto userRules = db_.LoadRulesForProject(projectId_);
    allRules.insert(allRules.end(), userRules.begin(), userRules.end());

    QDialog dialog(this);
    dialog.setWindowTitle("표시할 항목 선택");
    auto* layout = new QVBoxLayout(&dialog);

    auto* list = new QListWidget(&dialog);
    for (const auto& r : allRules) {
        auto* item = new QListWidgetItem(QString::fromStdString(r.name), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool visible = hiddenRuleNames_.find(r.name) == hiddenRuleNames_.end();
        item->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
    }
    layout->addWidget(list);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    hiddenRuleNames_.clear();
    for (int i = 0; i < list->count(); ++i) {
        const auto* item = list->item(i);
        if (item->checkState() == Qt::Unchecked) {
            hiddenRuleNames_.insert(item->text().toStdString());
        }
    }
    refreshComparisonTable();
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
