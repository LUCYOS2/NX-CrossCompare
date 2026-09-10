#include "ui/RuleEditorDialog.h"

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
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace ui {

namespace {
constexpr int kImagePreviewWidth = 420;
constexpr int kImagePreviewHeight = 236;
constexpr int kLeftColumnWidth = 260;
} // namespace

RuleEditorDialog::RuleEditorDialog(
    database::Database* db, int projectId, geometry::IGeometryAdapter* adapter,
    viewer::MultiViewportPanel* viewerPanel,
    const std::vector<std::pair<std::string, geometry::ModelHandle>>& models, QWidget* parent)
    : QDialog(parent), db_(db), projectId_(projectId), adapter_(adapter), models_(models) {
    setWindowTitle("규칙 관리");
    resize(1040, 900);

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

    // ---- 우측: 측정 이미지 ----
    imagePreviewLabel_ = new QLabel(this);
    imagePreviewLabel_->setFixedSize(kImagePreviewWidth, kImagePreviewHeight);
    imagePreviewLabel_->setStyleSheet("border: 1px solid #bbb; background: #f5f5f5; color: #888;");
    imagePreviewLabel_->setAlignment(Qt::AlignCenter);
    imagePreviewLabel_->setText("저장된 이미지 없음");

    captureImageButton_ = new QPushButton("현재 화면 캡쳐", this);
    connect(captureImageButton_, &QPushButton::clicked, this, &RuleEditorDialog::onCaptureRuleImageClicked);

    // § 이미지 캡쳐 뷰어 설정 - 메인 툴바를 따로 안 열어도 단면뷰만 빠르게 켜고 끌 수
    // 있게. 축/좌표 등 세부 조정은 여전히 메인 툴바에서(캡쳐는 같은 viewerPanel_을
    // 그대로 찍으므로 메인 쪽 설정이 그대로 반영된다).
    sectionViewCheck_ = new QCheckBox("단면 보기(캡쳐용)", this);
    connect(sectionViewCheck_, &QCheckBox::toggled, this, &RuleEditorDialog::onSectionViewToggled);

    auto* imageButtonsLayout = new QVBoxLayout();
    imageButtonsLayout->addWidget(captureImageButton_);
    imageButtonsLayout->addWidget(sectionViewCheck_);
    imageButtonsLayout->addStretch(1);

    auto* imageRow = new QHBoxLayout();
    imageRow->addWidget(imagePreviewLabel_);
    imageRow->addLayout(imageButtonsLayout);
    imageRow->addStretch(1);

    // ---- 우측: 규칙 이름 ----
    name_ = new QLineEdit(this);
    auto* nameForm = new QFormLayout();
    nameForm->addRow("규칙 이름", name_);

    // ---- 우측: 형상 타입(포인트 리스트) ----
    pointsTable_ = new QTableWidget(this);
    pointsTable_->setColumnCount(5);
    pointsTable_->setHorizontalHeaderLabels({"형상타입", "부품명", "지름(mm)", "축방향", "종류"});
    pointsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    pointsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    pointsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pointsTable_->setMaximumHeight(140);

    nextPointDirection_ = new QComboBox(this);
    nextPointDirection_->addItems({"(필터 안 씀)", "X", "Y", "Z"});

    pickPointButton_ = new QPushButton("지정...", this);
    connect(pickPointButton_, &QPushButton::clicked, this, &RuleEditorDialog::onPickPointClicked);
    searchPointButton_ = new QPushButton("검색으로 추가...", this);
    connect(searchPointButton_, &QPushButton::clicked, this, &RuleEditorDialog::onSearchPointClicked);
    deletePointButton_ = new QPushButton("선택 포인트 삭제", this);
    connect(deletePointButton_, &QPushButton::clicked, this, &RuleEditorDialog::onDeletePointClicked);

    pickStatusLabel_ = new QLabel(this);
    pickStatusLabel_->setStyleSheet("color: #b05000; font-weight: bold;");
    pickStatusLabel_->hide();

    auto* pointsButtonsRow = new QHBoxLayout();
    pointsButtonsRow->addWidget(new QLabel("추가할 포인트 축 방향:", this));
    pointsButtonsRow->addWidget(nextPointDirection_);
    pointsButtonsRow->addStretch(1);
    pointsButtonsRow->addWidget(pickPointButton_);
    pointsButtonsRow->addWidget(searchPointButton_);
    pointsButtonsRow->addWidget(deletePointButton_);

    auto* pointsLayout = new QVBoxLayout();
    pointsLayout->addWidget(new QLabel("형상 타입", this));
    pointsLayout->addWidget(pointsTable_);
    pointsLayout->addLayout(pointsButtonsRow);
    pointsLayout->addWidget(pickStatusLabel_);

    // ---- 우측: 측정 타입/Selector/Projection/Tolerance ----
    measurementType_ = new QComboBox(this);
    measurementType_->addItems({"point_to_point", "point_to_plane", "axis_projection", "face_to_face_gap",
                                 "instance_count", "min_pitch"});

    selector_ = new QComboBox(this);
    selector_->addItems(
        {"(없음)", "nearest_pair", "leftmost", "rightmost", "nearest_face_pair", "parallel_face_pair"});

    projection_ = new QComboBox(this);
    projection_->addItems({"3D", "normal", "X", "Y", "Z"});

    tolerancePlus_ = new QDoubleSpinBox(this);
    tolerancePlus_->setRange(0.0, 100.0);
    tolerancePlus_->setDecimals(3);
    tolerancePlus_->setSuffix(" mm");

    toleranceMinus_ = new QDoubleSpinBox(this);
    toleranceMinus_->setRange(0.0, 100.0);
    toleranceMinus_->setDecimals(3);
    toleranceMinus_->setSuffix(" mm");

    auto* bottomForm = new QFormLayout();
    bottomForm->addRow("측정 타입", measurementType_);
    bottomForm->addRow("Selector", selector_);
    bottomForm->addRow("Projection", projection_);
    bottomForm->addRow("공차 +", tolerancePlus_);
    bottomForm->addRow("공차 -", toleranceMinus_);

    addOrUpdateButton_ = new QPushButton("규칙 추가", this);
    connect(addOrUpdateButton_, &QPushButton::clicked, this, &RuleEditorDialog::onAddOrUpdateClicked);

    auto* rightLayout = new QVBoxLayout();
    rightLayout->addWidget(new QLabel("측정 이미지", this));
    rightLayout->addLayout(imageRow);
    rightLayout->addLayout(nameForm);
    rightLayout->addLayout(pointsLayout);
    rightLayout->addLayout(bottomForm);
    rightLayout->addWidget(addOrUpdateButton_);
    rightLayout->addStretch(1);
    auto* rightWidget = new QWidget(this);
    rightWidget->setLayout(rightLayout);

    auto* topRow = new QHBoxLayout();
    topRow->addWidget(leftWidget);
    topRow->addWidget(rightWidget, 1);

    auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(closeButtons, &QDialogButtonBox::accepted, this, &QDialog::close);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(topRow, 1);
    mainLayout->addWidget(closeButtons);

    UpdateImagePreview();
    refreshPointsTable();
    RewireViewerPanel(viewerPanel);
    refreshTable();
}

void RuleEditorDialog::RewireViewerPanel(viewer::MultiViewportPanel* panel) {
    if (viewerPanel_) {
        disconnect(viewerPanel_, &viewer::MultiViewportPanel::facePicked, this, &RuleEditorDialog::onFacePicked);
    }
    viewerPanel_ = panel;
    if (viewerPanel_) {
        connect(viewerPanel_, &viewer::MultiViewportPanel::facePicked, this, &RuleEditorDialog::onFacePicked);
    }
    // 패널이 바뀌면 이전 패널 기준으로 켜뒀던 피킹 대기 상태는 의미가 없어진다.
    pickArmed_ = false;
    pickStatusLabel_->hide();
    if (viewerPanel_) {
        const bool blocked = sectionViewCheck_->blockSignals(true);
        sectionViewCheck_->setChecked(viewerPanel_->sectionEnabled());
        sectionViewCheck_->blockSignals(blocked);
    }
}

void RuleEditorDialog::closeEvent(QCloseEvent* event) {
    if (viewerPanel_) {
        viewerPanel_->setPickModeActive(false);
    }
    QDialog::closeEvent(event);
}

void RuleEditorDialog::refreshTable() {
    rules_ = db_->LoadRulesForProject(projectId_);
    table_->setRowCount(static_cast<int>(rules_.size()));
    for (size_t row = 0; row < rules_.size(); ++row) {
        const auto& r = rules_[row];
        table_->setItem(static_cast<int>(row), 0, new QTableWidgetItem(QString::fromStdString(r.name)));
        table_->setItem(static_cast<int>(row), 1,
                        new QTableWidgetItem(QString::fromStdString(rule::ToString(r.measurementType))));
    }
}

void RuleEditorDialog::refreshPointsTable() {
    pointsTable_->setRowCount(static_cast<int>(points_.size()));
    for (size_t i = 0; i < points_.size(); ++i) {
        const auto& p = points_[i];
        const int row = static_cast<int>(i);
        pointsTable_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(p.type)));
        pointsTable_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(p.partName)));
        pointsTable_->setItem(
            row, 2, new QTableWidgetItem(p.diameterMm > 0.0 ? QString::number(p.diameterMm, 'f', 2) : "-"));
        pointsTable_->setItem(
            row, 3,
            new QTableWidgetItem(p.directionAxis.empty() ? "-" : QString::fromStdString(p.directionAxis)));
        QString kindLabel = "-";
        if (p.kind == geometry::PickedFaceKind::Cylinder) {
            kindLabel = "원통";
        } else if (p.kind == geometry::PickedFaceKind::Plane) {
            kindLabel = "평면";
        }
        pointsTable_->setItem(row, 4, new QTableWidgetItem(kindLabel));
    }
}

