#include "ui/RuleEditorDialog.h"

#include "rule/BuiltInCatalog.h"
#include "ui/AnchorSearchDialog.h"
#include "viewer/MultiViewportPanel.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ui {

namespace {
// § 이미지 영역 확대(2026-09-11) - "포인트 찍으려는데 이미지가 작아서 잘 안 보인다"는
// 피드백에 따라 큰 폭으로 키웠다. 라이브 뷰어 내장으로 바뀐 뒤에도(§ 헤더 주석 참고)
// 그대로 editorViewerPanel_의 최소 크기로 쓴다 - 고정 크기가 아니라 "최소 크기 +
// Expanding 정책"이라 다이얼로그를 더 키우면 뷰어도 같이 커진다.
constexpr int kImagePreviewWidth = 860;
constexpr int kImagePreviewHeight = 520;
constexpr int kLeftColumnWidth = 260;
} // namespace

RuleEditorDialog::RuleEditorDialog(
    database::Database* db, int projectId, geometry::IGeometryAdapter* adapter,
    const std::vector<std::pair<std::string, geometry::ModelHandle>>& models, QWidget* parent)
    : QDialog(parent), db_(db), projectId_(projectId), adapter_(adapter), models_(models) {
    setWindowTitle("규칙 관리");
    resize(1300, 1150);

    // ---- 좌측: 저장된 규칙 목록 ----
    table_ = new QTableWidget(this);
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({"이름", "측정 타입"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(table_, &QTableWidget::cellClicked, this, &RuleEditorDialog::onRowClicked);

    auto* deleteRuleButton = new QPushButton("선택 항목 삭제", this);
    connect(deleteRuleButton, &QPushButton::clicked, this, &RuleEditorDialog::onDeleteClicked);

    auto* leftLayout = new QVBoxLayout();
    leftLayout->addWidget(new QLabel("저장된 규칙", this));
    leftLayout->addWidget(table_, 1);
    leftLayout->addWidget(deleteRuleButton);
    auto* leftWidget = new QWidget(this);
    leftWidget->setLayout(leftLayout);
    leftWidget->setFixedWidth(kLeftColumnWidth);

    // § 대표 인치 선택(2026-09-11) - "여러 인치를 다 보여주니 헷갈리고, 어차피 대표
    // 인치 하나만 이미지로 쓸 거다"라는 피드백에 따라, 내장 뷰어가 항상 이 콤보에서 고른
    // 인치 하나만 보여주게 한다. 실제 채우기는 RebuildEditorPanel()이 한다(models_가
    // 아직 비어있을 수 있는 이 시점엔 항목이 없다).
    targetModelCombo_ = new QComboBox(this);
    auto* targetModelRow = new QHBoxLayout();
    targetModelRow->addWidget(new QLabel("대상 인치(대표):", this));
    targetModelRow->addWidget(targetModelCombo_, 1);
    connect(targetModelCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) {
            RebuildEditorPanel();
            // § 전장 사이즈 자동 포인트 - 대표 인치를 바꾸면 overall_size 규칙의 최외곽
            // 포인트도 새 모델 기준으로 다시 찾아야 한다(이전 인치의 좌표가 남아있으면
            // 마커가 새 뷰어의 모델과 안 맞는다).
            AutoDetectOverallSizePointsIfApplicable();
            refreshPointsTable();
            RefreshMarkers();
        }
    });

    // ---- 우측: 측정 뷰어(라이브 3D, editorViewerPanel_는 아래 RebuildEditorPanel()이
    // 채워 넣는다) 아래 캡쳐 버튼 ----
    captureImageButton_ = new QPushButton("현재 화면 캡쳐", this);
    connect(captureImageButton_, &QPushButton::clicked, this, &RuleEditorDialog::onCaptureRuleImageClicked);

    // § 캡쳐 영역 드래그 지정 - "뷰어 전체 캡쳐라 배경이 너무 많다"는 피드백에 따라,
    // 위 버튼(활성 뷰포트 전체)과 별개로 사용자가 직접 사각형을 드래그해서 원하는
    // 영역만 잘라 캡쳐할 수 있게.
    captureRegionButton_ = new QPushButton("드래그로 캡쳐 영역 지정", this);
    connect(captureRegionButton_, &QPushButton::clicked, this, &RuleEditorDialog::onCaptureRegionClicked);

    // § 뷰어 카메라 잠금(2026-09-11) - "캡쳐 전후 카메라 on/off가 있으면 좋겠다. 움직이고
    // 싶을 때 on, 고정하고 싶을 때 off"라는 요청. 기본은 꺼짐(=조작 가능)으로 두고,
    // 사용자가 포인트를 클릭할 때 실수로 카메라가 움직이지 않게 체크하면 잠긴다.
    // RebuildEditorPanel()이 editorViewerPanel_를 새로 만들 때마다 체크 상태를 다시
    // 적용해줘야 한다(새 패널은 항상 잠금 해제 상태로 시작하므로).
    cameraLockCheck_ = new QCheckBox("뷰어 카메라 잠금", this);
    connect(cameraLockCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (editorViewerPanel_) {
            editorViewerPanel_->setCameraLocked(checked);
        }
    });

    auto* captureButtonsRow = new QHBoxLayout();
    captureButtonsRow->addWidget(captureImageButton_);
    captureButtonsRow->addWidget(captureRegionButton_);
    captureButtonsRow->addStretch(1);
    captureButtonsRow->addWidget(cameraLockCheck_);

    pickStatusLabel_ = new QLabel(this);
    pickStatusLabel_->setStyleSheet("color: #b05000; font-weight: bold;");
    pickStatusLabel_->setWordWrap(true);
    pickStatusLabel_->hide();

    // ---- 우측: 1) 규칙 이름 ----
    // § 화면 재구성(2026-09-11) - "안내 박스 삭제하고, 1)규칙이름 2)포인트 측정
    // 3)관리 항목처럼 번호로 구분해달라"는 요청에 따라 섹션을 다시 짰다. "번호 라벨과
    // 입력칸을 한 줄에 배치해달라"는 후속 요청에 따라 세로 쌓기 대신 가로로 붙였다.
    name_ = new QLineEdit(this);
    auto* nameSection = new QHBoxLayout();
    nameSection->addWidget(new QLabel("1) 규칙 이름", this));
    nameSection->addWidget(name_, 1);

    // ---- 우측: 2) 포인트 측정(지정/축방향/추가/삭제 컨트롤 - 전부 한 줄) ----
    nextPointDirection_ = new QComboBox(this);
    nextPointDirection_->addItems({"(필터 안 씀)", "X", "Y", "Z"});

    pickPointButton_ = new QPushButton("지정...", this);
    connect(pickPointButton_, &QPushButton::clicked, this, &RuleEditorDialog::onPickPointClicked);
    searchPointButton_ = new QPushButton("검색으로 추가...", this);
    connect(searchPointButton_, &QPushButton::clicked, this, &RuleEditorDialog::onSearchPointClicked);
    deletePointButton_ = new QPushButton("선택 포인트 삭제", this);
    connect(deletePointButton_, &QPushButton::clicked, this, &RuleEditorDialog::onDeletePointClicked);

    auto* pointMeasureSection = new QHBoxLayout();
    pointMeasureSection->addWidget(new QLabel("2) 포인트 측정", this));
    pointMeasureSection->addSpacing(8);
    pointMeasureSection->addWidget(new QLabel("축 방향(구멍/평면 전용):", this));
    pointMeasureSection->addWidget(nextPointDirection_);
    pointMeasureSection->addStretch(1);
    pointMeasureSection->addWidget(pickPointButton_);
    pointMeasureSection->addWidget(searchPointButton_);
    pointMeasureSection->addWidget(deletePointButton_);

    // ---- 우측: 3) 관리 항목 ----
    // § 표 재설계(2026-09-11) - "지정을 누르면 포인트 2개를 연달아 찍고, 관리 항목엔
    // 한 줄만 생기게 하자"는 요청에 따라 "포인트 1개=행 1개"에서 "치수(포인트 2개
    // 묶음) 1개=행 1개"로 바꿨다. 그래서 표는 이제 항상 정확히 1행이고, 각 열이 전부
    // 위젯 하나씩만 담아서(이전처럼 행이 늘어도 위젯이 사라지지 않게 신경 쓸 필요가
    // 없어졌다 - 행이 아예 안 느니까) setSpan 같은 것도 필요 없다.
    // 열: INCH / Point(부위, 치수 이니셜 직접 입력) / 종류(Dimension 등 드롭다운) /
    // 측정타입(point_to_point 등 상세 드롭다운). 예전 "축방향"/"종류(형상)" 열은
    // "필요없다"는 요청으로 뺐다(값 자체는 points_에 그대로 남아 BuildRule()이 계속 씀 -
    // 표시만 안 할 뿐).
    pointsTable_ = new QTableWidget(this);
    // § 표에 공차/Projection/Selector 합치기(2026-09-12) - "관리항목 표 밑에 있는
    // 정보들(공차/Projection/Selector)을 표 오른쪽 끝에 셀로 추가하자"는 요청. 예전엔
    // 표 밑에 별도 QFormLayout(bottomForm)으로 떨어져 있어서 "관리 항목" 한 줄 요약이라는
    // 표의 취지와 분리돼 있었다. 이제 표 열 4개(INCH/Point(부위)/종류/측정타입) 뒤에
    // 공차 +/공차 -/Projection/Selector 4칸을 이어붙여 전부 한 줄에서 보이게 했다.
    pointsTable_->setColumnCount(8);
    pointsTable_->setHorizontalHeaderLabels(
        {"INCH", "Point(부위)", "종류", "측정타입", "공차 +", "공차 -", "Projection", "Selector"});
    // § 표 폭 문제 수정(2026-09-12) - "표에 INCH 칸만 보이고 나머지 칸이 안 보인다,
    // 가로 비율 맞춰서 키워달라"는 리포트. QSizePolicy::Maximum이 표를 sizeHint(=각 열
    // ResizeToContents 합)만큼만 좁게 그리는데, INCH 칸 내용이 길면 그 sizeHint 자체가
    // 커지면서도 스크롤바가 꺼져 있어 나머지 칸이 화면 밖으로 밀려 안 보이는 문제였다.
    // 마지막 열(Selector)만 Stretch로 남는 폭을 흡수하게 해서 표가 오른쪽 패널 폭에 맞게
    // 자연스럽게 커지도록 했다. 혹시라도 폭이 부족한 경우를 대비해 가로 스크롤바도
    // AlwaysOff 대신 AsNeeded로 되돌려 안전망을 남겨둔다.
    for (int col = 0; col < pointsTable_->columnCount() - 1; ++col) {
        pointsTable_->horizontalHeader()->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    }
    pointsTable_->horizontalHeader()->setSectionResizeMode(
        pointsTable_->columnCount() - 1, QHeaderView::Stretch);
    pointsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pointsTable_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pointsTable_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    pointsTable_->verticalHeader()->hide();
    pointsTable_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    pointsTable_->setRowCount(1);

    ctqCode_ = new QLineEdit(this);
    ctqCode_->setPlaceholderText("예: D");
    // § 치수선 중앙 라벨 - 타이핑하는 즉시 뷰어의 노란 라벨도 갱신되게.
    connect(ctqCode_, &QLineEdit::textChanged, this, [this]() { RefreshMarkers(); });
    pointsTable_->setCellWidget(0, 1, ctqCode_);

    checkPointCategory_ = new QComboBox(this);
    checkPointCategory_->addItems({"(미지정)", "Dimension", "Height", "Angle", "Gap", "Pitch", "Count"});
    pointsTable_->setCellWidget(0, 2, checkPointCategory_);

    measurementType_ = new QComboBox(this);
    // § 전장 사이즈 콤보 누락 수정(2026-09-12) - "overall_size"가 이 목록에 없어서
    // BuiltInRules()의 전장 사이즈 규칙을 불러오면 setCurrentText()가 조용히 실패하고
    // 콤보가 기본값(point_to_point)에 머물렀다 - 그래서 "포인트가 정확히 2개 필요합니다"
    // 오류가 엉뚱하게 떴었다(실제로는 OverallSize라 포인트가 전혀 필요 없는데도).
    measurementType_->addItems({"point_to_point", "point_to_plane", "axis_projection", "face_to_face_gap",
                                 "instance_count", "min_pitch", "overall_size"});
    // § 사용자가 자동 추론 결과를 수동으로 바꾸면 표의 INCH/마커도 그 값 기준으로 다시
    // 계산해야 한다(특히 point_to_plane 여부가 기준면 판단에 영향). overall_size를 고르면
    // 축(Projection) 기준 최외곽 포인트를 자동으로 찾아 채운다(아래 참고).
    connect(measurementType_, &QComboBox::currentTextChanged, this, [this]() {
        AutoDetectOverallSizePointsIfApplicable();
        refreshPointsTable();
        RefreshMarkers();
    });
    pointsTable_->setCellWidget(0, 3, measurementType_);

    tolerancePlus_ = new QDoubleSpinBox(this);
    tolerancePlus_->setRange(0.0, 100.0);
    tolerancePlus_->setDecimals(3);
    tolerancePlus_->setSuffix(" mm");
    pointsTable_->setCellWidget(0, 4, tolerancePlus_);

    toleranceMinus_ = new QDoubleSpinBox(this);
    toleranceMinus_->setRange(0.0, 100.0);
    toleranceMinus_->setDecimals(3);
    toleranceMinus_->setSuffix(" mm");
    pointsTable_->setCellWidget(0, 5, toleranceMinus_);

    projection_ = new QComboBox(this);
    projection_->addItems({"3D", "normal", "X", "Y", "Z"});
    // § 전장 사이즈 자동 포인트 - 측정타입이 overall_size일 때 Projection이 "이 측정이
    // 어느 축인지"를 그대로 겸한다(BuiltInCatalog와 동일 규약). 축을 바꾸면 최외곽
    // 포인트도 그 축 기준으로 다시 찾는다.
    connect(projection_, &QComboBox::currentTextChanged, this, [this]() {
        AutoDetectOverallSizePointsIfApplicable();
        refreshPointsTable();
        RefreshMarkers();
    });
    pointsTable_->setCellWidget(0, 6, projection_);

    selector_ = new QComboBox(this);
    selector_->addItems(
        {"(없음)", "nearest_pair", "leftmost", "rightmost", "nearest_face_pair", "parallel_face_pair"});
    pointsTable_->setCellWidget(0, 7, selector_);

    // § 포인트 진행 상태 - "포인트 0/2 선택됨" 같은 안내. refreshPointsTable()이 채운다.
    pointProgressLabel_ = new QLabel(this);
    pointProgressLabel_->setStyleSheet("color: #555; font-size: 11px;");

    // § 세로 여백 축소(2026-09-12) - "지정 가능한 치수항목이 하나로 정해져 있으면 표
    // 이미지의 여백이 있을 필요가 없다, 더 축소해달라"는 요청(1차로 "3줄 정도"로 줄인 뒤
    // 재요청). 행이 정확히 1개뿐이라 헤더 높이 + 실제 데이터 행 높이만 있으면 충분하다 -
    // 근사치(글꼴 높이*3) 대신 위젯들이 다 꽂힌 뒤의 실제 sizeHint를 그대로 쓴다.
    pointsTable_->resizeRowsToContents();
    const int exactHeight =
        pointsTable_->horizontalHeader()->sizeHint().height() + pointsTable_->rowHeight(0) + 2;
    pointsTable_->setMaximumHeight(exactHeight);

    auto* manageSection = new QVBoxLayout();
    manageSection->addWidget(new QLabel("3) 관리 항목", this));
    manageSection->addWidget(pointsTable_);
    manageSection->addWidget(pointProgressLabel_);

    addOrUpdateButton_ = new QPushButton("규칙 추가", this);
    connect(addOrUpdateButton_, &QPushButton::clicked, this, &RuleEditorDialog::onAddOrUpdateClicked);
    // § 같은 캡쳐에서 여러 CTQ 항목 만들기 - "한 캡쳐로 여러 측정 항목을 만들 수도
    // 있는데" 질문에 대한 답으로 추가. 지금 폼 내용을 (수정 중이던 규칙을 덮어쓰지 않고)
    // 새 규칙으로 하나 더 저장한다 - 이름/CTQ 코드만 바꿔서 반복 저장하기 좋다.
    saveAsButton_ = new QPushButton("다른 이름으로 저장", this);
    connect(saveAsButton_, &QPushButton::clicked, this, &RuleEditorDialog::onSaveAsClicked);
    auto* saveButtonsRow = new QHBoxLayout();
    saveButtonsRow->addWidget(addOrUpdateButton_);
    saveButtonsRow->addWidget(saveAsButton_);

    // editorViewerPanel_ 자리는 RebuildEditorPanel()이 index 1(레이블 바로 다음)에
    // insertWidget()으로 채운다 - 그래야 STEP 재로드로 패널을 통째로 갈아끼울 때도
    // (RewireModels) 같은 자리에 다시 꽂을 수 있다.
    rightLayout_ = new QVBoxLayout();
    rightLayout_->addWidget(new QLabel("측정 뷰어", this));
    rightLayout_->addLayout(targetModelRow);
    rightLayout_->addLayout(captureButtonsRow);
    rightLayout_->addWidget(pickStatusLabel_);
    rightLayout_->addLayout(nameSection);
    rightLayout_->addLayout(pointMeasureSection);
    rightLayout_->addLayout(manageSection);
    rightLayout_->addLayout(saveButtonsRow);
    rightLayout_->addStretch(1);
    auto* rightWidget = new QWidget(this);
    rightWidget->setLayout(rightLayout_);

    // § 창 크기 축소 시 무반응 수정(2026-09-11) - "창을 줄이면 스크롤바가 생겨야 하는데
    // 전혀 안 움직인다"는 피드백. editorViewerPanel_이 860x520 최소 크기를 강제하는 등
    // 우측 패널의 최소 높이가 다이얼로그보다 커질 수 있는데, 스크롤 영역 없이 QWidget을
    // 바로 넣으면 Qt가 그 최소 크기 이하로는 창 자체를 줄이지 못하게 막아버린다(스크롤바
    // 없이 그냥 안 줄어듦). QScrollArea로 감싸면 창을 줄였을 때 내용이 잘리는 대신
    // 스크롤바가 생긴다.
    auto* rightScroll = new QScrollArea(this);
    rightScroll->setWidget(rightWidget);
    rightScroll->setWidgetResizable(true);
    rightScroll->setFrameShape(QFrame::NoFrame);

    auto* topRow = new QHBoxLayout();
    topRow->addWidget(leftWidget);
    topRow->addWidget(rightScroll, 1);

    auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(closeButtons, &QDialogButtonBox::accepted, this, &QDialog::close);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(topRow, 1);
    mainLayout->addWidget(closeButtons);

    PopulateTargetModelCombo();
    RebuildEditorPanel();
    refreshPointsTable();
    refreshTable();
}

