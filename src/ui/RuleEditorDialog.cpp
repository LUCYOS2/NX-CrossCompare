#include "ui/RuleEditorDialog.h"

#include "ui/AnchorSearchDialog.h"
#include "viewer/MultiViewportPanel.h"

#include <QAbstractItemView>
#include <QCloseEvent>
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
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace ui {

RuleEditorDialog::RuleEditorDialog(
    database::Database* db, int projectId, geometry::IGeometryAdapter* adapter,
    viewer::MultiViewportPanel* viewerPanel,
    const std::vector<std::pair<std::string, geometry::ModelHandle>>& models, QWidget* parent)
    : QDialog(parent), db_(db), projectId_(projectId), adapter_(adapter), models_(models) {
    setWindowTitle("규칙 관리");
    resize(680, 860);

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

    // Anchor A: 형상타입 입력칸 + "검색..."(텍스트 기반, AnchorSearchDialog) +
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
    anchorBLayout->addRow("Anchor B 형상타입", anchorBRow);
    anchorBLayout->addRow("Anchor B 부품명", anchorBPart_);
    anchorBLayout->addRow("Anchor B 지름", anchorBDiameter_);

    // 기준평면은 검색(AnchorSearchDialog)이 아직 원통면만 다뤄서 "지정..."만 둔다.
    planeRefGroup_ = new QWidget(this);
    planeType_ = new QLineEdit(planeRefGroup_);
    planePart_ = new QLineEdit(planeRefGroup_);
    pickPlaneButton_ = new QPushButton("지정...", planeRefGroup_);
    connect(pickPlaneButton_, &QPushButton::clicked, this, &RuleEditorDialog::onPickPlaneClicked);
    auto* planeTypeRow = new QWidget(planeRefGroup_);
    auto* planeTypeRowLayout = new QHBoxLayout(planeTypeRow);
    planeTypeRowLayout->setContentsMargins(0, 0, 0, 0);
    planeTypeRowLayout->addWidget(planeType_, 1);
    planeTypeRowLayout->addWidget(pickPlaneButton_);
    auto* planeLayout = new QFormLayout(planeRefGroup_);
    planeLayout->setContentsMargins(0, 0, 0, 0);
    planeLayout->addRow("기준평면 타입", planeTypeRow);
    planeLayout->addRow("기준평면 부품명", planePart_);

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
    form->addRow("측정 타입", measurementType_);
    form->addRow("Anchor A 형상타입", anchorARow);
    form->addRow("Anchor A 부품명", anchorAPart_);
    form->addRow("Anchor A 지름", anchorADiameter_);
    form->addRow(anchorBGroup_);
    form->addRow(planeRefGroup_);
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
    planeType_->clear();
    planePart_->clear();
    selector_->setCurrentIndex(0);
    projection_->setCurrentIndex(0);
    tolerancePlus_->setValue(0.0);
    toleranceMinus_->setValue(0.0);
    table_->clearSelection();
}

void RuleEditorDialog::onMeasurementTypeChanged(int index) {
    const QString type = measurementType_->itemText(index);
    const bool isPointToPlane = (type == "point_to_plane");
    // instance_count/min_pitch는 anchor 1개만 쓴다(짝을 짓지 않음) - point_to_plane과
    // 마찬가지로 Anchor B는 필요 없지만, 기준평면도 필요 없다는 점이 다르다.
    const bool isSingleAnchorNoPlane = (type == "instance_count" || type == "min_pitch");
    anchorBGroup_->setVisible(!isPointToPlane && !isSingleAnchorNoPlane);
    planeRefGroup_->setVisible(isPointToPlane);
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

    if (r.anchors.size() > 1) {
        anchorBType_->setText(QString::fromStdString(r.anchors[1].anchorType));
        anchorBPart_->setText(QString::fromStdString(r.anchors[1].partName));
        anchorBDiameter_->setValue(r.anchors[1].paramValue.value_or(0.0));
    } else {
        anchorBType_->clear();
        anchorBPart_->clear();
        anchorBDiameter_->setValue(0.0);
    }

    if (r.referencePlane.has_value()) {
        planeType_->setText(QString::fromStdString(r.referencePlane->planeType));
        planePart_->setText(QString::fromStdString(r.referencePlane->partName));
    } else {
        planeType_->clear();
        planePart_->clear();
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

    const bool isPointToPlane = r.measurementType == rule::MeasurementType::PointToPlane;
    const bool isSingleAnchorNoPlane = r.measurementType == rule::MeasurementType::InstanceCount ||
                                        r.measurementType == rule::MeasurementType::MinPitch;

    rule::Anchor anchorA;
    anchorA.role = (isPointToPlane || isSingleAnchorNoPlane) ? "single" : "A";
    anchorA.anchorType = anchorAType_->text().toStdString();
    anchorA.partName = anchorAPart_->text().toStdString();
    if (anchorADiameter_->value() > 0.0) {
        anchorA.paramKey = "diameter";
        anchorA.paramValue = anchorADiameter_->value();
    }
    r.anchors.push_back(anchorA);

    if (isPointToPlane) {
        rule::PlaneRef planeRef;
        planeRef.planeType = planeType_->text().toStdString();
        planeRef.partName = planePart_->text().toStdString();
        r.referencePlane = planeRef;
    } else if (!isSingleAnchorNoPlane) {
        rule::Anchor anchorB;
        anchorB.role = "B";
        anchorB.anchorType = anchorBType_->text().toStdString();
        anchorB.partName = anchorBPart_->text().toStdString();
        if (anchorBDiameter_->value() > 0.0) {
            anchorB.paramKey = "diameter";
            anchorB.paramValue = anchorBDiameter_->value();
        }
        r.anchors.push_back(anchorB);
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
    ArmPicking(PickTarget::AnchorA, "피킹 대기 중 - 3D 뷰에서 Anchor A로 쓸 구멍/보스(원통면)를 클릭하세요.");
}

void RuleEditorDialog::onPickAnchorBClicked() {
    ArmPicking(PickTarget::AnchorB, "피킹 대기 중 - 3D 뷰에서 Anchor B로 쓸 구멍/보스(원통면)를 클릭하세요.");
}

void RuleEditorDialog::onPickPlaneClicked() {
    ArmPicking(PickTarget::ReferencePlane, "피킹 대기 중 - 3D 뷰에서 기준평면으로 쓸 평면을 클릭하세요.");
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

    if (target == PickTarget::AnchorA || target == PickTarget::AnchorB) {
        if (result.kind != geometry::PickedFaceKind::Cylinder) {
            QMessageBox::information(
                this, "다시 클릭해주세요", "구멍/보스(원통면)을 클릭해야 합니다 - 평면을 클릭했습니다.");
            return;
        }
        QLineEdit* typeField = (target == PickTarget::AnchorA) ? anchorAType_ : anchorBType_;
        QDoubleSpinBox* diameterField = (target == PickTarget::AnchorA) ? anchorADiameter_ : anchorBDiameter_;
        typeField->setText("Hole");
        diameterField->setValue(result.diameterMm);
    } else if (target == PickTarget::ReferencePlane) {
        if (result.kind != geometry::PickedFaceKind::Plane) {
            QMessageBox::information(
                this, "다시 클릭해주세요", "기준평면(평면)을 클릭해야 합니다 - 원통면을 클릭했습니다.");
            return;
        }
        planeType_->setText("Datum_Plane");
    }
}

} // namespace ui
