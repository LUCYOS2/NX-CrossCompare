#include "ui/RuleEditorDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QWidget>

namespace ui {

RuleEditorDialog::RuleEditorDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("새 규칙 추가");

    name_ = new QLineEdit(this);

    measurementType_ = new QComboBox(this);
    measurementType_->addItems({"point_to_point", "point_to_plane", "axis_projection", "face_to_face_gap"});

    anchorAType_ = new QLineEdit(this);
    anchorAPart_ = new QLineEdit(this);

    anchorBGroup_ = new QWidget(this);
    anchorBType_ = new QLineEdit(anchorBGroup_);
    anchorBPart_ = new QLineEdit(anchorBGroup_);
    auto* anchorBLayout = new QFormLayout(anchorBGroup_);
    anchorBLayout->setContentsMargins(0, 0, 0, 0);
    anchorBLayout->addRow("Anchor B 형상타입", anchorBType_);
    anchorBLayout->addRow("Anchor B 부품명", anchorBPart_);

    planeRefGroup_ = new QWidget(this);
    planeType_ = new QLineEdit(planeRefGroup_);
    planePart_ = new QLineEdit(planeRefGroup_);
    auto* planeLayout = new QFormLayout(planeRefGroup_);
    planeLayout->setContentsMargins(0, 0, 0, 0);
    planeLayout->addRow("기준평면 타입", planeType_);
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
    form->addRow("Anchor A 형상타입", anchorAType_);
    form->addRow("Anchor A 부품명", anchorAPart_);
    form->addRow(anchorBGroup_);
    form->addRow(planeRefGroup_);
    form->addRow("Selector", selector_);
    form->addRow("Projection", projection_);
    form->addRow("공차 +", tolerancePlus_);
    form->addRow("공차 -", toleranceMinus_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addWidget(buttons);

    connect(measurementType_, &QComboBox::currentIndexChanged, this, &RuleEditorDialog::onMeasurementTypeChanged);
    onMeasurementTypeChanged(measurementType_->currentIndex());
}

void RuleEditorDialog::onMeasurementTypeChanged(int index) {
    const QString type = measurementType_->itemText(index);
    const bool isPointToPlane = (type == "point_to_plane");
    anchorBGroup_->setVisible(!isPointToPlane);
    planeRefGroup_->setVisible(isPointToPlane);
}

rule::Rule RuleEditorDialog::BuildRule() const {
    rule::Rule r;
    r.name = name_->text().toStdString();
    r.measurementType = rule::MeasurementTypeFromString(measurementType_->currentText().toStdString());
    r.referenceFrame = {"World_Origin"};

    const bool isPointToPlane = r.measurementType == rule::MeasurementType::PointToPlane;

    rule::Anchor anchorA;
    anchorA.role = isPointToPlane ? "single" : "A";
    anchorA.anchorType = anchorAType_->text().toStdString();
    anchorA.partName = anchorAPart_->text().toStdString();
    r.anchors.push_back(anchorA);

    if (isPointToPlane) {
        rule::PlaneRef planeRef;
        planeRef.planeType = planeType_->text().toStdString();
        planeRef.partName = planePart_->text().toStdString();
        r.referencePlane = planeRef;
    } else {
        rule::Anchor anchorB;
        anchorB.role = "B";
        anchorB.anchorType = anchorBType_->text().toStdString();
        anchorB.partName = anchorBPart_->text().toStdString();
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

} // namespace ui