void RuleEditorDialog::RewireModels(const std::vector<std::pair<std::string, geometry::ModelHandle>>& models) {
    models_ = models;
    PopulateTargetModelCombo();
    RebuildEditorPanel();
}

void RuleEditorDialog::PopulateTargetModelCombo() {
    const QSignalBlocker blocker(targetModelCombo_);
    const geometry::ModelHandle previousHandle = targetModelCombo_->currentIndex() >= 0
        ? static_cast<geometry::ModelHandle>(targetModelCombo_->currentData().toInt())
        : geometry::kInvalidModelHandle;
    targetModelCombo_->clear();
    int newIndex = 0;
    for (size_t i = 0; i < models_.size(); ++i) {
        targetModelCombo_->addItem(QString::fromStdString(models_[i].first), models_[i].second);
        if (models_[i].second == previousHandle) {
            newIndex = static_cast<int>(i);
        }
    }
    if (targetModelCombo_->count() > 0) {
        targetModelCombo_->setCurrentIndex(newIndex);
    }
}

void RuleEditorDialog::RebuildEditorPanel() {
    if (editorViewerPanel_) {
        editorViewerPanel_->disconnect(this);
        rightLayout_->removeWidget(editorViewerPanel_);
        editorViewerPanel_->deleteLater();
        editorViewerPanel_ = nullptr;
    }
    // § 대표 인치 선택 - 전체 models_가 아니라 targetModelCombo_가 가리키는 모델 하나만
    // 담아서 만든다. "여러 인치가 다 보여서 헷갈린다"는 피드백 대응 - 어차피 캡쳐 이미지도
    // 대표 인치 하나만 쓴다.
    std::vector<std::pair<std::string, geometry::ModelHandle>> targetModel;
    const int targetIndex = targetModelCombo_->currentIndex();
    if (targetIndex >= 0 && targetIndex < static_cast<int>(models_.size())) {
        targetModel.push_back(models_[static_cast<size_t>(targetIndex)]);
    }
    editorViewerPanel_ = new viewer::MultiViewportPanel(adapter_, targetModel, this);
    // 고정 크기 대신 "최소 크기 + Expanding" - 다이얼로그 폭을 넓게 잡아뒀으니(1300px)
    // 레이아웃이 남는 공간을 뷰어에 최대한 몰아준다.
    editorViewerPanel_->setMinimumSize(kImagePreviewWidth, kImagePreviewHeight);
    editorViewerPanel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(editorViewerPanel_, &viewer::MultiViewportPanel::facePicked, this, &RuleEditorDialog::onFacePicked);
    connect(editorViewerPanel_, &viewer::MultiViewportPanel::captureRegionGrabbed, this,
            &RuleEditorDialog::onCaptureRegionGrabbed);
    // § 카메라 회전 시 치수선 축 자동 재선택(2026-09-13) - "화면뷰가 바뀔 때마다 최적
    // 위치로 업데이트해달라"는 요청. 사용자가 직접 드래그로 조정한 적이 없을 때만
    // 다시 계산한다(dimensionOffsetManuallyAdjusted_ 참고 - 드래그가 우선).
    connect(editorViewerPanel_, &viewer::MultiViewportPanel::cameraChanged, this, [this]() {
        if (!dimensionOffsetManuallyAdjusted_) {
            AutoDetectOverallSizePointsIfApplicable();
            refreshPointsTable();
            RefreshMarkers();
        }
    });
    // § 치수선 드래그 - 라벨을 클릭+드래그하면 여기로 델타(월드 단위)가 들어온다.
    connect(editorViewerPanel_, &viewer::MultiViewportPanel::dimensionOffsetDragged, this,
            &RuleEditorDialog::onDimensionOffsetDragged);
    // index 2 = "측정 뷰어" 레이블(0) + 대상 인치 콤보 행(1) 다음.
    rightLayout_->insertWidget(2, editorViewerPanel_, 1);

    // 패널이 바뀌면 이전 패널 기준으로 켜뒀던 피킹/캡쳐영역 대기 상태는 의미가 없어진다.
    pickArmed_ = false;
    pickStatusLabel_->hide();
    RefreshMarkers(); // 새 패널은 마커를 하나도 모르므로 지금까지 찍은 포인트를 다시 넘겨준다.
    // 새 패널은 항상 잠금 해제 상태로 시작하므로, 체크박스가 이미 켜져 있었다면 다시 적용.
    if (cameraLockCheck_) {
        editorViewerPanel_->setCameraLocked(cameraLockCheck_->isChecked());
    }
}