void RuleEditorDialog::resetForm() {
    editingRuleId_ = 0;
    addOrUpdateButton_->setText("규칙 추가");
    name_->clear();
    measurementType_->setCurrentIndex(0);
    points_.clear();
    refreshPointsTable();
    nextPointDirection_->setCurrentIndex(0);
    selector_->setCurrentIndex(0);
    projection_->setCurrentIndex(0);
    tolerancePlus_->setValue(0.0);
    toleranceMinus_->setValue(0.0);
    table_->clearSelection();
    ruleImagePath_.clear();
    UpdateImagePreview();
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
    ruleImagePath_ = r.imagePath.value_or(std::string());
    UpdateImagePreview();

    name_->setText(QString::fromStdString(r.name));
    measurementType_->setCurrentText(QString::fromStdString(rule::ToString(r.measurementType)));

    points_.clear();
    for (const auto& a : r.anchors) {
        PointEntry p;
        p.type = a.anchorType;
        p.partName = a.partName;
        p.diameterMm = a.paramValue.value_or(0.0);
        p.directionAxis = a.directionAxis.value_or(std::string());
        p.kind = geometry::PickedFaceKind::Cylinder;
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

    selector_->setCurrentText(r.selector.empty() ? "(없음)" : QString::fromStdString(r.selector.front()));
    if (!r.projection.empty()) {
        projection_->setCurrentText(QString::fromStdString(r.projection));
    }
    tolerancePlus_->setValue(r.tolerancePlusMm);
    toleranceMinus_->setValue(r.toleranceMinusMm);
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

void RuleEditorDialog::onAddOrUpdateClicked() {
    if (name_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "입력 필요", "규칙 이름은 비워둘 수 없습니다.");
        return;
    }

    const auto type = rule::MeasurementTypeFromString(measurementType_->currentText().toStdString());
    const bool isSingleAnchor =
        type == rule::MeasurementType::InstanceCount || type == rule::MeasurementType::MinPitch;
    if (isSingleAnchor && points_.size() != 1) {
        QMessageBox::warning(this, "포인트 필요", "이 측정 타입은 포인트가 정확히 1개 필요합니다.");
        return;
    }
    if (!isSingleAnchor && points_.size() != 2) {
        QMessageBox::warning(this, "포인트 필요", "이 측정 타입은 포인트가 정확히 2개 필요합니다.");
        return;
    }
    if (type == rule::MeasurementType::PointToPlane) {
        const auto planeCount = std::count_if(points_.begin(), points_.end(), [](const PointEntry& p) {
            return p.kind == geometry::PickedFaceKind::Plane;
        });
        if (planeCount != 1) {
            QMessageBox::warning(
                this, "포인트 확인 필요",
                "point_to_plane은 포인트 1개(점) + 평면 1개가 필요합니다 - 평면을 클릭/검색으로 추가했는지 확인하세요.");
            return;
        }
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

void RuleEditorDialog::onDeleteClicked() {
    const int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(rules_.size())) {
        return;
    }
    const int deletedId = rules_[static_cast<size_t>(row)].id;
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
            this, "추가 불가", "포인트는 최대 2개까지 추가할 수 있습니다 - 기존 포인트를 삭제한 뒤 다시 시도하세요.");
        return;
    }
    points_.push_back(entry);
    refreshPointsTable();
    TryInferMeasurementType();
}

