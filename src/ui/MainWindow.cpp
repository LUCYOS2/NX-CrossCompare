#include "ui/MainWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "geometry/StepGeometryAdapter.h"
#include "rule/BuiltInCatalog.h"
#include "rule/RuleEngine.h"
#include "ui/ImportInchDialog.h"
#include "ui/RuleEditorDialog.h"
#include "ui/ShapePresetDialog.h"
#include "viewer/MultiViewportPanel.h"

namespace ui {

namespace {
// § 비교 테이블 컬럼 재구성(2026-09-12)/인치별 삭제(2026-09-18) - "규칙/포인트 부위/
// 관리항목 종류/공차" 4칸 다음부터 인치별 측정값 열. refreshComparisonTable()과
// onRuleHeaderClicked()가 같은 값을 써야 해서 파일 스코프로 뺐다.
constexpr int kFixedColumnCount = 4;
} // namespace

MainWindow::MainWindow(QWidget* parent)
    // §17: STEP(.stp)을 OCCT로 직접 읽는 StepGeometryAdapter가 기본 경로. NX Open API
    // 자동 연동(NxJtGeometryAdapter)은 §18 사유로 보류 - 스켈레톤은 그대로 남겨뒀고,
    // 나중에 재개하면 여기 한 줄만 바꾸면 된다.
    : QMainWindow(parent),
      adapter_(std::make_unique<geometry::StepGeometryAdapter>()),
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

    auto* viewMenu = bar->addMenu("화면 설정");

    // 1. 화면 모드: 전체 Solid / Solid-Edge / Wireframe. 3개 중 하나만 켜지는 배타적
    // 그룹(QActionGroup) - 기본값은 기존 렌더링 동작과 동일한 Solid-Edge.
    auto* renderModeGroup = new QActionGroup(this);
    renderModeGroup->setExclusive(true);
    auto* solidAction = viewMenu->addAction("Solid");
    solidAction->setCheckable(true);
    renderModeGroup->addAction(solidAction);
    connect(solidAction, &QAction::triggered, this, [this]() {
        currentRenderMode_ = viewer::RenderMode::Solid;
        applyDisplaySettingsToCurrentPanel();
    });

    auto* solidEdgeAction = viewMenu->addAction("Solid-Edge");
    solidEdgeAction->setCheckable(true);
    solidEdgeAction->setChecked(true);
    renderModeGroup->addAction(solidEdgeAction);
    connect(solidEdgeAction, &QAction::triggered, this, [this]() {
        currentRenderMode_ = viewer::RenderMode::SolidEdge;
        applyDisplaySettingsToCurrentPanel();
    });

    auto* wireframeAction = viewMenu->addAction("Wireframe");
    wireframeAction->setCheckable(true);
    renderModeGroup->addAction(wireframeAction);
    connect(wireframeAction, &QAction::triggered, this, [this]() {
        currentRenderMode_ = viewer::RenderMode::Wireframe;
        applyDisplaySettingsToCurrentPanel();
    });

    viewMenu->addSeparator();

    // 2. 뷰포트 조작 모드: 여러 인치를 마우스로 동시에 조작(기본값, 기존 동작) vs
    // 인치 하나만 독립적으로 조작. 배타적 그룹으로 둘 중 하나만 선택.
    auto* manipulationGroup = new QActionGroup(this);
    manipulationGroup->setExclusive(true);
    auto* syncedAction = viewMenu->addAction("전체 동시 조작");
    syncedAction->setCheckable(true);
    syncedAction->setChecked(true);
    manipulationGroup->addAction(syncedAction);
    connect(syncedAction, &QAction::triggered, this, [this]() {
        currentSyncedManipulation_ = true;
        applyDisplaySettingsToCurrentPanel();
    });

    auto* independentAction = viewMenu->addAction("개별 인치 독립 조작");
    independentAction->setCheckable(true);
    manipulationGroup->addAction(independentAction);
    connect(independentAction, &QAction::triggered, this, [this]() {
        currentSyncedManipulation_ = false;
        applyDisplaySettingsToCurrentPanel();
    });