void RuleEditorDialog::RefreshMarkers() {
    if (!editorViewerPanel_) {
        return;
    }
    std::vector<QVector3D> positions;          // 마커(실제 측정 지점)
    std::vector<QVector3D> dimensionLinePoints; // 치수선이 실제로 그려질 위치
    std::vector<QVector3D> extensionFrom;       // 가이드선 시작(실제 측정 지점)
    std::vector<QVector3D> extensionTo;         // 가이드선 끝(치수선 위치)

    // 실제 좌표를 가진 포인트만 따로 모은다 - 아래 축 정렬 치수선 분기와 일반 분기가 공유.
    std::vector<const PointEntry*> positioned;
    for (const auto& p : points_) {
        if (p.hasPosition) {
            positioned.push_back(&p);
        }
    }

    // § 축 정렬 치수선(2026-09-13) - "축 방향/Projection을 Z로 지정했는데 치수선이
    // 대각선"이라는 리포트. 수동으로 찍은 2점(전장 사이즈 자동 포인트가 아닌 일반 2점
    // 측정)은 실제 좌표를 그대로 이어서 항상 대각선이었다 - Projection이 X/Y/Z로
    // 지정되어 있으면 그 축 방향 거리만 재는 것이므로, 화면에 보이는 치수선도 그 축과
    // 나란한 직선이어야 한다. 두 점 중 첫 번째 점의 위치(다른 두 축 좌표)를 기준으로
    // 삼고, 두 번째 점은 그 축 좌표만 가져와 "첫 점의 열을 따라 내려온" 것처럼
    // 표시한다 - 실제 포인트(마커)는 그대로 두고, 치수선 표시 위치만 바꾸며 그 차이를
    // 가이드선으로 이어서(전장 사이즈 자동 포인트와 같은 시각 문법) 어디서 투영됐는지
    // 보여준다.
    std::string manualProjectionAxis;
    const QString projText = projection_->currentText();
    if (projText == "X" || projText == "Y" || projText == "Z") {
        manualProjectionAxis = projText.toStdString();
    }
    const bool useAxisAlignedManualLine = !manualProjectionAxis.empty() && positioned.size() == 2 &&
        !positioned[0]->hasDimensionOffset && !positioned[1]->hasDimensionOffset;

    if (useAxisAlignedManualLine) {
        const QVector3D trueA(
            static_cast<float>(positioned[0]->position.x), static_cast<float>(positioned[0]->position.y),
            static_cast<float>(positioned[0]->position.z));
        const QVector3D trueB(
            static_cast<float>(positioned[1]->position.x), static_cast<float>(positioned[1]->position.y),
            static_cast<float>(positioned[1]->position.z));
        positions = {trueA, trueB};

        QVector3D dispB = trueA; // A의 다른 두 축 좌표는 그대로, 지정된 축만 B값으로 교체.
        if (manualProjectionAxis == "X") {
            dispB.setX(trueB.x());
        } else if (manualProjectionAxis == "Y") {
            dispB.setY(trueB.y());
        } else {
            dispB.setZ(trueB.z());
        }
        QVector3D dispA = trueA;

        // § 드래그 지원(2026-09-13) - "노란 텍스트 박스가 드래그로 안 움직인다" 재확인
        // 요청. 전장 사이즈처럼 측정 축이 아닌 나머지 두 축 중 카메라에 더 잘 보이는
        // 쪽을 오프셋 축으로 고르고, 드래그로 누적된 manualDimensionOffsetDelta_만큼 두
        // 표시점을 함께 밀어서 실제 치수선(가이드선 포함)이 드래그를 따라 움직이게 한다.
        const std::string offsetAxis = ChooseOverallSizeOffsetAxis(manualProjectionAxis);
        currentOverallSizeOffsetAxis_ = offsetAxis;
        if (offsetAxis == "X") {
            dispA.setX(dispA.x() + manualDimensionOffsetDelta_);
            dispB.setX(dispB.x() + manualDimensionOffsetDelta_);
        } else if (offsetAxis == "Y") {
            dispA.setY(dispA.y() + manualDimensionOffsetDelta_);
            dispB.setY(dispB.y() + manualDimensionOffsetDelta_);
        } else {
            dispA.setZ(dispA.z() + manualDimensionOffsetDelta_);
            dispB.setZ(dispB.z() + manualDimensionOffsetDelta_);
        }

        dimensionLinePoints = {dispA, dispB};
        // 두 점 다 실제 위치 -> 표시 위치로 이어주는 가이드선을 그린다(차이가 없으면
        // 길이 0이라 안 보일 뿐, 생략해도 결과는 같다).
        extensionFrom = {trueA, trueB};
        extensionTo = {dispA, dispB};
    } else {
        for (const auto* pPtr : positioned) {
            const PointEntry& p = *pPtr;
            const QVector3D truePos(
                static_cast<float>(p.position.x), static_cast<float>(p.position.y),
                static_cast<float>(p.position.z));
            positions.push_back(truePos);
            if (p.hasDimensionOffset) {
                const QVector3D offsetPos(
                    static_cast<float>(p.dimensionOffsetPosition.x),
                    static_cast<float>(p.dimensionOffsetPosition.y),
                    static_cast<float>(p.dimensionOffsetPosition.z));
                dimensionLinePoints.push_back(offsetPos);
                extensionFrom.push_back(truePos);
                extensionTo.push_back(offsetPos);
            } else {
                // § 도면 설계법 치수선 - 축 지정이 없으면(3D/normal, 또는 포인트가
                // 1개/3개 등) 예전처럼 실제 포인트 위치에 바로 치수선을 그린다.
                dimensionLinePoints.push_back(truePos);
            }
        }
    }
    editorViewerPanel_->SetMarkerPositions(positions);
    // § CTQ 치수선 - 좌표를 아는 포인트가 2개면(대개 방금 찍은 2개) 그 사이에 선을 그어
    // 캡쳐 이미지가 바로 CTQ 문서용 그림이 되게 한다.
    editorViewerPanel_->SetDimensionLine(dimensionLinePoints);
    // § 도면 설계법 가이드선(2026-09-12) - "치수 표시선에 가이드선 양옆에 빼달라"는 요청.
    editorViewerPanel_->SetDimensionExtensionLines(extensionFrom, extensionTo);
    // § 치수선 중앙 라벨(2026-09-11) - "검은 박스 정리, 치수선 중앙에 Point(부위) 이니셜을
    // 노란 배경+볼드로" 요청. Point(부위)는 이제 사용자가 직접 입력하는 텍스트(ctqCode_)라
    // 그 값을 그대로 쓴다.
    editorViewerPanel_->SetDimensionLabel(ctqCode_->text());
    // § 치수선 드래그(2026-09-13) - 오프셋이 있는 포인트가 하나라도 있으면 그 오프셋 축의
    // 단위벡터를 알려줘서 뷰어가 드래그를 받을 수 있게 한다. 없으면(수동 피킹만 있을 때)
    // 0벡터를 보내 드래그를 비활성화한다.
    const bool hasAnyOffset = !extensionFrom.empty();
    QVector3D offsetAxisUnit;
    if (hasAnyOffset) {
        if (currentOverallSizeOffsetAxis_ == "X") {
            offsetAxisUnit = QVector3D(1.0f, 0.0f, 0.0f);
        } else if (currentOverallSizeOffsetAxis_ == "Y") {
            offsetAxisUnit = QVector3D(0.0f, 1.0f, 0.0f);
        } else if (currentOverallSizeOffsetAxis_ == "Z") {
            offsetAxisUnit = QVector3D(0.0f, 0.0f, 1.0f);
        }
    }
    editorViewerPanel_->SetDimensionOffsetAxis(offsetAxisUnit);
}