void RuleEditorDialog::onSearchPointClicked() {
    if (points_.size() >= 2) {
        QMessageBox::information(
            this, "추가 불가", "포인트는 최대 2개까지 추가할 수 있습니다 - 기존 포인트를 삭제한 뒤 다시 시도하세요.");
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
        AddPoint(entry);
    }
}

void RuleEditorDialog::onPickPointClicked() {
    if (points_.size() >= 2) {
        QMessageBox::information(
            this, "추가 불가", "포인트는 최대 2개까지 추가할 수 있습니다 - 기존 포인트를 삭제한 뒤 다시 시도하세요.");
        return;
    }
    if (!viewerPanel_) {
        QMessageBox::information(this, "지정 불가", "먼저 STEP 파일을 불러오세요.");
        return;
    }
    pickArmed_ = true;
    pickStatusLabel_->setText("피킹 대기 중 - 3D 뷰에서 추가할 형상(구멍/보스 또는 평면)을 클릭하세요.");
    pickStatusLabel_->show();
    viewerPanel_->setPickModeActive(true);
}

void RuleEditorDialog::onDeletePointClicked() {
    const int row = pointsTable_->currentRow();
    if (row < 0 || row >= static_cast<int>(points_.size())) {
        return;
    }
    points_.erase(points_.begin() + row);
    refreshPointsTable();
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
    if (viewerPanel_) {
        viewerPanel_->setPickModeActive(false);
    }

    if (result.kind == geometry::PickedFaceKind::None) {
        QMessageBox::information(this, "다시 클릭해주세요", "형상을 찾지 못했습니다 - 면 위를 클릭했는지 확인하세요.");
        return;
    }

    PointEntry entry;
    entry.kind = result.kind;
    entry.directionAxis = CurrentDirectionFilter();
    if (result.kind == geometry::PickedFaceKind::Cylinder) {
        entry.type = "Hole";
        entry.diameterMm = result.diameterMm;
    } else {
        entry.type = "Datum_Plane";
    }
    AddPoint(entry);
}

