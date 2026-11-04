#include "ui/ShapePresetDialog.h"

#include "viewer/MultiViewportPanel.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace ui {

namespace {
constexpr int kViewerMinWidth = 640;
constexpr int kViewerMinHeight = 480;
} // namespace

ShapePresetDialog::ShapePresetDialog(
    database::Database* db, int projectId, geometry::IGeometryAdapter* adapter,
    const std::vector<std::pair<std::string, geometry::ModelHandle>>& models, QWidget* parent)
    : QDialog(parent), db_(db), projectId_(projectId), adapter_(adapter), models_(models) {
    setWindowTitle("형상 프리셋");
    resize(1100, 750);

    // ---- 좌측: 뷰어 + 새 프리셋 등록 ----
    modelCombo_ = new QComboBox(this);
    for (const auto& [label, handle] : models_) {
        modelCombo_->addItem(QString::fromStdString(label), handle);
    }
    connect(modelCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ShapePresetDialog::onModelComboChanged);

    viewerHost_ = new QWidget(this);
    auto* viewerHostLayout = new QVBoxLayout(viewerHost_);
    viewerHostLayout->setContentsMargins(0, 0, 0, 0);

    captureButton_ = new QPushButton("형상 클릭해서 등록...", this);
    connect(captureButton_, &QPushButton::clicked, this, &ShapePresetDialog::onCaptureClicked);
    // § 형상 프리셋 수동 선택(2026-09-18) - "자동으로 인접 면까지 묶는 게 의도와 다르게
    // 잡힌다"는 피드백에 따른 대안 경로. 면을 하나씩 클릭(추가)/Shift+클릭(제거)해서
    // 정확히 원하는 면들만 고를 수 있다.
    manualSelectButton_ = new QPushButton("면 직접 선택...", this);
    connect(manualSelectButton_, &QPushButton::clicked, this, &ShapePresetDialog::onManualSelectClicked);
    finishManualSelectButton_ = new QPushButton("선택 완료", this);
    finishManualSelectButton_->setVisible(false);
    connect(finishManualSelectButton_, &QPushButton::clicked, this,
            &ShapePresetDialog::onFinishManualSelectClicked);
    captureStatusLabel_ = new QLabel(
        "아직 캡처된 형상이 없습니다. \"형상 클릭해서 등록\" 또는 \"면 직접 선택\"을 누르거나, 바로 Ctrl+드래그로 "
        "면을 고르세요.",
        this);
    captureStatusLabel_->setWordWrap(true);

    nameEdit_ = new QLineEdit(this);
    nameEdit_->setPlaceholderText("예: FMB_HOOK");

    dimFilterKindCombo_ = new QComboBox(this);
    dimFilterKindCombo_->addItem("(선택) 치수 필터 안 씀", QString());
    dimFilterKindCombo_->addItem("지름", QString("diameter"));
    dimFilterKindCombo_->addItem("높이", QString("height"));
    dimFilterKindCombo_->addItem("폭", QString("width"));

    dimFilterValueSpin_ = new QDoubleSpinBox(this);
    dimFilterValueSpin_->setRange(0.0, 1000.0);
    dimFilterValueSpin_->setDecimals(2);
    dimFilterValueSpin_->setSuffix(" mm");

    findSameModelButton_ = new QPushButton("같은 모델에서 동일 형상 찾기", this);
    findSameModelButton_->setEnabled(false);
    connect(findSameModelButton_, &QPushButton::clicked, this, &ShapePresetDialog::onFindSameModelClicked);
    sameModelCountLabel_ = new QLabel(this);

    saveButton_ = new QPushButton("프리셋으로 저장", this);
    saveButton_->setEnabled(false);
    connect(saveButton_, &QPushButton::clicked, this, &ShapePresetDialog::onSaveClicked);

    auto* registerForm = new QFormLayout();
    registerForm->addRow("이름", nameEdit_);
    registerForm->addRow("치수 필터 종류", dimFilterKindCombo_);
    registerForm->addRow("치수 필터 값", dimFilterValueSpin_);

    auto* leftLayout = new QVBoxLayout();
    leftLayout->addWidget(new QLabel("대상 인치", this));
    leftLayout->addWidget(modelCombo_);
    leftLayout->addWidget(viewerHost_, 1);
    auto* captureButtonsRow = new QHBoxLayout();
    captureButtonsRow->addWidget(captureButton_);
    captureButtonsRow->addWidget(manualSelectButton_);
    captureButtonsRow->addWidget(finishManualSelectButton_);
    leftLayout->addLayout(captureButtonsRow);
    leftLayout->addWidget(captureStatusLabel_);
    leftLayout->addLayout(registerForm);
    leftLayout->addWidget(findSameModelButton_);
    leftLayout->addWidget(sameModelCountLabel_);
    leftLayout->addWidget(saveButton_);
    auto* leftWidget = new QWidget(this);
    leftWidget->setLayout(leftLayout);

    // ---- 우측: 저장된 프리셋 목록 ----
    presetsTable_ = new QTableWidget(this);
    presetsTable_->setColumnCount(3);
    presetsTable_->setHorizontalHeaderLabels({"이름", "치수 필터", "값(mm)"});
    presetsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    presetsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    presetsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(presetsTable_, &QTableWidget::itemSelectionChanged, this, &ShapePresetDialog::onTableSelectionChanged);

    deleteButton_ = new QPushButton("선택 프리셋 삭제", this);
    deleteButton_->setEnabled(false);
    connect(deleteButton_, &QPushButton::clicked, this, &ShapePresetDialog::onDeleteClicked);

    auto* rightLayout = new QVBoxLayout();
    rightLayout->addWidget(new QLabel("저장된 프리셋", this));
    rightLayout->addWidget(presetsTable_, 1);
    rightLayout->addWidget(deleteButton_);
    auto* rightWidget = new QWidget(this);
    rightWidget->setLayout(rightLayout);
    rightWidget->setMinimumWidth(320);

    auto* topRow = new QHBoxLayout();
    topRow->addWidget(leftWidget, 1);
    topRow->addWidget(rightWidget);

    auto* buttons = new QDialogButtonBox(this);
    usePresetButton_ = buttons->addButton("이 프리셋 사용", QDialogButtonBox::AcceptRole);
    usePresetButton_->setEnabled(false);
    buttons->addButton("닫기", QDialogButtonBox::RejectRole);
    connect(usePresetButton_, &QPushButton::clicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(topRow, 1);
    mainLayout->addWidget(buttons);

    RebuildViewer();
    RefreshPresetsTable();
}

void ShapePresetDialog::onModelComboChanged() {
    ResetCaptureState();
    RebuildViewer();
}

void ShapePresetDialog::RebuildViewer() {
    if (viewerPanel_) {
        viewerPanel_->disconnect(this);
        viewerHost_->layout()->removeWidget(viewerPanel_);
        viewerPanel_->deleteLater();
        viewerPanel_ = nullptr;
    }
    std::vector<std::pair<std::string, geometry::ModelHandle>> targetModel;
    const int index = modelCombo_->currentIndex();
    if (index >= 0 && index < static_cast<int>(models_.size())) {
        targetModel.push_back(models_[static_cast<size_t>(index)]);
    }
    viewerPanel_ = new viewer::MultiViewportPanel(adapter_, targetModel, this);
    viewerPanel_->setMinimumSize(kViewerMinWidth, kViewerMinHeight);
    viewerPanel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(viewerPanel_, &viewer::MultiViewportPanel::facePicked, this, &ShapePresetDialog::onFacePicked);
    connect(viewerPanel_, &viewer::MultiViewportPanel::facesInRegionPicked, this,
            &ShapePresetDialog::onFacesInRegionPicked);
    viewerHost_->layout()->addWidget(viewerPanel_);
    ArmIdleSelection();
}

// § Ctrl+드래그 바로 선택(2026-11-04) - "마우스 드래그로도 선택할 수 있게(Ctrl+왼쪽), Shift+왼쪽은 해제"
// 요청. 예전엔 "면 직접 선택..."을 먼저 눌러야만 드래그 선택이 됐다. 이제 다른 작업 중이 아니면 사각형
// 선택만 켜 두어서, 언제든 Ctrl+드래그하면 바로 면 직접 선택으로 들어간다. 피킹 모드는 끈다 - 피킹 모드는
// 마우스가 움직일 때마다 무거운 호버 계산(PickFace)을 해서, 대기 상태에 켜 두면 큰 도면에서 렉/응답 없음이
// 났다(같은 날 리포트). 그냥 클릭/드래그는 예전처럼 화면 조작이다.
void ShapePresetDialog::ArmIdleSelection() {
    if (!viewerPanel_) {
        return;
    }
    viewerPanel_->setPickModeActive(false);
    viewerPanel_->setRegionSelectModeActive(true);
}

namespace {
QString ManualSelectionGuide(size_t count) {
    return QString("면 선택 중(%1개) - 클릭 또는 Ctrl+드래그 = 추가, Shift+클릭 또는 Shift+드래그 = 제거. "
                   "그냥 드래그는 화면 회전입니다. 다 고르면 \"선택 완료\"를 누르세요.")
        .arg(count);
}
} // namespace

void ShapePresetDialog::ResetCaptureState() {
    pickArmed_ = false;
    manualSelectMode_ = false;
    manualSelectedFaces_.clear();
    finishManualSelectButton_->setVisible(false);
    hasCapturedPatch_ = false;
    capturedDescriptor_ = geometry::FeaturePatchDescriptor{};
    capturedHandle_ = geometry::kInvalidModelHandle;
    captureStatusLabel_->setText(
        "아직 캡처된 형상이 없습니다. \"형상 클릭해서 등록\" 또는 \"면 직접 선택\"을 누르거나, 바로 Ctrl+드래그로 "
        "면을 고르세요.");
    findSameModelButton_->setEnabled(false);
    sameModelCountLabel_->clear();
    saveButton_->setEnabled(false);
    if (viewerPanel_) {
        viewerPanel_->SetCapturedPatchHighlight({});
    }
    ArmIdleSelection();
}

void ShapePresetDialog::onCaptureClicked() {
    if (!viewerPanel_) {
        return;
    }
    manualSelectMode_ = false;
    manualSelectedFaces_.clear();
    finishManualSelectButton_->setVisible(false);
    pickArmed_ = true;
    captureStatusLabel_->setText(
        "피킹 대기 중 - 뷰어에서 등록하려는 형상을 클릭하세요(인접 면까지 자동으로 묶음). "
        "면을 직접 고르려면 Ctrl+드래그하세요.");
    viewerPanel_->setRegionSelectModeActive(true);
    viewerPanel_->setPickModeActive(true);
}

void ShapePresetDialog::onManualSelectClicked() {
    if (!viewerPanel_) {
        return;
    }
    pickArmed_ = false;
    manualSelectMode_ = true;
    manualSelectedFaces_.clear();
    viewerPanel_->SetCapturedPatchHighlight({});
    finishManualSelectButton_->setVisible(true);
    captureStatusLabel_->setText(ManualSelectionGuide(0));
    // § 드래그 영역 다중 선택 - 수동 선택 모드에서만 켠다. 일반 "지정..." 흐름
    // (onCaptureClicked, RuleEditorDialog 등)은 꺼둬서 "피킹 모드 중 드래그=회전"이라는
    // 기존 동작을 그대로 유지한다.
    viewerPanel_->setRegionSelectModeActive(true);
    viewerPanel_->setPickModeActive(true);
}

void ShapePresetDialog::onFinishManualSelectClicked() {
    if (!manualSelectMode_) {
        return;
    }
    manualSelectMode_ = false;
    finishManualSelectButton_->setVisible(false);
    ArmIdleSelection();
    if (manualSelectedFaces_.empty()) {
        QMessageBox::information(this, "선택 없음", "면을 하나도 선택하지 않았습니다.");
        ResetCaptureState();
        return;
    }
    const geometry::ModelHandle handle = SelectedModelHandle();
    const geometry::FeaturePatchCapture capture = adapter_->CaptureManualPatch(handle, manualSelectedFaces_);
    if (!capture.valid) {
        QMessageBox::warning(this, "캡처 실패", "선택한 면들로 형상 패치를 만들지 못했습니다.");
        return;
    }
    ApplyCapture(handle, capture);
}

void ShapePresetDialog::onFacePicked(
    geometry::ModelHandle handle, QVector3D rayOrigin, QVector3D rayDir, bool shiftHeld) {
    const geometry::Vec3 origin{rayOrigin.x(), rayOrigin.y(), rayOrigin.z()};
    const geometry::Vec3 dir{rayDir.x(), rayDir.y(), rayDir.z()};

    if (manualSelectMode_) {
        const int faceIndex = adapter_->PickFaceIndex(handle, origin, dir);
        if (faceIndex < 0) {
            return; // 형상을 못 찾은 클릭은 조용히 무시(모드가 계속 유지돼야 함).
        }
        ToggleManualFace(faceIndex, shiftHeld);
        UpdateManualSelectionHighlight();
        captureStatusLabel_->setText(ManualSelectionGuide(manualSelectedFaces_.size()));
        return;
    }

    if (!pickArmed_) {
        return; // 대기 상태(ArmIdleSelection)의 클릭은 무시 - Ctrl+드래그만 선택을 시작한다.
    }
    pickArmed_ = false;
    ArmIdleSelection(); // 자동 캡처는 클릭 한 번으로 끝 - 피킹(호버 계산)을 끄고 대기 상태로.

    const geometry::PickResult pick = adapter_->PickFace(handle, origin, dir);
    if (pick.kind == geometry::PickedFaceKind::None) {
        QMessageBox::information(this, "다시 클릭해주세요", "형상을 찾지 못했습니다 - 면 위를 클릭했는지 확인하세요.");
        return;
    }

    const geometry::FeaturePatchCapture capture = adapter_->CaptureFeaturePatch(handle, pick.point);
    if (!capture.valid) {
        QMessageBox::warning(
            this, "캡처 실패", "클릭한 위치에서 형상 패치를 만들지 못했습니다 - 다른 지점을 클릭해보세요.");
        return;
    }
    ApplyCapture(handle, capture);
}

void ShapePresetDialog::onFacesInRegionPicked(
    geometry::ModelHandle handle, std::vector<int> faceIndices, bool shiftHeld) {
    (void)handle; // 대상 인치는 뷰어가 하나뿐이라 이미 SelectedModelHandle()과 같음.
    if (!manualSelectMode_) {
        // § Ctrl+드래그 바로 선택 - 대기 중이거나 "형상 클릭해서 등록" 대기 중에 Ctrl+드래그하면 면 직접
        // 선택으로 넘어간다. 아직 고른 게 없는데 Shift+드래그(해제)면 할 일이 없다.
        if (shiftHeld || faceIndices.empty()) {
            return;
        }
        onManualSelectClicked();
    }
    for (int faceIndex : faceIndices) {
        ToggleManualFace(faceIndex, shiftHeld);
    }
    UpdateManualSelectionHighlight();
    captureStatusLabel_->setText(ManualSelectionGuide(manualSelectedFaces_.size()));
}

void ShapePresetDialog::ToggleManualFace(int faceIndex, bool shiftHeld) {
    const auto it = std::find(manualSelectedFaces_.begin(), manualSelectedFaces_.end(), faceIndex);
    if (shiftHeld) {
        if (it != manualSelectedFaces_.end()) {
            manualSelectedFaces_.erase(it);
        }
    } else if (it == manualSelectedFaces_.end()) {
        manualSelectedFaces_.push_back(faceIndex);
    }
}

void ShapePresetDialog::UpdateManualSelectionHighlight() {
    if (!viewerPanel_) {
        return;
    }
    const geometry::ModelHandle handle = SelectedModelHandle();
    std::vector<geometry::Vec3> triangles;
    for (int faceIndex : manualSelectedFaces_) {
        const auto faceTriangles = adapter_->GetFaceHighlightTriangles(handle, faceIndex);
        triangles.insert(triangles.end(), faceTriangles.begin(), faceTriangles.end());
    }
    viewerPanel_->SetCapturedPatchHighlight(triangles);
}

void ShapePresetDialog::ApplyCapture(geometry::ModelHandle handle, const geometry::FeaturePatchCapture& capture) {
    hasCapturedPatch_ = true;
    capturedDescriptor_ = capture.descriptor;
    capturedHandle_ = handle;
    // § "선택된 영역이 안 보인다" 피드백 - 캡처된 패치(면 여러 개일 수 있음)를 뷰어에
    // 초록색으로 고정 하이라이트해서, 면 하나만이 아니라 인접 면들까지 묶여서 캡처됐다는
    // 걸 눈으로 바로 확인할 수 있게 한다.
    if (viewerPanel_) {
        viewerPanel_->SetCapturedPatchHighlight(capture.highlightTriangles);
    }
    captureStatusLabel_->setText(QString("캡처됨 - 면 %1개, bbox비율(%2, %3, %4), 경계비율 %5, 대표반지름비율 %6")
                                      .arg(capture.descriptor.faceCount)
                                      .arg(capture.descriptor.bboxRatioX, 0, 'f', 2)
                                      .arg(capture.descriptor.bboxRatioY, 0, 'f', 2)
                                      .arg(capture.descriptor.bboxRatioZ, 0, 'f', 2)
                                      .arg(capture.descriptor.boundaryLengthRatio, 0, 'f', 2)
                                      .arg(capture.descriptor.dominantRadiusRatio, 0, 'f', 2));
    findSameModelButton_->setEnabled(true);
    sameModelCountLabel_->clear();
    saveButton_->setEnabled(true);
}

void ShapePresetDialog::onFindSameModelClicked() {
    if (!hasCapturedPatch_) {
        return;
    }
    const auto candidates =
        adapter_->FindPatchCandidates(capturedHandle_, capturedDescriptor_, geometry::kPatchSimilarityThreshold);
    sameModelCountLabel_->setText(QString("동일 모델에서 %1개 발견 (유사도 %2 이상)")
                                       .arg(candidates.size())
                                       .arg(geometry::kPatchSimilarityThreshold, 0, 'f', 2));
}

void ShapePresetDialog::onSaveClicked() {
    if (!hasCapturedPatch_) {
        return;
    }
    const QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::information(this, "이름 필요", "프리셋 이름을 입력하세요.");
        return;
    }

    std::optional<std::string> dimFilterKind;
    std::optional<double> dimFilterValue;
    const QString kindData = dimFilterKindCombo_->currentData().toString();
    if (!kindData.isEmpty()) {
        dimFilterKind = kindData.toStdString();
        dimFilterValue = dimFilterValueSpin_->value();
    }

    db_->SaveShapePreset(projectId_, name.toStdString(), capturedDescriptor_, /*imagePath=*/std::string(),
                          dimFilterKind, dimFilterValue);

    nameEdit_->clear();
    ResetCaptureState();
    RefreshPresetsTable();
}