namespace {
void SetVecComponent(geometry::Vec3& v, const std::string& axis, double value) {
    if (axis == "X") {
        v.x = value;
    } else if (axis == "Y") {
        v.y = value;
    } else {
        v.z = value;
    }
}

std::pair<double, double> BoxRangeForAxis(const geometry::BoundingBox& box, const std::string& axis) {
    if (axis == "X") {
        return {box.min.x, box.max.x};
    }
    if (axis == "Y") {
        return {box.min.y, box.max.y};
    }
    return {box.min.z, box.max.z};
}
} // namespace

std::vector<RuleEditorDialog::PointEntry> RuleEditorDialog::AutoDetectOverallSizePoints(
    geometry::ModelHandle handle, const std::string& axis, const std::string& offsetAxis) const {
    std::vector<PointEntry> result;
    if (handle == geometry::kInvalidModelHandle || !adapter_) {
        return result;
    }
    if (axis != "X" && axis != "Y" && axis != "Z") {
        return result;
    }
    if (offsetAxis != "X" && offsetAxis != "Y" && offsetAxis != "Z") {
        return result;
    }

    const geometry::BoundingBox box = adapter_->GetBoundingBox(handle);
    // § 도면 설계법 스타일 치수선(2026-09-12) - "치수 표시선이 도면 설계법 기준으로
    // 도면 그림에서 빼서 여백에다가 표현해달라"는 요청. RuleEngine은 이 점들의 위치와
    // 무관하게 BoundingBox로 직접 값을 계산하므로(측정값에 영향 없음), 치수선을 모델
    // 바깥으로 밀어내도 표시되는 값은 그대로 정확하다.
    // "실제 측정 지점"(true, 모델 중앙을 지나는 점 - 마커로 표시)과 "치수선이 그려지는
    // 위치"(offset, 모델 바깥 여백)를 따로 계산해서 둘 다 PointEntry에 담는다 -
    // RefreshMarkers()가 이 둘을 가이드선(연장선)으로 이어서, 실제 도면처럼 "부품 →
    // 가이드선 → 치수선(여백)" 구조가 되게 한다. offsetAxis는 카메라 각도에 따라 달라질
    // 수 있어(§ 카메라 회전 시 자동 재선택) 파라미터로 받는다 - axis(측정 축) 자체와는
    // 별개다.
    geometry::Vec3 truePos;
    truePos.x = (box.min.x + box.max.x) / 2.0;
    truePos.y = (box.min.y + box.max.y) / 2.0;
    truePos.z = (box.min.z + box.max.z) / 2.0;

    geometry::Vec3 trueP1 = truePos;
    geometry::Vec3 trueP2 = truePos;
    const auto [measuredLo, measuredHi] = BoxRangeForAxis(box, axis);
    SetVecComponent(trueP1, axis, measuredLo);
    SetVecComponent(trueP2, axis, measuredHi);

    // 최소 5mm는 벌려서 여백 확보.
    const auto [offsetLo, offsetHi] = BoxRangeForAxis(box, offsetAxis);
    const double margin = std::max((offsetHi - offsetLo) * 0.15, 5.0);
    const double offsetValue = offsetHi + margin;
    geometry::Vec3 offsetP1 = trueP1;
    geometry::Vec3 offsetP2 = trueP2;
    SetVecComponent(offsetP1, offsetAxis, offsetValue);
    SetVecComponent(offsetP2, offsetAxis, offsetValue);

    const geometry::Vec3 truePoints[2] = {trueP1, trueP2};
    const geometry::Vec3 offsetPoints[2] = {offsetP1, offsetP2};
    for (int i = 0; i < 2; ++i) {
        PointEntry e;
        e.type = "Vertex"; // 수동 꼭짓점 피킹과 같은 anchorType 표기 규약(LoadRuleIntoForm 참고)
        e.kind = geometry::PickedFaceKind::Point;
        e.pointSubKind = geometry::PointSubKind::Vertex;
        e.hasPosition = true;
        e.position = truePoints[i];
        e.hasDimensionOffset = true;
        e.dimensionOffsetPosition = offsetPoints[i];
        e.modelHandle = handle;
        result.push_back(std::move(e));
    }
    return result;
}