    viewMenu->addSeparator();

    // 3. 현재 뷰 화면을 이미지로 저장.
    auto* captureAction = viewMenu->addAction("이미지로 저장...");
    connect(captureAction, &QAction::triggered, this, &MainWindow::onCaptureImageClicked);

    auto* importExportMenu = bar->addMenu("가져오기/내보내기");
    auto* importStepAction = importExportMenu->addAction("STEP(.stp) 파일 불러오기...");
    connect(importStepAction, &QAction::triggered, this, &MainWindow::onImportStepClicked);
    // § 불러온 파일 관리(2026-09-18) - 비교 테이블 인치 헤더 클릭 삭제는 "인치 미지정"
    // 상태인 파일(테이블에 열 자체가 안 생김, refreshComparisonTable이 inch<=0을
    // 건너뜀)을 못 지운다는 사각지대가 있었다(사용자 리포트: 도면창엔 남아있는데 테이블만
    // 바뀜). 인치 지정 여부와 무관하게 불러온 파일 전부를 보여주고 개별 삭제하는 창을
    // 별도로 둔다.
    auto* manageFilesAction = importExportMenu->addAction("불러온 파일 관리...");
    connect(manageFilesAction, &QAction::triggered, this, &MainWindow::onManageLoadedFilesClicked);

    bar->addMenu("옵션");

    // § 형상 프리셋으로 전환(2026-09-18) - 예전 "포인트 검색"(AnchorSearchDialog,
    // Hole/Boss_Center 텍스트+지름 검색)을 형상 프리셋(ShapePresetDialog)으로 완전히
    // 대체했다. 원통도 프리셋으로 등록해서 쓸 수 있는 상위 호환이라 별도로 둘 이유가
    // 없다는 사용자 판단 - RuleEditorDialog의 "프리셋으로 추가..." 버튼과 같은 창을
    // 여기서는 규칙에 묶지 않고 미리 등록/관리하는 용도로 연다.
    auto* searchMenu = bar->addMenu("형상 프리셋");
    auto* searchAction = searchMenu->addAction("형상 프리셋 관리...");
    connect(searchAction, &QAction::triggered, this, &MainWindow::onSearchAnchorsClicked);

    auto* ruleMenu = bar->addMenu("규칙 관리");
    auto* addRuleAction = ruleMenu->addAction("+ 새 규칙 추가");
    connect(addRuleAction, &QAction::triggered, this, &MainWindow::onAddRuleClicked);
}

std::vector<int> MainWindow::LoadedInches() const {
    std::vector<int> inches;
    for (const auto& entry : inchFiles_) {
        if (entry.inch > 0) {
            inches.push_back(entry.inch);
        }
    }
    return inches;
}

std::vector<std::pair<std::string, geometry::ModelHandle>> MainWindow::BuildLoadedModelList() const {
    // inchFiles_는 onImportStepClicked에서 이미 인치 오름차순(소형->대형)으로 정렬해
    // 두므로, 순서 그대로 반환하면 된다. handle도 이미 로드된 것을 재사용 - 다시
    // 파싱하지 않는다.
    std::vector<std::pair<std::string, geometry::ModelHandle>> models;
    for (const auto& entry : inchFiles_) {
        const QString label = entry.inch > 0
            ? QString("%1  %2").arg(ui::FormatInchLabel(entry.inch)).arg(
                  QFileInfo(QString::fromStdString(entry.filePath)).fileName())
            : QFileInfo(QString::fromStdString(entry.filePath)).fileName();
        models.push_back({label.toStdString(), entry.handle});
    }
    return models;
}