void ShapePresetDialog::RefreshPresetsTable() {
    presets_ = db_->LoadShapePresetsForProject(projectId_);
    presetsTable_->setRowCount(static_cast<int>(presets_.size()));
    for (size_t row = 0; row < presets_.size(); ++row) {
        const auto& preset = presets_[row];
        const int r = static_cast<int>(row);
        presetsTable_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(preset.name)));
        presetsTable_->setItem(
            r, 1, new QTableWidgetItem(preset.dimFilterKind ? QString::fromStdString(*preset.dimFilterKind) : "-"));
        presetsTable_->setItem(
            r, 2,
            new QTableWidgetItem(preset.dimFilterValueMm ? QString::number(*preset.dimFilterValueMm, 'f', 2) : "-"));
    }
    onTableSelectionChanged();
}

void ShapePresetDialog::onTableSelectionChanged() {
    const auto selected = presetsTable_->selectionModel() ? presetsTable_->selectionModel()->selectedRows() : QModelIndexList();
    if (selected.isEmpty()) {
        selectedPreset_.reset();
        deleteButton_->setEnabled(false);
        usePresetButton_->setEnabled(false);
        return;
    }
    const int row = selected.front().row();
    if (row < 0 || row >= static_cast<int>(presets_.size())) {
        selectedPreset_.reset();
        deleteButton_->setEnabled(false);
        usePresetButton_->setEnabled(false);
        return;
    }
    selectedPreset_ = presets_[static_cast<size_t>(row)];
    deleteButton_->setEnabled(true);
    usePresetButton_->setEnabled(true);
}

void ShapePresetDialog::onDeleteClicked() {
    if (!selectedPreset_) {
        return;
    }
    db_->DeleteShapePreset(selectedPreset_->id);
    selectedPreset_.reset();
    RefreshPresetsTable();
}

geometry::FeaturePatchDescriptor ShapePresetDialog::SelectedDescriptor() {
    if (!selectedPreset_) {
        return geometry::FeaturePatchDescriptor{};
    }
    return db_->LoadShapePresetDescriptor(selectedPreset_->id);
}

geometry::ModelHandle ShapePresetDialog::SelectedModelHandle() const {
    const int index = modelCombo_->currentIndex();
    if (index < 0 || index >= static_cast<int>(models_.size())) {
        return geometry::kInvalidModelHandle;
    }
    return models_[static_cast<size_t>(index)].second;
}

} // namespace ui