std::string RuleEditorDialog::ChooseOverallSizeOffsetAxis(const std::string& measuredAxis) const {
    std::vector<std::string> candidates;
    if (measuredAxis == "X") {
        candidates = {"Y", "Z"};
    } else if (measuredAxis == "Y") {
        candidates = {"X", "Z"};
    } else {
        candidates = {"X", "Y"};
    }
    if (!editorViewerPanel_) {
        return candidates.front();
    }

    // § 뷰 프리셋 유도식 재사용(MultiViewportPanel.h 주석 참고) - 카메라가 보는 방향
    // (world space): worldDir = (cos(pitch)sin(yaw), -sin(pitch), -cos(pitch)cos(yaw)).
    const viewer::Camera& camera = editorViewerPanel_->sharedCamera();
    const double yawRad = camera.yawDeg * 0.017453292519943295; // deg -> rad
    const double pitchRad = camera.pitchDeg * 0.017453292519943295;
    const QVector3D forward(
        static_cast<float>(std::cos(pitchRad) * std::sin(yawRad)), static_cast<float>(-std::sin(pitchRad)),
        static_cast<float>(-std::cos(pitchRad) * std::cos(yawRad)));

    auto axisVector = [](const std::string& axis) {
        if (axis == "X") return QVector3D(1.0f, 0.0f, 0.0f);
        if (axis == "Y") return QVector3D(0.0f, 1.0f, 0.0f);
        return QVector3D(0.0f, 0.0f, 1.0f);
    };

    // 카메라 시선과 가장 수직인(=화면에 가장 크게 벌어져 보이는) 후보를 고른다 - 시선과
    // 거의 평행한 축으로 오프셋하면 치수선이 화면상 거의 한 점으로 찌그러져 안 보인다.
    std::string best = candidates.front();
    float bestAlignment = std::numeric_limits<float>::max();
    for (const auto& candidate : candidates) {
        const float alignment = std::abs(QVector3D::dotProduct(forward, axisVector(candidate)));
        if (alignment < bestAlignment) {
            bestAlignment = alignment;
            best = candidate;
        }
    }
    return best;
}

void RuleEditorDialog::AutoDetectOverallSizePointsIfApplicable() {
    const auto type = rule::MeasurementTypeFromString(measurementType_->currentText().toStdString());
    if (type != rule::MeasurementType::OverallSize) {
        return;
    }
    const std::string axis = projection_->currentText().toStdString();
    const geometry::ModelHandle handle = targetModelCombo_->currentIndex() >= 0
        ? static_cast<geometry::ModelHandle>(targetModelCombo_->currentData().toInt())
        : geometry::kInvalidModelHandle;
    // § 카메라 회전 시 자동 재선택 - 매번 현재 카메라 각도 기준으로 다시 고른다.
    const std::string offsetAxis = ChooseOverallSizeOffsetAxis(axis);
    const auto detected = AutoDetectOverallSizePoints(handle, axis, offsetAxis);
    if (!detected.empty()) {
        points_ = detected;
        autoChainSecondPoint_ = false;
        currentOverallSizeOffsetAxis_ = offsetAxis;
        // 새로 자동 계산했으니 이전 수동 드래그 조정은 더 이상 유효하지 않다.
        dimensionOffsetManuallyAdjusted_ = false;
    }
}

void RuleEditorDialog::onDimensionOffsetDragged(float deltaWorld) {
    if (currentOverallSizeOffsetAxis_.empty()) {
        return;
    }
    bool moved = false;
    for (auto& p : points_) {
        if (!p.hasDimensionOffset) {
            continue;
        }
        if (currentOverallSizeOffsetAxis_ == "X") {
            p.dimensionOffsetPosition.x += deltaWorld;
        } else if (currentOverallSizeOffsetAxis_ == "Y") {
            p.dimensionOffsetPosition.y += deltaWorld;
        } else {
            p.dimensionOffsetPosition.z += deltaWorld;
        }
        moved = true;
    }
    // § 수동 2점 축 정렬 치수선 드래그(2026-09-13) - "노란 텍스트 박스가 드래그로 안
    // 움직인다"는 재확인 요청. 이 경우 points_에는 hasDimensionOffset인 포인트가
    // 없어서(위 루프가 아무것도 못 옮겨서) 대신 누적 델타에 더해준다 - RefreshMarkers()가
    // 축 정렬 치수선을 계산할 때 이 델타를 반영한다.
    if (!moved) {
        manualDimensionOffsetDelta_ += deltaWorld;
    }
    // § 사용자가 직접 위치를 조정했으니, 카메라를 돌려도 자동 재선택이 이 값을 덮어쓰지
    // 않게 막는다(질문 답변: "사용자 드래그가 우선").
    dimensionOffsetManuallyAdjusted_ = true;
    RefreshMarkers();
}

void RuleEditorDialog::closeEvent(QCloseEvent* event) {
    if (editorViewerPanel_) {
        editorViewerPanel_->setPickModeActive(false);
        editorViewerPanel_->setCaptureRegionModeActive(false);
    }
    QDialog::closeEvent(event);
}

void RuleEditorDialog::refreshTable() {
    // § 기본값 규칙 편집(2026-09-12) - "규칙 추가 리스트에 전장 사이즈 기본값을 넣어두고
    // 거기서 공차를 수정할 수 있게 해달라"는 요청. 예전엔 DB 사용자 규칙만 보여줘서
    // BuiltInRules()(전장 사이즈 X/Y/Z)를 이 목록에서 선택/편집할 방법이 아예 없었다.
    // MergeWithBuiltIns()로 내장 기본값도 같이 보여주고(공차 등을 고쳐서 저장하면
    // id==0이라 SaveRule()이 새 사용자 규칙으로 INSERT하고, 이름이 같으므로 다음
    // refreshTable()부터는 그 사용자 저장본이 기본값 자리를 덮어쓴다).
    const auto userRules = db_->LoadRulesForProject(projectId_);
    rules_ = rule::MergeWithBuiltIns(userRules);
    table_->setRowCount(static_cast<int>(rules_.size()));
    for (size_t row = 0; row < rules_.size(); ++row) {
        const auto& r = rules_[row];
        table_->setItem(static_cast<int>(row), 0, new QTableWidgetItem(QString::fromStdString(r.name)));
        table_->setItem(static_cast<int>(row), 1,
                        new QTableWidgetItem(QString::fromStdString(rule::ToString(r.measurementType))));
    }
}

void RuleEditorDialog::refreshPointsTable() {
    // § 표 재설계(2026-09-11) - 표는 항상 정확히 1행("이 규칙=이 치수" 요약)이라 행
    // 개수 계산이나 setSpan이 더 이상 필요 없다. INCH만 points_에서 계산해서 채우고
    // (여러 포인트가 같은 모델이면 그 라벨, 아직 없으면 "-"), Point(부위)/종류/측정타입은
    // 이미 그 칸에 실제 위젯(ctqCode_/checkPointCategory_/measurementType_)이 꽂혀
    // 있어서 따로 채울 게 없다.
    // § 표 폭 문제 수정(2026-09-12) - "관리항목 표에 INCH 칸만 보이고 나머지 칸(Point(부위)/
    // 종류/측정타입)이 안 보인다"는 리포트. 원인은 model.first가 targetModelCombo_용
    // 라벨("50\"  파일명.stp")을 그대로 썼는데, 그 긴 문자열이 ResizeToContents로 INCH
    // 칸을 꽉 채워버려 Maximum 폭 안에서 나머지 칸이 밀려나 안 보이게 된 것. 여기선
    // 인치 숫자(+따옴표)만 잘라 쓰고, 전체 라벨은 툴팁으로 남겨둔다.
    QString modelLabel = "-";
    for (const auto& p : points_) {
        for (const auto& model : models_) {
            if (model.second == p.modelHandle) {
                modelLabel = QString::fromStdString(model.first);
                break;
            }
        }
        if (modelLabel != "-") {
            break;
        }
    }
    const int quoteIndex = modelLabel.indexOf('"');
    const QString inchLabel = quoteIndex >= 0 ? modelLabel.left(quoteIndex + 1) : modelLabel;
    auto* inchItem = new QTableWidgetItem(inchLabel);
    inchItem->setToolTip(modelLabel);
    pointsTable_->setItem(0, 0, inchItem);

    // § 포인트 진행 상태 안내 - "포인트 0/2 선택됨" 식.
    const auto type = rule::MeasurementTypeFromString(measurementType_->currentText().toStdString());
    const bool isSingleAnchor =
        type == rule::MeasurementType::InstanceCount || type == rule::MeasurementType::MinPitch;
    const int needed = isSingleAnchor ? 1 : 2;
    pointProgressLabel_->setText(
        QString("포인트 %1/%2 선택됨").arg(static_cast<int>(points_.size())).arg(needed));
}

