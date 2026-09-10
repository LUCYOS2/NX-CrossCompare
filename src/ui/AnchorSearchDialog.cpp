#include "ui/AnchorSearchDialog.h"

#include "rule/Selector.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <optional>
#include <utility>

namespace ui {

AnchorSearchDialog::AnchorSearchDialog(
    geometry::IGeometryAdapter* adapter,
    const std::vector<std::pair<std::string, geometry::ModelHandle>>& models, QWidget* parent)
    : QDialog(parent), adapter_(adapter), models_(models) {
    setWindowTitle("포인트 검색 (앵커 후보 탐색기)");
    resize(640, 520);

    modelCombo_ = new QComboBox(this);
    for (const auto& [label, handle] : models_) {
        modelCombo_->addItem(QString::fromStdString(label));
    }

    anchorType_ = new QLineEdit(this);
    anchorType_->setPlaceholderText("Hole, Boss_Center ...");
    partName_ = new QLineEdit(this);
    diameter_ = new QDoubleSpinBox(this);
    diameter_->setRange(0.0, 1000.0); // 실측 Ø254mm(구조용 원통) 이상도 검색 가능해야 함
    diameter_->setDecimals(2);
    diameter_->setSuffix(" mm (0 = 필터 안 씀)");

    auto* form = new QFormLayout();
    form->addRow("대상 인치", modelCombo_);
    form->addRow("형상타입 (anchor_type)", anchorType_);
    form->addRow("부품명 (선택)", partName_);
    form->addRow("지름 필터", diameter_);

    auto* searchButton = new QPushButton("검색", this);
    connect(searchButton, &QPushButton::clicked, this, &AnchorSearchDialog::onSearchClicked);

    countLabel_ = new QLabel("검색 결과 없음", this);

    resultsTable_ = new QTableWidget(this);
    resultsTable_->setColumnCount(4);
    resultsTable_->setHorizontalHeaderLabels({"X", "Y", "Z", "지름(mm)"});
    resultsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* buttons = new QDialogButtonBox(this);
    auto* applyButton = buttons->addButton("이 조건 적용", QDialogButtonBox::AcceptRole);
    buttons->addButton("닫기", QDialogButtonBox::RejectRole);
    connect(applyButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addWidget(searchButton);
    mainLayout->addWidget(countLabel_);
    mainLayout->addWidget(resultsTable_, 1);
    mainLayout->addWidget(buttons);
}

void AnchorSearchDialog::SetInitialCriteria(
    const std::string& anchorType, const std::string& partName, double diameterMm) {
    anchorType_->setText(QString::fromStdString(anchorType));
    partName_->setText(QString::fromStdString(partName));
    diameter_->setValue(diameterMm);
}

std::string AnchorSearchDialog::AnchorType() const {
    return anchorType_->text().toStdString();
}

std::string AnchorSearchDialog::PartName() const {
    return partName_->text().toStdString();
}

double AnchorSearchDialog::Diameter() const {
    return diameter_->value();
}

void AnchorSearchDialog::onSearchClicked() {
    const int index = modelCombo_->currentIndex();
    if (index < 0 || index >= static_cast<int>(models_.size())) {
        countLabel_->setText("불러온 인치가 없습니다 - 먼저 STEP 파일을 불러오세요.");
        resultsTable_->setRowCount(0);
        return;
    }

    const geometry::ModelHandle handle = models_[static_cast<size_t>(index)].second;
    auto candidates = adapter_->FindAnchorCandidates(
        handle, anchorType_->text().toStdString(), partName_->text().toStdString());

    // RuleEngine이 실제 규칙 평가 때 쓰는 것과 같은 필터 - 여기서 보이는 결과가 곧
    // 이 조건으로 규칙을 저장했을 때 실제로 매칭될 후보와 일치한다.
    std::optional<std::string> paramKey;
    std::optional<double> paramValue;
    if (diameter_->value() > 0.0) {
        paramKey = "diameter";
        paramValue = diameter_->value();
    }
    candidates = rule::FilterByDiameter(std::move(candidates), paramKey, paramValue);

    countLabel_->setText(QString("검색 결과: 총 %1개").arg(candidates.size()));

    resultsTable_->setRowCount(static_cast<int>(candidates.size()));
    for (size_t row = 0; row < candidates.size(); ++row) {
        const auto& c = candidates[row];
        const int r = static_cast<int>(row);
        resultsTable_->setItem(r, 0, new QTableWidgetItem(QString::number(c.position.x, 'f', 3)));
        resultsTable_->setItem(r, 1, new QTableWidgetItem(QString::number(c.position.y, 'f', 3)));
        resultsTable_->setItem(r, 2, new QTableWidgetItem(QString::number(c.position.z, 'f', 3)));
        resultsTable_->setItem(r, 3, new QTableWidgetItem(QString::number(c.diameterMm, 'f', 3)));
    }
}

} // namespace ui