void RuleEditorDialog::TryInferMeasurementType() {
    if (points_.size() != 2) {
        return; // 둘 다 채워져야 조합을 판단할 수 있다.
    }
    const bool aCyl = points_[0].kind == geometry::PickedFaceKind::Cylinder;
    const bool bCyl = points_[1].kind == geometry::PickedFaceKind::Cylinder;
    if (aCyl && bCyl) {
        measurementType_->setCurrentText("point_to_point");
    } else if (aCyl != bCyl) {
        measurementType_->setCurrentText("point_to_plane");
    } else {
        measurementType_->setCurrentText("face_to_face_gap");
    }
}

void RuleEditorDialog::onSectionViewToggled(bool checked) {
    if (viewerPanel_) {
        viewerPanel_->setSectionEnabled(checked);
    }
}

void RuleEditorDialog::onCaptureRuleImageClicked() {
    if (!viewerPanel_) {
        QMessageBox::information(this, "캡쳐 불가", "먼저 STEP 파일을 불러오세요.");
        return;
    }
    const QPixmap pixmap = viewerPanel_->grab();
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
    UpdateImagePreview();
}

void RuleEditorDialog::UpdateImagePreview() {
    if (ruleImagePath_.empty()) {
        imagePreviewLabel_->setPixmap(QPixmap());
        imagePreviewLabel_->setText("저장된 이미지 없음");
        return;
    }
    const QPixmap pixmap(QString::fromStdString(ruleImagePath_));
    if (pixmap.isNull()) {
        imagePreviewLabel_->setPixmap(QPixmap());
        imagePreviewLabel_->setText("이미지를 불러올 수 없음");
        return;
    }
    imagePreviewLabel_->setPixmap(pixmap.scaled(
        kImagePreviewWidth, kImagePreviewHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace ui