void RuleEditorDialog::resetForm() {
    editingRuleId_ = 0;
    addOrUpdateButton_->setText("규칙 추가");
    name_->clear();
    ctqCode_->clear();
    checkPointCategory_->setCurrentIndex(0);
    measurementType_->setCurrentIndex(0);
    points_.clear();
    autoChainSecondPoint_ = false;
    currentOverallSizeOffsetAxis_.clear();
    dimensionOffsetManuallyAdjusted_ = false;
    manualDimensionOffsetDelta_ = 0.0f;
    refreshPointsTable();
    RefreshMarkers();
    nextPointDirection_->setCurrentIndex(0);
    selector_->setCurrentIndex(0);
    projection_->setCurrentIndex(0);
    tolerancePlus_->setValue(0.0);
    toleranceMinus_->setValue(0.0);
    table_->clearSelection();
    ruleImagePath_.clear();
    pickStatusLabel_->hide();
}

void RuleEditorDialog::onRowClicked(int row) {
    if (row < 0 || row >= static_cast<int>(rules_.size())) {
        return;
    }
    editingRuleId_ = rules_[static_cast<size_t>(row)].id;
    LoadRuleIntoForm(rules_[static_cast<size_t>(row)]);
    addOrUpdateButton_->setText("수정 저장");
}

void RuleEditorDialog::LoadRuleIntoForm(const rule::Rule& r) {
    // § 라이브 뷰어 내장 - 저장된 이미지는 CTQ 문서용 기록일 뿐, 이 다이얼로그의 뷰어는
    // 항상 라이브 상태를 보여준다(불러온 이미지를 다시 그 안에 띄우지 않는다). 이미지가
    // 있었다는 사실만 상태 라벨로 알려준다.
    ruleImagePath_ = r.imagePath.value_or(std::string());
    if (!ruleImagePath_.empty()) {
        pickStatusLabel_->setText(
            QString::fromStdString("저장된 캡쳐 이미지 있음: " + ruleImagePath_ + " (뷰어는 항상 라이브 상태)"));
        pickStatusLabel_->show();
    } else {
        pickStatusLabel_->hide();
    }

    name_->setText(QString::fromStdString(r.name));
    ctqCode_->setText(QString::fromStdString(r.ctqCode.value_or(std::string())));
    checkPointCategory_->setCurrentText(
        r.checkPointCategory.has_value() ? QString::fromStdString(*r.checkPointCategory) : "(미지정)");
    measurementType_->setCurrentText(QString::fromStdString(rule::ToString(r.measurementType)));

    points_.clear();
    autoChainSecondPoint_ = false;
    currentOverallSizeOffsetAxis_.clear();
    dimensionOffsetManuallyAdjusted_ = false;
    manualDimensionOffsetDelta_ = 0.0f;
    for (const auto& a : r.anchors) {
        PointEntry p;
        p.type = a.anchorType;
        p.partName = a.partName;
        p.diameterMm = a.paramValue.value_or(0.0);
        p.directionAxis = a.directionAxis.value_or(std::string());
        // § NX 스타일 포인트 스냅 - Rule 스키마는 kind를 직접 저장하지 않으므로
        // anchorType 문자열(onFacePicked가 쓰는 것과 동일)로 역추론한다. § NX 포인트
        // 생성자 스타일 확장(2026-09-13) - 시작점/끝점/중앙점/교차점 문자열도 인식한다
        // ("Vertex"는 그 확장 이전에 저장된 예전 데이터와의 호환용으로 계속 유지).
        if (a.anchorType == "Vertex") {
            p.kind = geometry::PickedFaceKind::Point;
            p.pointSubKind = geometry::PointSubKind::Vertex;
        } else if (a.anchorType == "Start_Point") {
            p.kind = geometry::PickedFaceKind::Point;
            p.pointSubKind = geometry::PointSubKind::StartPoint;
        } else if (a.anchorType == "End_Point") {
            p.kind = geometry::PickedFaceKind::Point;
            p.pointSubKind = geometry::PointSubKind::EndPoint;
        } else if (a.anchorType == "Edge_Midpoint") {
            p.kind = geometry::PickedFaceKind::Point;
            p.pointSubKind = geometry::PointSubKind::EdgeMidpoint;
        } else if (a.anchorType == "Center_Point") {
            p.kind = geometry::PickedFaceKind::Point;
            p.pointSubKind = geometry::PointSubKind::Center;
        } else if (a.anchorType == "Intersection_Point") {
            p.kind = geometry::PickedFaceKind::Point;
            p.pointSubKind = geometry::PointSubKind::Intersection;
        } else {
            p.kind = geometry::PickedFaceKind::Cylinder;
        }
        points_.push_back(std::move(p));
    }
    if (r.referencePlane.has_value()) {
        PointEntry p;
        p.type = r.referencePlane->planeType;
        p.partName = r.referencePlane->partName;
        p.directionAxis = r.referencePlane->normalAxis.value_or(std::string());
        p.kind = geometry::PickedFaceKind::Plane;
        points_.push_back(std::move(p));
    }
    refreshPointsTable();
    RefreshMarkers(); // DB에서 불러온 포인트는 좌표가 없어(hasPosition==false) 결국 다 지워짐.

    selector_->setCurrentText(r.selector.empty() ? "(없음)" : QString::fromStdString(r.selector.front()));
    if (!r.projection.empty()) {
        projection_->setCurrentText(QString::fromStdString(r.projection));
    }
    tolerancePlus_->setValue(r.tolerancePlusMm);
    toleranceMinus_->setValue(r.toleranceMinusMm);

    // § 전장 사이즈 자동 포인트(2026-09-13 수정) - "화살표가 안 보인다" 버그의 실제 원인.
    // 예전엔 "points_.empty()일 때만" 자동 재계산했는데, 한 번이라도 저장된 적 있는
    // 전장 사이즈 규칙은 anchors[](위 루프가 채움)가 비어있지 않다 - BuildRule()이 당시
    // points_를 그대로 anchors로 저장해두기 때문. 문제는 Anchor 스키마엔 좌표/오프셋을
    // 담을 필드가 없어서, 그 anchors로부터 되살린 PointEntry는 hasPosition/hasDimensionOffset이
    // 전부 false다 - "포인트는 있지만 좌표가 없는" 상태라 points_.empty()가 false가 되어
    // 자동 재계산을 건너뛰었고, 결과적으로 마커/치수선/화살표가 하나도 안 그려졌다(라벨만
    // 중간에 measurementType_ 시그널로 우연히 한 번 그려진 적 있으면 그 상태가 남아있을
    // 수 있음). OverallSize는 anchors 좌표 자체가 의미 없으므로(RuleEngine이 무시) 항상
    // 무조건 새로 계산한다 - points_ 비어있는지 여부와 무관하게.
    const bool isOverallSize = r.measurementType == rule::MeasurementType::OverallSize;
    if (points_.empty() || isOverallSize) {
        AutoDetectOverallSizePointsIfApplicable();
        refreshPointsTable();
        RefreshMarkers();
    }
}