void MainWindow::setupCentralViewer() {
    auto* panel = new viewer::MultiViewportPanel(adapter_.get(), BuildLoadedModelList(), this);
    setCentralWidget(panel);
    viewerPanel_ = panel;
    applyDisplaySettingsToCurrentPanel();
    // 규칙 관리가 비모달로 열려 있는 동안 STEP을 다시 불러오면 handle이 전부 새로
    // 만들어진다 - 그 다이얼로그가 내장 뷰어(editorViewerPanel_)에서 이전 handle을 계속
    // 들고 있으면 댕글링되므로 새 모델 목록으로 내장 패널을 다시 만들게 한다.
    if (activeRuleDialog_) {
        activeRuleDialog_->RewireModels(BuildLoadedModelList());
        activeRuleDialog_->SetAvailableInches(LoadedInches());
    }
}

void MainWindow::applyDisplaySettingsToCurrentPanel() {
    if (!viewerPanel_) {
        return;
    }
    viewerPanel_->setRenderMode(currentRenderMode_);
    viewerPanel_->setSyncedManipulation(currentSyncedManipulation_);
}

void MainWindow::setupComparisonTable() {
    comparisonTable_ = new QTableWidget(this);
    comparisonTable_->verticalHeader()->setVisible(false);
    // 이 테이블은 계산된 결과를 보여주기만 하는 읽기 전용 표시 - 편집 가능하게 두면
    // 셀을 선택한 채로 키를 치면(예: 뷰어 단축키를 누르려다 포커스가 여기 있으면) 셀
    // 편집 모드로 들어가버려서 값이 안 바뀌었는데 바뀐 것처럼 보이는 문제가 있었다.
    comparisonTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(comparisonTable_->horizontalHeader(), &QHeaderView::sectionClicked, this,
            &MainWindow::onRuleHeaderClicked);
    // resizeDocks({dock},{320},...)는 일회성 힌트라서 나중에 refreshComparisonTable()이
    // 테이블 컬럼 수를 바꾸면(가져오기로 인치가 늘어날 때마다) 무시되고 도크가 3D 뷰어
    // 공간을 거의 다 차지해버렸다(사용자 리포트: 도면이 안 보임 - 도크가 항상 이겼음).
    // maximumHeight는 매 레이아웃 패스마다 강제되는 하드 제약이라 컬럼/행이 바뀌어도
    // 계속 유지된다. 테이블 자체는 스크롤 가능한 뷰라 내용이 넘치면 스크롤바가 생긴다.
    comparisonTable_->setMaximumHeight(260);

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
    for (const auto& entry : inchFiles_) {
        if (entry.inch <= 0) {
            continue; // 인치 미지정 항목은 제외 - 목록/재입고 시 먼저 인치를 채워야 함
        }
        handlesByInch[entry.inch] = entry.handle; // onImportStepClicked에서 이미 로드된 handle 재사용
    }

    // 내장 규칙(기본 세팅) + 사용자가 추가한 규칙(DB 저장)을 합쳐서 보여준다. 같은
    // 이름이면 사용자 저장본이 내장 기본값을 덮어쓴다(rule::MergeWithBuiltIns 참고 -
    // "기본값 공차를 수정해서 저장하면 내장값+사용자값 중복으로 뜨면 안 된다"는 요청).
    // "규칙" 헤더 클릭으로 숨긴 항목(hiddenRuleNames_)은 제외한다.
    const auto userRules = db_.LoadRulesForProject(projectId_);
    auto allRules = rule::MergeWithBuiltIns(userRules);

    std::vector<rule::Rule> rules;
    for (auto& r : allRules) {
        if (hiddenRuleNames_.find(r.name) == hiddenRuleNames_.end()) {
            rules.push_back(r);
        }
    }

    // § 비교 테이블 컬럼 재구성(2026-09-12) - "규칙/포인트 부위/관리항목 종류/공차/
    // 인치들 쭉 나열하면 될 것 같다"는 요청. 예전엔 "규칙" 다음 바로 인치 값들이었는데,
    // 규칙 관리에서 입력한 CTQ 코드(Point 부위)/종류/공차가 화면에서 전혀 안 보였다.
    // 고정 4칸(규칙/포인트 부위/관리항목 종류/공차) 다음에 인치별 측정값을 그대로 붙인다.
    comparisonTable_->clear();
    comparisonTable_->setRowCount(static_cast<int>(rules.size()));
    comparisonTable_->setColumnCount(kFixedColumnCount + static_cast<int>(handlesByInch.size()));

    QStringList headers;
    headers << "규칙 ▾" << "포인트(부위)" << "관리항목 종류" << "공차"; // 헤더 클릭 힌트는 규칙 칸에만
    comparisonInchColumns_.clear();
    for (const auto& [inch, handle] : handlesByInch) {
        headers << (ui::FormatInchLabel(inch) + " ▾"); // 클릭하면 이 인치를 삭제할 수 있음
        comparisonInchColumns_.push_back(inch);
    }
    comparisonTable_->setHorizontalHeaderLabels(headers);

    lastReports_.clear();
    for (size_t row = 0; row < rules.size(); ++row) {
        const auto& r = rules[row];
        const int rowIdx = static_cast<int>(row);
        comparisonTable_->setItem(rowIdx, 0, new QTableWidgetItem(QString::fromStdString(r.name)));
        comparisonTable_->setItem(
            rowIdx, 1, new QTableWidgetItem(QString::fromStdString(r.ctqCode.value_or(""))));
        comparisonTable_->setItem(
            rowIdx, 2, new QTableWidgetItem(QString::fromStdString(r.checkPointCategory.value_or(""))));
        // § 공차 표시 버그 수정(2026-09-12) - "규칙관리에서 공차 저장했는데 나오지 않아"
        // 리포트. 전장 사이즈(OverallSize)는 공차값과 무관하게 판정에는 안 쓰지만(인치마다
        // 값이 다른 게 정상 - RuleEngine.cpp), 사용자가 기록/참고용으로 실제 공차값을
        // 입력해서 저장한 경우(예: 0.500/0.500)까지 무조건 "-"로 가려버리면 안 된다.
        // 값을 하나도 안 넣었을 때(둘 다 0)만 "-"로 표시하고, 실제 값이 있으면 보여준다.
        const bool hasToleranceValue = r.tolerancePlusMm != 0.0 || r.toleranceMinusMm != 0.0;
        const QString toleranceText = (r.measurementType == rule::MeasurementType::OverallSize && !hasToleranceValue)
            ? "-"
            : QString("+%1/-%2").arg(r.tolerancePlusMm, 0, 'f', 3).arg(r.toleranceMinusMm, 0, 'f', 3);
        comparisonTable_->setItem(rowIdx, 3, new QTableWidgetItem(toleranceText));

        std::vector<rule::InchResult> results;
        try {
            results = rule::RuleEngine::Evaluate(*adapter_, handlesByInch, r);
        } catch (const std::exception& e) {
            comparisonTable_->setItem(
                rowIdx, kFixedColumnCount, new QTableWidgetItem(QString("오류: %1").arg(e.what())));
            continue;
        }

        // § 인치별 판정 표시(2026-10-30) - 인치마다 따로: 매칭 실패는 회색 "실패"(사유는 툴팁),
        // 도면 스펙이 있으면 진한 초록/빨강(도면 합격/불합격), 없으면 옅은 색(가장 작은 인치 대비
        // 비교일 뿐 도면 판정이 아님)으로 구분한다.
        for (size_t col = 0; col < results.size(); ++col) {
            const auto& result = results[col];
            QTableWidgetItem* item = nullptr;
            if (!result.ok) {
                // § 실패 사유 표시(2026-11-04) - 툴팁에만 있으면 실패가 왜 났는지 눈에 안 띈다. 칸에
                // 사유를 바로 쓰고(칸이 좁으면 잘림), 전체 문장은 툴팁에 둔다.
                item = new QTableWidgetItem(QString("실패: %1").arg(QString::fromStdString(result.error)));
                item->setBackground(QBrush(QColor(220, 220, 220)));
                item->setToolTip(QString("측정 실패: %1").arg(QString::fromStdString(result.error)));
            } else if (result.spec.has_value()) {
                item = new QTableWidgetItem(QString::number(result.value, 'f', 3));
                item->setBackground(QBrush(result.withinTolerance ? QColor(150, 230, 150) : QColor(255, 150, 150)));
                item->setToolTip(QString("도면 스펙 %1 / 편차 %2%3 mm → %4")
                                     .arg(*result.spec, 0, 'f', 3)
                                     .arg(result.deviation >= 0.0 ? "+" : "")
                                     .arg(result.deviation, 0, 'f', 3)
                                     .arg(result.withinTolerance ? "합격" : "불합격"));
            } else {
                item = new QTableWidgetItem(QString::number(result.value, 'f', 3));
                item->setBackground(QBrush(result.withinTolerance ? QColor(225, 250, 225) : QColor(255, 225, 225)));
                item->setToolTip(QString("도면 스펙 없음 - 가장 작은 인치 대비 편차 %1%2 mm (도면 판정 아님)")
                                     .arg(result.deviation >= 0.0 ? "+" : "")
                                     .arg(result.deviation, 0, 'f', 3));
            }
            // § 인치 간 값 일관성(2026-11-04) - 값은 냈지만 튀는 인치는 ⚠를 붙이고 글자를 주황색으로,
            // 사유는 툴팁 맨 앞에 둔다(판정 배경색은 그대로 - 합격/불합격과 별개인 "확인 필요" 표시).
            if (result.ok && !result.warning.empty()) {
                item->setText(item->text() + " ⚠");
                item->setForeground(QBrush(QColor(200, 90, 0)));
                item->setToolTip(QString("⚠ 확인 필요: %1\n%2")
                                     .arg(QString::fromStdString(result.warning))
                                     .arg(item->toolTip()));
            }
            comparisonTable_->setItem(rowIdx, kFixedColumnCount + static_cast<int>(col), item);
        }
        lastReports_.push_back(report::RuleReport{r.name, results, r.imagePath, r.ctqCode,
                                                    r.checkPointCategory, rule::ToString(r.measurementType),
                                                    r.tolerancePlusMm, r.toleranceMinusMm});
    }
    // 고정 4칸은 내용 길이에 맞추고, 인치 값 칸들은 서로 비교하기 쉽도록 폭을 통일한다.
    for (int col = 0; col < kFixedColumnCount; ++col) {
        comparisonTable_->resizeColumnToContents(col);
        comparisonTable_->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Interactive);
    }
    for (int col = kFixedColumnCount; col < comparisonTable_->columnCount(); ++col) {
        comparisonTable_->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);
    }
}

