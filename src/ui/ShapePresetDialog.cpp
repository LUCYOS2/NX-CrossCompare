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
    captureStatusLabel_ = new QLabel("아직 캡처된 형상이 없습니다.", this);
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
    leftLayout->addWidget(captureButton_);
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
    viewerHost_->layout()->addWidget(viewerPanel_);
}

void ShapePresetDialog::ResetCaptureState() {
    pickArmed_ = false;
    hasCapturedPatch_ = false;
    capturedDescriptor_ = geometry::FeaturePatchDescriptor{};
    capturedHandle_ = geometry::kInvalidModelHandle;
    captureStatusLabel_->setText("아직 캡처된 형상이 없습니다.");
    findSameModelButton_->setEnabled(false);
    sameModelCountLabel_->clear();
    saveButton_->setEnabled(false);
    if (viewerPanel_) {
        viewerPanel_->setPickModeActive(false);
    }
}

void ShapePresetDialog::onCaptureClicked() {
    if (!viewerPanel_) {
        return;
    }
    pickArmed_ = true;
    captureStatusLabel_->setText("피킹 대기 중 - 뷰어에서 등록하려는 형상을 클릭하세요.");
    viewerPanel_->setPickModeActive(true);
}

void ShapePresetDialog::onFacePicked(geometry::ModelHandle handle, QVector3D rayOrigin, QVector3D rayDir) {
    if (!pickArmed_) {
        return;
    }
    pickArmed_ = false;
    if (viewerPanel_) {
        viewerPanel_->setPickModeActive(false);
    }

    const geometry::Vec3 origin{rayOrigin.x(), rayOrigin.y(), rayOrigin.z()};
    const geometry::Vec3 dir{rayDir.x(), rayDir.y(), rayDir.z()};
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

    hasCapturedPatch_ = true;
    capturedDescriptor_ = capture.descriptor;
    capturedHandle_ = handle;
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