rule::Rule RuleEditorDialog::BuildRule() const {
    rule::Rule r;
    r.id = editingRuleId_;
    r.name = name_->text().toStdString();
    r.measurementType = rule::MeasurementTypeFromString(measurementType_->currentText().toStdString());
    r.referenceFrame = {"World_Origin"};
    if (!ruleImagePath_.empty()) {
        r.imagePath = ruleImagePath_;
    }
    const std::string ctqCode = ctqCode_->text().trimmed().toStdString();
    if (!ctqCode.empty()) {
        r.ctqCode = ctqCode;
    }
    if (checkPointCategory_->currentText() != "(미지정)") {
        r.checkPointCategory = checkPointCategory_->currentText().toStdString();
    }

    auto makeAnchor = [](const PointEntry& p, const std::string& role) {
        rule::Anchor a;
        a.role = role;
        a.anchorType = p.type;
        a.partName = p.partName;
        if (p.diameterMm > 0.0) {
            a.paramKey = "diameter";
            a.paramValue = p.diameterMm;
        }
        if (!p.directionAxis.empty()) {
            a.directionAxis = p.directionAxis;
        }
        return a;
    };
    auto makePlane = [](const PointEntry& p) {
        rule::PlaneRef plane;
        plane.planeType = p.type;
        plane.partName = p.partName;
        if (!p.directionAxis.empty()) {
            plane.normalAxis = p.directionAxis;
        }
        return plane;
    };

    const bool isSingleAnchor = r.measurementType == rule::MeasurementType::InstanceCount ||
                                 r.measurementType == rule::MeasurementType::MinPitch;
    const bool isPointToPlane = r.measurementType == rule::MeasurementType::PointToPlane;

    if (isSingleAnchor) {
        if (!points_.empty()) {
            r.anchors.push_back(makeAnchor(points_[0], "single"));
        }
    } else if (isPointToPlane) {
        // 리스트에서 kind==Plane인 항목을 referencePlane으로, 나머지 하나를 anchor(점)로.
        const PointEntry* planeEntry = nullptr;
        const PointEntry* pointEntry = nullptr;
        for (const auto& p : points_) {
            if (p.kind == geometry::PickedFaceKind::Plane && !planeEntry) {
                planeEntry = &p;
            } else if (!pointEntry) {
                pointEntry = &p;
            }
        }
        if (pointEntry) {
            r.anchors.push_back(makeAnchor(*pointEntry, "single"));
        }
        if (planeEntry) {
            r.referencePlane = makePlane(*planeEntry);
        }
    } else {
        if (points_.size() >= 1) {
            r.anchors.push_back(makeAnchor(points_[0], "A"));
        }
        if (points_.size() >= 2) {
            r.anchors.push_back(makeAnchor(points_[1], "B"));
        }
    }

    const QString selectorText = selector_->currentText();
    if (selectorText != "(없음)") {
        r.selector = {selectorText.toStdString()};
    }

    r.projection = projection_->currentText().toStdString();
    r.tolerancePlusMm = tolerancePlus_->value();
    r.toleranceMinusMm = toleranceMinus_->value();

    return r;
}

bool RuleEditorDialog::ValidateFormForSave() {
    if (name_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "입력 필요", "규칙 이름은 비워둘 수 없습니다.");
        return false;
    }

    const auto type = rule::MeasurementTypeFromString(measurementType_->currentText().toStdString());
    const bool isSingleAnchor =
        type == rule::MeasurementType::InstanceCount || type == rule::MeasurementType::MinPitch;
    if (isSingleAnchor && points_.size() != 1) {
        QMessageBox::warning(this, "포인트 필요", "이 측정 타입은 포인트가 정확히 1개 필요합니다.");
        return false;
    }
    if (!isSingleAnchor && points_.size() != 2) {
        QMessageBox::warning(this, "포인트 필요", "이 측정 타입은 포인트가 정확히 2개 필요합니다.");
        return false;
    }
    if (type == rule::MeasurementType::PointToPlane) {
        const auto planeCount = std::count_if(points_.begin(), points_.end(), [](const PointEntry& p) {
            return p.kind == geometry::PickedFaceKind::Plane;
        });
        if (planeCount != 1) {
            QMessageBox::warning(
                this, "포인트 확인 필요",
                "point_to_plane은 포인트 1개(점) + 평면 1개가 필요합니다 - 평면을 클릭/검색으로 추가했는지 확인하세요.");
            return false;
        }
    }
    return true;
}

void RuleEditorDialog::onAddOrUpdateClicked() {
    if (!ValidateFormForSave()) {
        return;
    }

    const rule::Rule r = BuildRule();
    try {
        if (editingRuleId_ != 0) {
            db_->UpdateRule(editingRuleId_, r);
        } else {
            db_->SaveRule(projectId_, r);
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "저장 실패", QString::fromStdString(e.what()));
        return;
    }

    resetForm();
    refreshTable();
    emit rulesChanged();
}

void RuleEditorDialog::onSaveAsClicked() {
    if (!ValidateFormForSave()) {
        return;
    }

    // editingRuleId_와 무관하게 항상 새로 만든다 - BuildRule()이 editingRuleId_를
    // r.id에 넣어주긴 하지만 SaveRule()은 이 id를 안 쓰고(AUTOINCREMENT) 그냥 새
    // 행을 INSERT한다.
    const rule::Rule r = BuildRule();
    try {
        db_->SaveRule(projectId_, r);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "저장 실패", QString::fromStdString(e.what()));
        return;
    }

    resetForm();
    refreshTable();
    emit rulesChanged();
}

void RuleEditorDialog::onDeleteClicked() {
    const int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(rules_.size())) {
        return;
    }
    const int deletedId = rules_[static_cast<size_t>(row)].id;
    if (deletedId == 0) {
        // § 기본값 규칙 편집 - id==0은 DB에 없는 내장 기본값(전장 사이즈 등) 자체라
        // 지울 대상이 없다. 사용자가 그 위에 공차를 고쳐 저장한 적이 없으면 여기서 할
        // 일이 없고, 저장한 적이 있으면(같은 이름의 사용자 규칙이 생겼으면) 그 저장본이
        // 이 목록에서 이미 이 행을 대신하고 있으므로 id==0으로 보일 일이 없다.
        QMessageBox::information(this, "삭제 불가", "내장 기본값 규칙은 삭제할 수 없습니다.");
        return;
    }
    db_->DeleteRule(deletedId);
    if (editingRuleId_ == deletedId) {
        resetForm();
    }
    refreshTable();
    emit rulesChanged();
}

std::string RuleEditorDialog::CurrentDirectionFilter() const {
    const QString t = nextPointDirection_->currentText();
    return t == "(필터 안 씀)" ? std::string() : t.toStdString();
}

void RuleEditorDialog::AddPoint(const PointEntry& entry) {
    if (points_.size() >= 2) {
        QMessageBox::information(
            this, "추가 불가",
            "포인트는 최대 2개까지 추가할 수 있습니다 - 이미 2개가 있습니다. \"선택 포인트 삭제\"로 지우고 "
            "다시 시도하세요.");
        return;
    }
    points_.push_back(entry);
    // § refreshPointsTable()의 포인트 진행 상태 표시가 measurementType_의 현재 값을
    // 읽으므로, 자동 추론(point_to_point 등)이 반영된 *이후*에 표를 그린다.
    TryInferMeasurementType();
    refreshPointsTable();
    RefreshMarkers();
}

void RuleEditorDialog::onSearchPointClicked() {
    if (points_.size() >= 2) {
        QMessageBox::information(
            this, "추가 불가",
            "포인트는 최대 2개까지 추가할 수 있습니다 - 이미 2개가 있습니다. \"선택 포인트 삭제\"로 지우고 "
            "다시 시도하세요.");
        return;
    }
    AnchorSearchDialog dialog(adapter_, models_, this);
    dialog.SetInitialCriteria("", "", 0.0);
    if (dialog.exec() == QDialog::Accepted) {
        PointEntry entry;
        entry.type = dialog.AnchorType();
        entry.partName = dialog.PartName();
        entry.diameterMm = dialog.Diameter();
        entry.directionAxis = CurrentDirectionFilter();
        entry.kind = geometry::PickedFaceKind::Cylinder; // 검색(AnchorSearchDialog)은 원통만 다룸
        entry.modelHandle = dialog.SelectedModelHandle();
        AddPoint(entry);
    }
}

void RuleEditorDialog::onPickPointClicked() {
    if (!editorViewerPanel_) {
        QMessageBox::information(this, "지정 불가", "먼저 STEP 파일을 불러오세요.");
        return;
    }
    // § 포인트 재지정 UX(2026-09-12) - "관리항목에서 포인트 지정을 어떻게 수정하냐"는
    // 질문. 예전엔 이미 2개가 있으면 "선택 포인트 삭제"로 먼저 지운 뒤 다시 "지정..."을
    // 눌러야 했다(2단계). 이제 "지정..."을 다시 누르면 기존 포인트를 비우고 바로 새로
    // 2개를 찍기 시작한다 - "다시 지정하고 싶으면 그냥 지정 버튼을 다시 누르면 된다"로
    // 단순화(수동 삭제는 "이 규칙에서 포인트를 아예 없애고 싶을 때"만 남겨둠).
    if (!points_.empty()) {
        points_.clear();
        currentOverallSizeOffsetAxis_.clear();
        dimensionOffsetManuallyAdjusted_ = false;
        manualDimensionOffsetDelta_ = 0.0f;
        refreshPointsTable();
        RefreshMarkers();
    }
    // § "지정을 누르면 포인트 2개를 연달아 찍자" 요청(2026-09-11) - 이 버튼이 켜는 피킹은
    // 항상 "필요한 만큼(보통 2개) 자동으로 이어간다"는 뜻 - onFacePicked가 이 플래그를
    // 보고 1개 찍은 뒤 스스로 다시 무장한다.
    autoChainSecondPoint_ = true;
    pickArmed_ = true;
    pickStatusLabel_->setText(
        "피킹 대기 중 - 위 3D 뷰에서 클릭하세요. 모서리/꼭짓점 근처를 클릭하면 자동으로 그 점에 스냅되고(주황 점으로 "
        "표시), 그 외엔 클릭한 면(구멍은 원통, 그 외엔 평면)으로 인식됩니다. 포인트를 2개 연달아 찍습니다.");
    pickStatusLabel_->show();
    editorViewerPanel_->setPickModeActive(true);
}

