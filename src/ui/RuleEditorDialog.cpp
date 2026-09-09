#include "ui/RuleEditorDialog.h"

#include "ui/AnchorSearchDialog.h"
#include "viewer/MultiViewportPanel.h"

#include <QAbstractItemView>
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

namespace ui {

namespace {
constexpr int kImagePreviewWidth = 240;
constexpr int kImagePreviewHeight = 135;
} // namespace

RuleEditorDialog::RuleEditorDialog(
    database::Database* db, int projectId, geometry::IGeometryAdapter* adapter,
    viewer::MultiViewportPanel* viewerPanel,
    const std::vector<std::pair<std::string, geometry::ModelHandle>>& models, QWidget* parent)
    : QDialog(parent), db_(db), projectId_(projectId), adapter_(adapter), models_(models) {
    setWindowTitle("규칙 관리");
    resize(680, 920);

    table_ = new QTableWidget(this);
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({"이름", "측정 타입"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(table_, &QTableWidget::cellClicked, this, &RuleEditorDialog::onRowClicked);

    auto* deleteButton = new QPushButton("선택 항목 삭제", this);
    connect(deleteButton, &QPushButton::clicked, this, &RuleEditorDialog::onDeleteClicked);

    pickStatusLabel_ = new QLabel(this);
    pickStatusLabel_->setStyleSheet("color: #b05000; font-weight: bold;");
    pickStatusLabel_->hide();

    name_ = new QLineEdit(this);

    measurementType_ = new QComboBox(this);
    measurementType_->addItems({"point_to_point", "point_to_plane", "axis_projection", "face_to_face_gap",
                                 "instance_count", "min_pitch"});

    // 포인트 A: 형상타입 입력칸 + "검색..."(텍스트 기반, AnchorSearchDialog) +
    // "지정..."(3D 클릭 피킹) - 클릭하기 쉬우면 지정을, 겹쳐 있어 찾기 어려우면
    // 검색을 쓰라고 둘 다 남겨뒀다.
    anchorAType_ = new QLineEdit(this);
    anchorAPart_ = new QLineEdit(this);
    anchorADiameter_ = new QDoubleSpinBox(this);
    anchorADiameter_->setRange(0.0, 100.0);
    anchorADiameter_->setDecimals(2);
    anchorADiameter_->setSuffix(" mm (0 = 필터 안 씀)");
    searchAButton_ = new QPushButton("검색...", this);
    connect(searchAButton_, &QPushButton::clicked, this, &RuleEditorDialog::onSearchAnchorAClicked);
    pickAButton_ = new QPushButton("지정...", this);
    connect(pickAButton_, &QPushButton::clicked, this, &RuleEditorDialog::onPickAnchorAClicked);
    auto* anchorARow = new QWidget(this);
    auto* anchorARowLayout = new QHBoxLayout(anchorARow);
    anchorARowLayout->setContentsMargins(0, 0, 0, 0);
    anchorARowLayout->addWidget(anchorAType_, 1);
    anchorARowLayout->addWidget(searchAButton_);
    anchorARowLayout->addWidget(pickAButton_);

    // 포인트 B: 예전엔 원통 전용 "Anchor B"와 평면 전용 "기준평면"이 측정타입에 따라
    // 서로 다른 입력칸으로 갈렸는데, 지금은 하나로 합쳤다 - 원통을 찍으면 point 값으로,
    // 평면을 찍으면 기준평면으로 쓰인다(둘 중 뭐가 뭔지는 pickedAKind_/pickedBKind_로
    // 기억해뒀다가 BuildRule()이 판단). instance_count/min_pitch처럼 포인트가 하나뿐인
    // 측정타입일 땐 이 그룹 전체를 숨긴다(onMeasurementTypeChanged).
    anchorBGroup_ = new QWidget(this);
    anchorBType_ = new QLineEdit(anchorBGroup_);
    anchorBPart_ = new QLineEdit(anchorBGroup_);
    anchorBDiameter_ = new QDoubleSpinBox(anchorBGroup_);
    anchorBDiameter_->setRange(0.0, 100.0);
    anchorBDiameter_->setDecimals(2);
    anchorBDiameter_->setSuffix(" mm (0 = 필터 안 씀)");
    searchBButton_ = new QPushButton("검색...", anchorBGroup_);
    connect(searchBButton_, &QPushButton::clicked, this, &RuleEditorDialog::onSearchAnchorBClicked);
    pickBButton_ = new QPushButton("지정...", anchorBGroup_);
    connect(pickBButton_, &QPushButton::clicked, this, &RuleEditorDialog::onPickAnchorBClicked);
    auto* anchorBRow = new QWidget(anchorBGroup_);
    auto* anchorBRowLayout = new QHBoxLayout(anchorBRow);
    anchorBRowLayout->setContentsMargins(0, 0, 0, 0);
    anchorBRowLayout->addWidget(anchorBType_, 1);
    anchorBRowLayout->addWidget(searchBButton_);
    anchorBRowLayout->addWidget(pickBButton_);
    auto* anchorBLayout = new QFormLayout(anchorBGroup_);
    anchorBLayout->setContentsMargins(0, 0, 0, 0);
    anchorBLayout->addRow("포인트 B 형상타입", anchorBRow);
    anchorBLayout->addRow("포인트 B 부품명", anchorBPart_);
    anchorBLayout->addRow("포인트 B 지름", anchorBDiameter_);

    // § 이미지 캡쳐 연동 - 규칙별로 측정 포인트 그림을 남겨 CTQ 관리서처럼 쓸 수 있게.
    imagePreviewLabel_ = new QLabel(this);
    imagePreviewLabel_->setFixedSize(kImagePreviewWidth, kImagePreviewHeight);
    imagePreviewLabel_->setStyleSheet("border: 1px solid #bbb; background: #f5f5f5; color: #888;");
    imagePreviewLabel_->setAlignment(Qt::AlignCenter);
    imagePreviewLabel_->setText("저장된 이미지 없음");
    captureImageButton_ = new QPushButton("현재 화면 캡쳐", this);
    connect(captureImageButton_, &QPushButton::clicked, this, &RuleEditorDialog::onCaptureRuleImageClicked);
    auto* imageRow = new QWidget(this);
    auto* imageRowLayout = new QHBoxLayout(imageRow);
    imageRowLayout->setContentsMargins(0, 0, 0, 0);
    imageRowLayout->addWidget(imagePreviewLabel_);
    imageRowLayout->addWidget(captureImageButton_);
    imageRowLayout->addStretch(1);

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

    auto* form = new QFormLayout();
    form->addRow("규칙 이름", name_);
    form->addRow("측정 이미지", imageRow);
    form->addRow("측정 타입", measurementType_);
    form->addRow("포인트 A 형상타입", anchorARow);
    form->addRow("포인트 A 부품명", anchorAPart_);
    form->addRow("포인트 A 지름", anchorADiameter_);
    form->addRow(anchorBGroup_);
    form->addRow("Selector", selector_);
    form->addRow("Projection", projection_);
    form->addRow("공차 +", tolerancePlus_);
    form->addRow("공차 -", toleranceMinus_);

    addOrUpdateButton_ = new QPushButton("규칙 추가", this);
    connect(addOrUpdateButton_, &QPushButton::clicked, this, &RuleEditorDialog::onAddOrUpdateClicked);

    auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(closeButtons, &QDialogButtonBox::accepted, this, &QDialog::close);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(table_, 1);
    mainLayout->addWidget(deleteButton);
    mainLayout->addWidget(pickStatusLabel_);
    mainLayout->addLayout(form);
    mainLayout->addWidget(addOrUpdateButton_);
    mainLayout->addWidget(closeButtons);

    connect(measurementType_, &QComboBox::currentIndexChanged, this, &RuleEditorDialog::onMeasurementTypeChanged);
    onMeasurementTypeChanged(measurementType_->currentIndex());
    UpdateImagePreview();

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
    pickTarget_ = PickTarget::None;
    pickStatusLabel_->hide();
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

void RuleEditorDialog::resetForm() {
    editingRuleId_ = 0;
    addOrUpdateButton_->setText("규칙 추가");
    name_->clear();
    measurementType_->setCurrentIndex(0);
    anchorAType_->clear();
    anchorAPart_->clear();
    anchorADiameter_->setValue(0.0);
    anchorBType_->clear();
    anchorBPart_->clear();
    anchorBDiameter_->setValue(0.0);
    selector_->setCurrentIndex(0);
    projection_->setCurrentIndex(0);
    tolerancePlus_->setValue(0.0);
    toleranceMinus_->setValue(0.0);
    table_->clearSelection();
    pickedAKind_ = geometry::PickedFaceKind::None;
    pickedBKind_ = geometry::PickedFaceKind::None;
    ruleImagePath_.clear();
    UpdateImagePreview();
}

void RuleEditorDialog::onMeasurementTypeChanged(int index) {
    const QString type = measurementType_->itemText(index);
    // instance_count/min_pitch는 포인트 1개만 쓴다(짝을 짓지 않음) - 그 외(point_to_point/
    // point_to_plane/axis_projection/face_to_face_gap)는 전부 포인트 B가 필요하다.
    const bool isSingleAnchor = (type == "instance_count" || type == "min_pitch");
    anchorBGroup_->setVisible(!isSingleAnchor);
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
    // DB에는 피킹된 형상 종류가 저장되지 않으므로, 기존 규칙을 불러올 땐 항상 모른다고
    // 취급한다 - 재추론은 사용자가 다시 클릭으로 찍어야 일어난다.
    pickedAKind_ = geometry::PickedFaceKind::None;
    pickedBKind_ = geometry::PickedFaceKind::None;
    ruleImagePath_ = r.imagePath.value_or(std::string());
    UpdateImagePreview();

    name_->setText(QString::fromStdString(r.name));
    measurementType_->setCurrentText(QString::fromStdString(rule::ToString(r.measurementType)));

    if (!r.anchors.empty()) {
        anchorAType_->setText(QString::fromStdString(r.anchors[0].anchorType));
        anchorAPart_->setText(QString::fromStdString(r.anchors[0].partName));
        anchorADiameter_->setValue(r.anchors[0].paramValue.value_or(0.0));
    } else {
        anchorAType_->clear();
        anchorAPart_->clear();
        anchorADiameter_->setValue(0.0);
    }

    // 포인트 B 입력칸은 "두 번째 anchor" 또는 "referencePlane" 둘 중 있는 쪽을 보여준다
    // (point_to_plane은 referencePlane만 있고 anchors는 1개뿐이라 서로 배타적).
    if (r.measurementType == rule::MeasurementType::PointToPlane && r.referencePlane.has_value()) {
        anchorBType_->setText(QString::fromStdString(r.referencePlane->planeType));
        anchorBPart_->setText(QString::fromStdString(r.referencePlane->partName));
        anchorBDiameter_->setValue(0.0);
    } else if (r.anchors.size() > 1) {
        anchorBType_->setText(QString::fromStdString(r.anchors[1].anchorType));
        anchorBPart_->setText(QString::fromStdString(r.anchors[1].partName));
        anchorBDiameter_->setValue(r.anchors[1].paramValue.value_or(0.0));
    } else {
        anchorBType_->clear();
        anchorBPart_->clear();
        anchorBDiameter_->setValue(0.0);
    }

    selector_->setCurrentText(r.selector.empty() ? "(없음)" : QString::fromStdString(r.selector.front()));
    if (!r.projection.empty()) {
        projection_->setCurrentText(QString::fromStdString(r.projection));
    }
    tolerancePlus_->setValue(r.tolerancePlusMm);
    toleranceMinus_->setValue(r.toleranceMinusMm);

    onMeasurementTypeChanged(measurementType_->currentIndex());
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

    const bool isPointToPlane = r.measurementType == rule::MeasurementType::PointToPlane;
    const bool isSingleAnchor = r.measurementType == rule::MeasurementType::InstanceCount ||
                                 r.measurementType == rule::MeasurementType::MinPitch;

    auto anchorFromA = [&](const std::string& role) {
        rule::Anchor a;
        a.role = role;
        a.anchorType = anchorAType_->text().toStdString();
        a.partName = anchorAPart_->text().toStdString();
        if (anchorADiameter_->value() > 0.0) {
            a.paramKey = "diameter";
            a.paramValue = anchorADiameter_->value();
        }
        return a;
    };
    auto anchorFromB = [&](const std::string& role) {
        rule::Anchor b;
        b.role = role;
        b.anchorType = anchorBType_->text().toStdString();
        b.partName = anchorBPart_->text().toStdString();
        if (anchorBDiameter_->value() > 0.0) {
            b.paramKey = "diameter";
            b.paramValue = anchorBDiameter_->value();
        }
        return b;
    };
    auto planeFromA = [&] {
        rule::PlaneRef p;
        p.planeType = anchorAType_->text().toStdString();
        p.partName = anchorAPart_->text().toStdString();
        return p;
    };
    auto planeFromB = [&] {
        rule::PlaneRef p;
        p.planeType = anchorBType_->text().toStdString();
        p.partName = anchorBPart_->text().toStdString();
        return p;
    };

    if (isSingleAnchor) {
        r.anchors.push_back(anchorFromA("single"));
    } else if (isPointToPlane) {
        // 클릭으로 어느 쪽이 평면인지 알고 있으면 그걸 따르고, 모르면(텍스트로 직접
        // 입력) 기존 관례대로 A=점/B=평면으로 취급한다.
        if (pickedAKind_ == geometry::PickedFaceKind::Plane && pickedBKind_ != geometry::PickedFaceKind::Plane) {
            r.anchors.push_back(anchorFromB("single"));
            r.referencePlane = planeFromA();
        } else {
            r.anchors.push_back(anchorFromA("single"));
            r.referencePlane = planeFromB();
        }
    } else {
        r.anchors.push_back(anchorFromA("A"));
        r.anchors.push_back(anchorFromB("B"));
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

void RuleEditorDialog::onSearchAnchorAClicked() {
    AnchorSearchDialog dialog(adapter_, models_, this);
    dialog.SetInitialCriteria(
        anchorAType_->text().toStdString(), anchorAPart_->text().toStdString(), anchorADiameter_->value());
    if (dialog.exec() == QDialog::Accepted) {
        anchorAType_->setText(QString::fromStdString(dialog.AnchorType()));
        anchorAPart_->setText(QString::fromStdString(dialog.PartName()));
        anchorADiameter_->setValue(dialog.Diameter());
        // 텍스트로 다시 채웠으니 이전 피킹 결과(있었다면)는 더 이상 유효하지 않다.
        pickedAKind_ = geometry::PickedFaceKind::None;
    }
}

void RuleEditorDialog::onSearchAnchorBClicked() {
    AnchorSearchDialog dialog(adapter_, models_, this);
    dialog.SetInitialCriteria(
        anchorBType_->text().toStdString(), anchorBPart_->text().toStdString(), anchorBDiameter_->value());
    if (dialog.exec() == QDialog::Accepted) {
        anchorBType_->setText(QString::fromStdString(dialog.AnchorType()));
        anchorBPart_->setText(QString::fromStdString(dialog.PartName()));
        anchorBDiameter_->setValue(dialog.Diameter());
        pickedBKind_ = geometry::PickedFaceKind::None;
    }
}

void RuleEditorDialog::ArmPicking(PickTarget target, const QString& statusText) {
    if (!viewerPanel_) {
        QMessageBox::information(this, "지정 불가", "먼저 STEP 파일을 불러오세요.");
        return;
    }
    pickTarget_ = target;
    pickStatusLabel_->setText(statusText);
    pickStatusLabel_->show();
    viewerPanel_->setPickModeActive(true);
}

void RuleEditorDialog::onPickAnchorAClicked() {
    ArmPicking(PickTarget::AnchorA, "피킹 대기 중 - 3D 뷰에서 포인트 A로 쓸 형상(구멍/보스 또는 평면)을 클릭하세요.");
}

void RuleEditorDialog::onPickAnchorBClicked() {
    ArmPicking(PickTarget::AnchorB, "피킹 대기 중 - 3D 뷰에서 포인트 B로 쓸 형상(구멍/보스 또는 평면)을 클릭하세요.");
}

void RuleEditorDialog::onFacePicked(geometry::ModelHandle handle, QVector3D rayOrigin, QVector3D rayDir) {
    if (pickTarget_ == PickTarget::None) {
        return; // 이 다이얼로그가 피킹을 요청한 상태가 아니면 무시.
    }

    const geometry::Vec3 origin{rayOrigin.x(), rayOrigin.y(), rayOrigin.z()};
    const geometry::Vec3 dir{rayDir.x(), rayDir.y(), rayDir.z()};
    const geometry::PickResult result = adapter_->PickFace(handle, origin, dir);

    const PickTarget target = pickTarget_;
    pickTarget_ = PickTarget::None;
    pickStatusLabel_->hide();
    if (viewerPanel_) {
        viewerPanel_->setPickModeActive(false);
    }

    if (result.kind == geometry::PickedFaceKind::None) {
        QMessageBox::information(this, "다시 클릭해주세요", "형상을 찾지 못했습니다 - 면 위를 클릭했는지 확인하세요.");
        return;
    }

    // 원통이든 평면이든 둘 다 포인트 A/B 자리를 받을 수 있다 - 어느 쪽인지는
    // pickedAKind_/pickedBKind_에 기록해뒀다가 측정타입 추론과 BuildRule()이 쓴다.
    QLineEdit* typeField = (target == PickTarget::AnchorA) ? anchorAType_ : anchorBType_;
    QDoubleSpinBox* diameterField = (target == PickTarget::AnchorA) ? anchorADiameter_ : anchorBDiameter_;
    if (result.kind == geometry::PickedFaceKind::Cylinder) {
        typeField->setText("Hole");
        diameterField->setValue(result.diameterMm);
    } else {
        typeField->setText("Datum_Plane");
        diameterField->setValue(0.0);
    }

    if (target == PickTarget::AnchorA) {
        pickedAKind_ = result.kind;
    } else {
        pickedBKind_ = result.kind;
    }

    TryInferMeasurementType();
}

void RuleEditorDialog::TryInferMeasurementType() {
    if (pickedAKind_ == geometry::PickedFaceKind::None || pickedBKind_ == geometry::PickedFaceKind::None) {
        return; // 둘 다 찍혀야 조합을 판단할 수 있다.
    }
    const bool aCyl = pickedAKind_ == geometry::PickedFaceKind::Cylinder;
    const bool bCyl = pickedBKind_ == geometry::PickedFaceKind::Cylinder;
    if (aCyl && bCyl) {
        measurementType_->setCurrentText("point_to_point");
    } else if (aCyl != bCyl) {
        measurementType_->setCurrentText("point_to_plane");
    } else {
        measurementType_->setCurrentText("face_to_face_gap");
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
