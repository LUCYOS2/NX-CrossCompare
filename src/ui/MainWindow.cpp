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
#include "ui/AnchorSearchDialog.h"
#include "ui/ImportInchDialog.h"
#include "ui/RuleEditorDialog.h"
#include "viewer/MultiViewportPanel.h"

namespace ui {

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

    bar->addMenu("옵션");

    // § 워크플로우(사용자 확인, 2026-09-08): 포인트 검색으로 형상을 먼저 확인 -> 규칙
    // 관리(RuleEditorDialog)에서 그 조건 그대로 불러와 규칙을 만들거나 수정. "포인트
    // 그룹"은 규칙 관리와 필드가 완전히 겹쳐서 폐기(대화 기록 참고) - 재사용은
    // RuleEditorDialog의 포인트 A/B "검색..." 버튼이 대신한다.
    auto* searchMenu = bar->addMenu("포인트 검색");
    auto* searchAction = searchMenu->addAction("형상 검색...");
    connect(searchAction, &QAction::triggered, this, &MainWindow::onSearchAnchorsClicked);

    auto* ruleMenu = bar->addMenu("규칙 관리");
    auto* addRuleAction = ruleMenu->addAction("+ 새 규칙 추가");
    connect(addRuleAction, &QAction::triggered, this, &MainWindow::onAddRuleClicked);
}

std::vector<std::pair<std::string, geometry::ModelHandle>> MainWindow::BuildLoadedModelList() const {
    // inchFiles_는 onImportStepClicked에서 이미 인치 오름차순(소형->대형)으로 정렬해
    // 두므로, 순서 그대로 반환하면 된다. handle도 이미 로드된 것을 재사용 - 다시
    // 파싱하지 않는다.
    std::vector<std::pair<std::string, geometry::ModelHandle>> models;
    for (const auto& entry : inchFiles_) {
        const QString label = entry.inch > 0
            ? QString("%1\"  %2").arg(entry.inch).arg(QFileInfo(QString::fromStdString(entry.filePath)).fileName())
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
    // 규칙 관리가 비모달로 열려 있는 동안 STEP을 다시 불러오면 이전 패널이 통째로
    // 교체된다 - 다이얼로그가 그 패널을 가리키던 포인터를 계속 들고 있으면 댕글링되므로
    // 새 패널로 다시 연결해준다.
    if (activeRuleDialog_) {
        activeRuleDialog_->RewireViewerPanel(viewerPanel_);
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
    // 규칙 이름 칸(0)은 내용 길이에 맞추고, 인치 값 칸들은 서로 비교하기 쉽도록 폭을 통일한다.
    comparisonTable_->resizeColumnToContents(0);
    comparisonTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    for (int col = 1; col < comparisonTable_->columnCount(); ++col) {
        comparisonTable_->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);
    }
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
    auto* dialog = new RuleEditorDialog(&db_, projectId_, adapter_.get(), viewerPanel_, BuildLoadedModelList(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    activeRuleDialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this]() { activeRuleDialog_ = nullptr; });
    connect(dialog, &RuleEditorDialog::rulesChanged, this, &MainWindow::refreshComparisonTable);
    dialog->show();
}

// § 포인트 검색 - anchor_type(+부품명/지름)을 입력하면 현재 로드된 인치 중 하나를
// 골라 실제 매칭 후보를 미리 보여준다. 아직 STEP을 하나도 안 불러왔으면 검색할
// 대상이 없다는 뜻이라 바로 안내만 하고 다이얼로그를 열지 않는다.
void MainWindow::onSearchAnchorsClicked() {
    const auto models = BuildLoadedModelList();
    if (models.empty()) {
        QMessageBox::information(this, "검색 대상 없음", "먼저 STEP 파일을 불러온 뒤 검색하세요.");
        return;
    }
    AnchorSearchDialog dialog(adapter_.get(), models, this);
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

    try {
        report::ExportComparisonCsv(path.toStdString(), lastReports_);
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