void RuleEditorDialog::onDeletePointClicked() {
    // § 표 재설계(2026-09-11) - 표가 더 이상 "포인트별 행"이 아니라 "치수 1개=1행"이라
    // 특정 행을 선택해 지울 필요가 없어졌다 - 그냥 가장 최근에 찍은 포인트 1개를 지운다
    // (두 번 누르면 2개 다 지워짐).
    if (points_.empty()) {
        QMessageBox::information(this, "삭제할 포인트 없음", "아직 찍은 포인트가 없습니다.");
        return;
    }
    points_.pop_back();
    autoChainSecondPoint_ = false;
    refreshPointsTable();
    RefreshMarkers();
}

void RuleEditorDialog::onFacePicked(geometry::ModelHandle handle, QVector3D rayOrigin, QVector3D rayDir) {
    if (!pickArmed_) {
        return; // 이 다이얼로그가 피킹을 요청한 상태가 아니면 무시.
    }

    const geometry::Vec3 origin{rayOrigin.x(), rayOrigin.y(), rayOrigin.z()};
    const geometry::Vec3 dir{rayDir.x(), rayDir.y(), rayDir.z()};
    const geometry::PickResult result = adapter_->PickFace(handle, origin, dir);

    pickArmed_ = false;
    pickStatusLabel_->hide();
    if (editorViewerPanel_) {
        editorViewerPanel_->setPickModeActive(false);
    }

    if (result.kind == geometry::PickedFaceKind::None) {
        // § 연속 피킹 예약이 있었는데 이번 클릭이 실패했으면 취소한다 - 실패한 채로
        // 남겨두면 다음에 "지정..."을 다시 눌렀을 때 엉뚱하게 자동 연쇄가 걸릴 수 있다.
        autoChainSecondPoint_ = false;
        QMessageBox::information(this, "다시 클릭해주세요", "형상을 찾지 못했습니다 - 면 위를 클릭했는지 확인하세요.");
        return;
    }

    PointEntry entry;
    entry.kind = result.kind;
    entry.directionAxis = CurrentDirectionFilter();
    // § NX 스타일 포인트 스냅 - 라이브 뷰 클릭은 좌표를 알고 있으니(검색으로 추가한
    // 포인트와 달리) 뷰어에 마커로 다시 그릴 수 있다(RefreshMarkers).
    entry.hasPosition = true;
    entry.position = result.point;
    entry.modelHandle = handle; // § 리스트 항목 분류 - "INCH" 열에 쓴다.
    if (result.kind == geometry::PickedFaceKind::Cylinder) {
        entry.type = "Hole";
        entry.diameterMm = result.diameterMm;
    } else if (result.kind == geometry::PickedFaceKind::Point) {
        entry.pointSubKind = result.pointSubKind;
        // § NX 포인트 생성자 스타일 확장(2026-09-13) - "교차점/끝점/시작점/중앙점으로
        // 구분해달라"는 요청. anchorType 문자열은 §7 스키마/DB에 저장되는 값이라 각
        // 종류를 구분되는 문자열로 남긴다 - LoadRuleIntoForm이 역으로 읽어 pointSubKind를
        // 복원한다(예전 데이터의 "Vertex"/"Edge_Midpoint"도 계속 인식해야 함).
        switch (result.pointSubKind) {
            case geometry::PointSubKind::StartPoint: entry.type = "Start_Point"; break;
            case geometry::PointSubKind::EndPoint: entry.type = "End_Point"; break;
            case geometry::PointSubKind::EdgeMidpoint: entry.type = "Edge_Midpoint"; break;
            case geometry::PointSubKind::Center: entry.type = "Center_Point"; break;
            case geometry::PointSubKind::Intersection: entry.type = "Intersection_Point"; break;
            default: entry.type = "Vertex"; break;
        }
    } else {
        entry.type = "Datum_Plane";
    }
    AddPoint(entry);

    // § "지정 누르면 연달아 포인트 2개" - 아직 1개뿐이면 사용자가 다시 버튼을 누를
    // 필요 없이 스스로 피킹 모드를 재무장한다. instance_count/min_pitch처럼 1개만
    // 필요한 드문 경우엔 "선택 포인트 삭제"로 자동 추가된 2번째 점을 지우면 된다.
    if (autoChainSecondPoint_ && points_.size() < 2 && editorViewerPanel_) {
        pickArmed_ = true;
        pickStatusLabel_->setText(
            QString("포인트 %1/2 완료 - 두 번째 포인트를 이어서 클릭하세요.").arg(static_cast<int>(points_.size())));
        pickStatusLabel_->show();
        editorViewerPanel_->setPickModeActive(true);
    } else {
        autoChainSecondPoint_ = false;
    }
}

void RuleEditorDialog::TryInferMeasurementType() {
    if (points_.size() != 2) {
        return; // 둘 다 채워져야 조합을 판단할 수 있다.
    }
    // § NX 스타일 포인트 스냅 - Point(꼭짓점/모서리 중점)도 Cylinder(구멍 중심)처럼 "점"으로
    // 취급한다 - 실제 좌표 하나를 대표하는 값이라는 점에서 둘 다 같은 성격이다. Plane만
    // "면"으로 남는다.
    auto isPointLike = [](geometry::PickedFaceKind kind) {
        return kind == geometry::PickedFaceKind::Cylinder || kind == geometry::PickedFaceKind::Point;
    };
    const bool aPointLike = isPointLike(points_[0].kind);
    const bool bPointLike = isPointLike(points_[1].kind);
    if (aPointLike && bPointLike) {
        measurementType_->setCurrentText("point_to_point");
    } else if (aPointLike != bPointLike) {
        measurementType_->setCurrentText("point_to_plane");
    } else {
        measurementType_->setCurrentText("face_to_face_gap");
    }
}

void RuleEditorDialog::onCaptureRuleImageClicked() {
    if (!editorViewerPanel_) {
        QMessageBox::information(this, "캡쳐 불가", "먼저 STEP 파일을 불러오세요.");
        return;
    }
    // § 캡쳐 화질/구도 개선 - 패널 전체(툴바 포함) 대신 활성 뷰포트 하나만 찍는다.
    SaveCapturedImage(editorViewerPanel_->grabActiveViewport());
}

void RuleEditorDialog::onCaptureRegionClicked() {
    if (!editorViewerPanel_) {
        QMessageBox::information(this, "캡쳐 불가", "먼저 STEP 파일을 불러오세요.");
        return;
    }
    pickStatusLabel_->setText("드래그로 캡쳐 영역 지정 대기 중 - 위 3D 뷰에서 원하는 영역을 마우스로 드래그하세요.");
    pickStatusLabel_->show();
    editorViewerPanel_->setCaptureRegionModeActive(true);
    // § 라이브 뷰어 내장(2026-09-11) - 뷰어가 이제 이 다이얼로그 안에 있어서(예전처럼
    // 메인 창 뒤에 있지 않음) 드래그할 영역을 가릴 게 없다 - 예전의
    // showMinimized()/showNormal() 우회가 더 이상 필요 없다.
}

void RuleEditorDialog::onCaptureRegionGrabbed(QPixmap pixmap, viewer::ModelViewport::CaptureInfo /*info*/) {
    pickStatusLabel_->hide();
    if (editorViewerPanel_) {
        editorViewerPanel_->setCaptureRegionModeActive(false);
    }
    SaveCapturedImage(pixmap);
}

void RuleEditorDialog::SaveCapturedImage(const QPixmap& pixmap) {
    if (pixmap.isNull()) {
        QMessageBox::warning(this, "캡쳐 실패", "화면 캡쳐에 실패했습니다.");
        return;
    }

    QDir imagesDir(QDir::current().filePath("rule_images"));
    if (!imagesDir.exists() && !imagesDir.mkpath(".")) {
        QMessageBox::warning(this, "캡쳐 실패", "이미지 저장 폴더를 만들지 못했습니다: " + imagesDir.path());
        return;
    }
    const QString fileName = "rule_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + ".png";
    const QString fullPath = imagesDir.filePath(fileName);
    if (!pixmap.save(fullPath, "PNG")) {
        QMessageBox::warning(this, "캡쳐 실패", "이미지 저장에 실패했습니다: " + fullPath);
        return;
    }

    ruleImagePath_ = fullPath.toStdString();
    pickStatusLabel_->setText("이미지 저장됨: " + fileName);
    pickStatusLabel_->show();
}

} // namespace ui