void MainWindow::onRuleHeaderClicked(int section) {
    // § 인치별 삭제(2026-09-18) - "불러오기한 도면들 칸 각각 삭제 가능하게 해줘" 요청.
    // 인치 열 헤더(4번째 칸부터)를 클릭하면 확인 후 그 인치를 목록에서 뺀다 - "규칙" 열
    // (section==0)의 표시/숨김 토글과 같은 "헤더 클릭 = 그 열 관리" 패턴을 그대로 따른다.
    if (section >= kFixedColumnCount) {
        const int columnIndex = section - kFixedColumnCount;
        if (columnIndex < 0 || columnIndex >= static_cast<int>(comparisonInchColumns_.size())) {
            return;
        }
        const int inch = comparisonInchColumns_[static_cast<size_t>(columnIndex)];
        const auto it = std::find_if(inchFiles_.begin(), inchFiles_.end(),
                                      [inch](const LoadedInch& e) { return e.inch == inch; });
        if (it == inchFiles_.end()) {
            return;
        }
        const QString fileName = QFileInfo(QString::fromStdString(it->filePath)).fileName();
        const auto answer = QMessageBox::question(
            this, "인치 삭제",
            QString("%1(%2)을(를) 목록에서 뺄까요? 뷰어/비교 테이블에서 사라집니다.")
                .arg(ui::FormatInchLabel(inch))
                .arg(fileName));
        if (answer != QMessageBox::Yes) {
            return;
        }
        inchFiles_.erase(it);
        setupCentralViewer();
        refreshComparisonTable();
        return;
    }

    if (section != 0) {
        return;
    }

    const auto userRules = db_.LoadRulesForProject(projectId_);
    auto allRules = rule::MergeWithBuiltIns(userRules);

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

// § 규칙 관리 통합 + 3D 클릭 피킹 - 이 창을 열어둔 채로 뒤의 3D 뷰포트를 클릭해야
// 해서 비모달로 띄운다(exec() 대신 show()). RuleEditorDialog가 목록 조회+생성+수정+
// 삭제를 전부 자체적으로 DB에 즉시 반영하고 rulesChanged()를 쏘므로, 그걸 받아서
// 비교 테이블만 새로 고친다. 이미 열려 있으면 새로 만들지 않고 그 창을 앞으로 올린다.
void MainWindow::onAddRuleClicked() {
    if (activeRuleDialog_) {
        activeRuleDialog_->raise();
        activeRuleDialog_->activateWindow();
        return;
    }
    auto* dialog = new RuleEditorDialog(&db_, projectId_, adapter_.get(), BuildLoadedModelList(), this);
    dialog->SetAvailableInches(LoadedInches());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    activeRuleDialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this]() { activeRuleDialog_ = nullptr; });
    connect(dialog, &RuleEditorDialog::rulesChanged, this, &MainWindow::refreshComparisonTable);
    dialog->show();
}

// § 형상 프리셋 - 뷰어에서 형상을 클릭해 캡처하고 이름 붙여 저장/관리한다(RuleEditorDialog
// 없이도 미리 등록해둘 수 있게). 아직 STEP을 하나도 안 불러왔으면 등록할 대상이 없다는
// 뜻이라 바로 안내만 하고 다이얼로그를 열지 않는다.
void MainWindow::onSearchAnchorsClicked() {
    const auto models = BuildLoadedModelList();
    if (models.empty()) {
        QMessageBox::information(this, "대상 없음", "먼저 STEP 파일을 불러온 뒤 이용하세요.");
        return;
    }
    ShapePresetDialog dialog(&db_, projectId_, adapter_.get(), models, this);
    dialog.exec();
}

void MainWindow::onManageLoadedFilesClicked() {
    if (inchFiles_.empty()) {
        QMessageBox::information(this, "불러온 파일 없음", "아직 불러온 STEP 파일이 없습니다.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("불러온 파일 관리");
    dialog.resize(520, 360);
    auto* layout = new QVBoxLayout(&dialog);

    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({"인치", "파일명"});
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // § 인치 재지정(2026-09-18) - "미지정을 다시 지정/수정할 수 있게 해달라"는 요청.
    // 인치 칸만 더블클릭으로 편집 가능하게 하고(파일명은 그대로 읽기 전용), 숫자를
    // 입력하면 그 자리에서 바로 반영한다.
    table->setEditTriggers(QAbstractItemView::DoubleClicked);

    bool populating = false; // populateTable() 안에서의 setItem이 itemChanged를 다시 부르지 않게.
    auto populateTable = [&]() {
        populating = true;
        table->setRowCount(static_cast<int>(inchFiles_.size()));
        for (size_t row = 0; row < inchFiles_.size(); ++row) {
            const auto& entry = inchFiles_[row];
            const int r = static_cast<int>(row);
            const QString inchText = entry.inch > 0 ? QString::number(entry.inch) : QString();
            auto* inchItem = new QTableWidgetItem(inchText);
            inchItem->setFlags(inchItem->flags() | Qt::ItemIsEditable);
            table->setItem(r, 0, inchItem);
            auto* fileItem =
                new QTableWidgetItem(QFileInfo(QString::fromStdString(entry.filePath)).fileName());
            fileItem->setFlags(fileItem->flags() & ~Qt::ItemIsEditable);
            table->setItem(r, 1, fileItem);
        }
        populating = false;
    };
    populateTable();
    layout->addWidget(table);
    layout->addWidget(new QLabel("인치 칸을 더블클릭해서 숫자를 입력/수정할 수 있습니다(비우면 미지정).", &dialog));

    connect(table, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem* item) {
        if (populating || item->column() != 0) {
            return;
        }
        const int row = item->row();
        if (row < 0 || row >= static_cast<int>(inchFiles_.size())) {
            return;
        }
        const QString text = item->text().trimmed();
        int newInch = 0;
        if (!text.isEmpty()) {
            bool ok = false;
            newInch = text.toInt(&ok);
            if (!ok || newInch <= 0) {
                QMessageBox::warning(&dialog, "잘못된 값", "인치는 양의 정수여야 합니다(비우면 미지정).");
                populateTable();
                return;
            }
        }
        inchFiles_[static_cast<size_t>(row)].inch = newInch;
        // onImportStepClicked와 같은 정렬 기준(미지정은 맨 뒤) - 인치 관계로 화면에서도
        // 일관되게 소형->대형 순으로 보이게 한다.
        std::stable_sort(inchFiles_.begin(), inchFiles_.end(), [](const LoadedInch& a, const LoadedInch& b) {
            const int ka = a.inch <= 0 ? 100000 : a.inch;
            const int kb = b.inch <= 0 ? 100000 : b.inch;
            return ka < kb;
        });
        setupCentralViewer();
        refreshComparisonTable();
        populateTable();
    });

    auto* deleteButton = new QPushButton("선택 항목 삭제", &dialog);
    layout->addWidget(deleteButton);
    connect(deleteButton, &QPushButton::clicked, &dialog, [&]() {
        const auto selected =
            table->selectionModel() ? table->selectionModel()->selectedRows() : QModelIndexList();
        if (selected.isEmpty()) {
            return;
        }
        const int row = selected.front().row();
        if (row < 0 || row >= static_cast<int>(inchFiles_.size())) {
            return;
        }
        inchFiles_.erase(inchFiles_.begin() + row);
        setupCentralViewer();
        refreshComparisonTable();
        populateTable();
    });

    auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(closeButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(closeButtons);

    dialog.exec();
}

void MainWindow::onImportStepClicked() {
    // 사용자 피드백 반영: 인치를 미리 지정하지 않고 여러 STEP 파일(예: 43/65/85 FRAME류)을
    // 한번에 선택 -> 파일명에서 인치 자동인식(ImportInchDialog) -> 소형~대형 순 정렬.
    // 파일당 LoadModel은 여기서 딱 한 번만 호출하고, 그 handle을 뷰어/비교표가 재사용한다
    // (예전에는 검증/뷰어/비교표에서 각각 다시 파싱해 배치 업로드 시 그만큼 무거워졌었다).
    const QStringList filePaths = QFileDialog::getOpenFileNames(
        this, "STEP 파일 선택 (여러 개 선택 가능)", QString(), "STEP Files (*.stp *.step)");
    if (filePaths.isEmpty()) {
        return;
    }

    ImportInchDialog inchDialog(filePaths, this);
    if (inchDialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto picked = inchDialog.Result(); // (inch, path) - 인치 오름차순, 미지정은 뒤

    QProgressDialog progress("STEP 파일 불러오는 중...", "취소", 0, static_cast<int>(picked.size()), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    QStringList failures;
    int done = 0;
    for (const auto& [inch, path] : picked) {
        progress.setLabelText(
            QString("불러오는 중: %1").arg(QFileInfo(QString::fromStdString(path)).fileName()));
        progress.setValue(done);
        if (progress.wasCanceled()) {
            break;
        }

        geometry::ModelHandle handle = geometry::kInvalidModelHandle;
        try {
            handle = adapter_->LoadModel(path);
        } catch (const std::exception& e) {
            failures << QString("%1: %2").arg(QString::fromStdString(path), QString::fromStdString(e.what()));
            ++done;
            continue;
        }

        // 같은 인치를 다시 불러오면 기존 항목을 교체(재입고). 미지정(inch<=0)은 항상 새로 추가.
        auto it = std::find_if(inchFiles_.begin(), inchFiles_.end(), [inch](const LoadedInch& e) {
            return inch > 0 && e.inch == inch;
        });
        if (it != inchFiles_.end()) {
            *it = LoadedInch{inch, path, handle};
        } else {
            inchFiles_.push_back(LoadedInch{inch, path, handle});
        }
        ++done;
    }
    progress.setValue(static_cast<int>(picked.size()));

    // 소형 -> 대형 순 정렬(요청사항) - 미지정(inch<=0)은 뒤로 보낸다.
    std::stable_sort(inchFiles_.begin(), inchFiles_.end(), [](const LoadedInch& a, const LoadedInch& b) {
        const int ka = a.inch <= 0 ? 100000 : a.inch;
        const int kb = b.inch <= 0 ? 100000 : b.inch;
        return ka < kb;
    });

    if (!failures.isEmpty()) {
        QMessageBox::warning(this, "일부 파일 불러오기 실패", failures.join("\n"));
    }

    setupCentralViewer();
    refreshComparisonTable();
}

void MainWindow::exportComparisonCsv() {
    const QString path = QFileDialog::getSaveFileName(this, "비교 결과 내보내기", "comparison_export.csv", "CSV (*.csv)");
    if (path.isEmpty()) {
        return;
    }

    // § 모델 컬럼(2026-09-12) - export만 세로형(long)으로 바뀌면서 "모델" 컬럼에 인치별
    // 실제 로드된 STEP 파일명을 보여줘야 한다(사용자 확인: 화면 매트릭스는 그대로,
    // export만 세로형). 경로 전체 대신 파일명만 보이게 QFileInfo::fileName() 사용.
    std::map<int, std::string> modelNameByInch;
    for (const auto& entry : inchFiles_) {
        if (entry.inch <= 0) {
            continue;
        }
        modelNameByInch[entry.inch] = QFileInfo(QString::fromStdString(entry.filePath)).fileName().toStdString();
    }

    try {
        report::ExportComparisonCsv(path.toStdString(), lastReports_, modelNameByInch);
        QMessageBox::information(this, "내보내기 완료",
            "CSV로 저장했습니다:\n" + path +
            "\n\n엑셀 서식이 필요하면 src/python/export_excel.py로 변환하세요.");
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "내보내기 실패", QString::fromStdString(e.what()));
    }
}

// § 화면설정 - 현재 뷰(3D 뷰포트 그리드 + 단면 컨트롤 바)를 이미지로 저장.
// QWidget::grab()은 자식 QOpenGLWidget의 GPU 렌더 결과도 Qt가 백킹스토어로 합성해서
// 정확히 포함한다 - 뷰포트별로 따로 grabFramebuffer()를 호출해 이어붙일 필요가 없다.
void MainWindow::onCaptureImageClicked() {
    if (!viewerPanel_) {
        return;
    }
    const QString defaultName =
        "nx_crosscompare_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
    const QString path =
        QFileDialog::getSaveFileName(this, "화면 이미지로 저장", defaultName, "PNG 이미지 (*.png)");
    if (path.isEmpty()) {
        return;
    }

    const QPixmap capture = viewerPanel_->grab();
    if (!capture.save(path, "PNG")) {
        QMessageBox::critical(this, "저장 실패", "이미지를 저장하지 못했습니다:\n" + path);
        return;
    }
    QMessageBox::information(this, "저장 완료", "화면 이미지를 저장했습니다:\n" + path);
}

} // namespace ui
