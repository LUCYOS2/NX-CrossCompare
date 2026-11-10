#include "ui/PresetDimensionDialog.h"

#include "rule/CtpCapture.h"
#include "rule/RuleEngine.h"
#include "ui/CtpImage.h"
#include "viewer/DiagnosticsLog.h"
#include "viewer/MultiViewportPanel.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QPixmap>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace ui {

namespace {
// 회사PC처럼 화면이 작거나 배율이 크면 창이 화면 밖으로 넘쳐 목록/안내가 잘려 보였다(2026-11-06) -
// 뷰어 최소 크기를 줄이고 창 크기는 화면 안으로 맞춘다.
constexpr int kViewerMinWidth = 480;
constexpr int kViewerMinHeight = 360;
constexpr int kFixedColumns = 3; // 이름 / 기준 인치 값 / 설명 - 그 뒤는 "모든 인치에서 확인" 결과 열

double Component(const geometry::Vec3& v, int axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

void SetComponent(geometry::Vec3& v, int axis, double value) {
    (axis == 0 ? v.x : (axis == 1 ? v.y : v.z)) = value;
}

QVector3D ToQt(const geometry::Vec3& v) {
    return QVector3D(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}

int AxisIndex(const std::string& axis) {
    return axis == "X" ? 0 : (axis == "Y" ? 1 : 2);
}

// 형상 평면을 축과의 각도로 나눈 개수 - 후보가 안 나올 때 이유를 보여주려고(2026-11-06, 회사PC "목록 자체가
// 안 생긴다"). 사출품은 옆면에 구배(보통 1~3도)가 있어서 축 수직 허용(0.5도)을 넘으면 끝면에서 빠진다.
struct PlaneAngleCounts {
    int aligned = 0;  // 축과 0.5도 안
    int drafted = 0;  // 0.5~5도 (구배로 보이는 면)
    int other = 0;    // 그 밖
};
PlaneAngleCounts CountPlaneAngles(const std::vector<geometry::PlaneCandidate>& planes) {
    PlaneAngleCounts counts;
    for (const auto& plane : planes) {
        const auto& n = plane.normal;
        const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len < 1e-12) {
            ++counts.other;
            continue;
        }
        const double maxComponent = std::max({std::abs(n.x), std::abs(n.y), std::abs(n.z)}) / len;
        const double angleDeg = std::acos(std::min(1.0, maxComponent)) * 180.0 / 3.14159265358979323846;
        if (angleDeg <= rule::kPresetFaceAxisToleranceDeg) {
            ++counts.aligned;
        } else if (angleDeg <= 5.0) {
            ++counts.drafted;
        } else {
            ++counts.other;
        }
    }
    return counts;
}

// 모델 목록 라벨("50\"  파일.stp")의 앞자리 인치. 인치가 없는 라벨이면 0.
int InchFromLabel(const std::string& label) {
    const QString text = QString::fromStdString(label);
    const int quote = text.indexOf('"');
    if (quote <= 0) {
        return 0;
    }
    bool ok = false;
    const int inch = text.left(quote).trimmed().toInt(&ok);
    return ok ? inch : 0;
}
} // namespace

PresetDimensionDialog::PresetDimensionDialog(
    database::Database* db, int projectId, geometry::IGeometryAdapter* adapter,
    const std::vector<std::pair<std::string, geometry::ModelHandle>>& models, geometry::ModelHandle targetHandle,
    const std::string& presetName, const geometry::FeaturePatchDescriptor& descriptor, QWidget* parent)
    : QDialog(parent),
      db_(db),
      projectId_(projectId),
      adapter_(adapter),
      models_(models),
      targetHandle_(targetHandle),
      presetName_(presetName) {
    setWindowTitle("프리셋 자동 치수");
    {
        QSize size(1400, 860);
        if (const QScreen* screen = parent ? parent->screen() : QGuiApplication::primaryScreen()) {
            const QRect available = screen->availableGeometry();
            size = size.boundedTo(QSize(available.width() * 95 / 100, available.height() * 90 / 100));
        }
        resize(size);
    }

    // ---- 좌측: 미리보기 뷰어(대표 인치 하나) ----
    std::vector<std::pair<std::string, geometry::ModelHandle>> targetModel;
    QString targetLabel;
    for (const auto& model : models_) {
        if (model.second == targetHandle_) {
            targetModel.push_back(model);
            targetLabel = QString::fromStdString(model.first);
        }
    }
    viewerPanel_ = new viewer::MultiViewportPanel(adapter_, targetModel, this);
    viewerPanel_->setMinimumSize(kViewerMinWidth, kViewerMinHeight);
    viewerPanel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(viewerPanel_, &viewer::MultiViewportPanel::cameraChanged, this, [this]() { RefreshPreview(); });

    // ---- 우측: 형상 선택 + 후보 목록 ----
    shapeCombo_ = new QComboBox(this);
    connect(shapeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { OnShapeChanged(); });
    auto* focusButton = new QPushButton("이 형상으로 화면 맞추기", this);
    connect(focusButton, &QPushButton::clicked, this, [this]() {
        const int index = shapeCombo_->currentIndex();
        if (index >= 0 && index < static_cast<int>(shapes_.size()) && shapes_[static_cast<size_t>(index)].hasBox) {
            geometry::BoundingBox box = shapes_[static_cast<size_t>(index)].box;
            // 형상만 꽉 차면 주변(외곽 기준면 방향)이 안 보이므로 형상 크기만큼 여유를 둔다.
            const double pad = std::max({box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z, 10.0});
            box.min = geometry::Vec3{box.min.x - pad, box.min.y - pad, box.min.z - pad};
            box.max = geometry::Vec3{box.max.x + pad, box.max.y + pad, box.max.z + pad};
            viewerPanel_->fitToBox(box);
        }
    });
    auto* shapeRow = new QHBoxLayout();
    shapeRow->addWidget(new QLabel("대상 형상:", this));
    shapeRow->addWidget(shapeCombo_, 1);
    shapeRow->addWidget(focusButton);

    table_ = new QTableWidget(this);
    table_->setColumnCount(kFixedColumns);
    table_->setHorizontalHeaderLabels({"규칙 이름 (체크 = 저장)", "값 (mm)", "측정면"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table_->verticalHeader()->hide();
    table_->setWordWrap(false);
    connect(table_, &QTableWidget::currentCellChanged, this, [this](int, int, int, int) { RefreshPreview(); });

    tolerancePlus_ = new QDoubleSpinBox(this);
    tolerancePlus_->setRange(0.0, 100.0);
    tolerancePlus_->setDecimals(3);
    tolerancePlus_->setSuffix(" mm");
    toleranceMinus_ = new QDoubleSpinBox(this);
    toleranceMinus_->setRange(0.0, 100.0);
    toleranceMinus_->setDecimals(3);
    toleranceMinus_->setSuffix(" mm");
    auto* toleranceRow = new QHBoxLayout();
    toleranceRow->addWidget(new QLabel("저장할 규칙 공통 공차  + ", this));
    toleranceRow->addWidget(tolerancePlus_);
    toleranceRow->addWidget(new QLabel("  - ", this));
    toleranceRow->addWidget(toleranceMinus_);
    toleranceRow->addStretch(1);

    checkAllInchesButton_ = new QPushButton("모든 인치에서 확인", this);
    connect(checkAllInchesButton_, &QPushButton::clicked, this, &PresetDimensionDialog::OnCheckAllInches);
    saveButton_ = new QPushButton("체크한 항목을 규칙으로 저장", this);
    connect(saveButton_, &QPushButton::clicked, this, &PresetDimensionDialog::OnSaveClicked);
    ctpImageButton_ = new QPushButton("CTP 이미지 만들기", this);
    ctpImageButton_->setToolTip("체크한 항목(최대 4개)에 A~D 라벨을 붙여, 시선/단면/확대를 자동으로 잡은 CTP 양식용 그림을 "
                                "만듭니다.");
    connect(ctpImageButton_, &QPushButton::clicked, this, &PresetDimensionDialog::OnMakeCtpImage);
    auto* actionRow = new QHBoxLayout();
    actionRow->addWidget(checkAllInchesButton_);
    actionRow->addWidget(ctpImageButton_);
    actionRow->addStretch(1);
    actionRow->addWidget(saveButton_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet("font-weight: bold;");

    auto* guide = new QLabel(
        "프리셋과 비슷한 형상에서, 버니어가 실제로 닿는 평면 끝면 사이의 치수만 후보로 만듭니다.\n"
        "- 크기: 바깥을 향해 마주 보는 형상의 두 평면 사이 (버니어 두 날이 동시에 물리는 자리, 곡면 끝은 제외)\n"
        "- 위치: 제품 외곽 끝면(전장 사이즈와 같은 면) → 형상의 가까운 끝면\n"
        "행을 고르면 왼쪽 뷰어에 측정면(주황 점)과 치수선이 보입니다. 이름은 더블클릭해서 바꿀 수 있습니다.",
        this);
    guide->setWordWrap(true);
    guide->setStyleSheet("color: #555;");

    auto* rightLayout = new QVBoxLayout();
    rightLayout->addWidget(new QLabel(QString("프리셋: %1    대표 인치: %2")
                                          .arg(QString::fromStdString(presetName_), targetLabel),
                                      this));
    rightLayout->addLayout(shapeRow);
    // 상태(찾은 형상/후보 개수, 못 찾은 이유)는 창이 화면에서 잘려도 보이도록 위쪽에 둔다.
    rightLayout->addWidget(statusLabel_);
    rightLayout->addWidget(guide);
    rightLayout->addWidget(table_, 1);
    rightLayout->addLayout(toleranceRow);
    rightLayout->addLayout(actionRow);
    auto* rightWidget = new QWidget(this);
    rightWidget->setLayout(rightLayout);
    rightWidget->setMinimumWidth(440);
    table_->setMinimumHeight(160);

    auto* topRow = new QHBoxLayout();
    topRow->addWidget(viewerPanel_, 1);
    topRow->addWidget(rightWidget);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(topRow, 1);
    mainLayout->addWidget(buttons);

    // ---- 형상 찾기(대표 인치) ----
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QElapsedTimer timer;
    timer.start();
    modelBox_ = adapter_->GetBoundingBox(targetHandle_);
    modelPlanes_ = adapter_->FindPlaneCandidates(targetHandle_, "Datum_Plane", std::string());
    shapes_ = adapter_->FindPatchCandidates(targetHandle_, descriptor, geometry::kPatchSimilarityThreshold);
    const qint64 searchMs = timer.elapsed();
    QApplication::restoreOverrideCursor();
    LogSearchSummary(searchMs, targetModel.size());

    {
        const QSignalBlocker blocker(shapeCombo_);
        for (size_t i = 0; i < shapes_.size(); ++i) {
            shapeCombo_->addItem(QString("형상 %1 / %2  (유사도 %3)")
                                     .arg(i + 1)
                                     .arg(shapes_.size())
                                     .arg(shapes_[i].similarity, 0, 'f', 2));
        }
    }
    if (shapes_.empty()) {
        statusLabel_->setText(targetModel.empty()
                                  ? "대표 인치 모델을 찾지 못했습니다 - 규칙 관리 창의 대표 인치를 다시 골라 주세요."
                                  : "대표 인치 모델에서 이 프리셋과 비슷한 형상을 찾지 못했습니다.");
        checkAllInchesButton_->setEnabled(false);
        saveButton_->setEnabled(false);
        ctpImageButton_->setEnabled(false);
        focusButton->setEnabled(false);
    } else {
        OnShapeChanged();
        // 레이아웃이 잡힌 뒤(뷰포트 크기가 정해진 뒤) 형상으로 화면을 맞춘다.
        // 뷰포트가 처음 그려지기 전에 넘긴 마커/치수선은 반영이 안 될 수 있어서 미리보기도 다시 넘긴다.
        QTimer::singleShot(150, this, [this, focusButton]() {
            focusButton->click();
            RefreshPreview();
        });
    }
}

void PresetDimensionDialog::OnShapeChanged() {
    candidates_.clear();
    ctpLabelsByRow_.clear();
    ctpImagePath_.clear();
    const int index = shapeCombo_->currentIndex();
    if (index >= 0 && index < static_cast<int>(shapes_.size())) {
        candidates_ =
            rule::ComputePresetDimensionCandidates(shapes_[static_cast<size_t>(index)], modelBox_, modelPlanes_);
    }
    // 같은 형상이 여러 개면 이름에 번호를 붙여 구분한다(다른 형상으로 다시 저장해도 이름이 안 겹치게).
    const std::string baseName =
        shapes_.size() > 1 ? presetName_ + "#" + std::to_string(index + 1) : presetName_;

    const QSignalBlocker blocker(table_);
    table_->setColumnCount(kFixedColumns);
    table_->setRowCount(static_cast<int>(candidates_.size()));
    for (size_t row = 0; row < candidates_.size(); ++row) {
        const auto& c = candidates_[row];
        auto* nameItem = new QTableWidgetItem(QString::fromStdString(rule::DefaultPresetDimensionName(baseName, c)));
        nameItem->setFlags(nameItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        // 형상 크기는 기본 체크, 외곽에서의 위치는 필요한 것만 고르도록 기본 해제.
        nameItem->setCheckState(c.kind == rule::PresetDimensionKind::ShapeSize ? Qt::Checked : Qt::Unchecked);
        table_->setItem(static_cast<int>(row), 0, nameItem);
        auto* valueItem = new QTableWidgetItem(QString::number(c.valueMm, 'f', 3));
        valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
        valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(static_cast<int>(row), 1, valueItem);
        auto* descItem = new QTableWidgetItem(QString::fromStdString(rule::DescribePresetDimension(c)));
        descItem->setFlags(descItem->flags() & ~Qt::ItemIsEditable);
        table_->setItem(static_cast<int>(row), 2, descItem);
    }
    if (candidates_.empty()) {
        const PlaneAngleCounts counts = (index >= 0 && index < static_cast<int>(shapes_.size()))
                                            ? CountPlaneAngles(shapes_[static_cast<size_t>(index)].planes)
                                            : PlaneAngleCounts{};
        statusLabel_->setText(
            QString("형상 %1개를 찾았지만 이 형상에서는 잴 만한 평면 끝면 쌍을 찾지 못했습니다. "
                    "이 형상의 평면 %2개 = 축에 수직 %3개 + 조금 기울어짐(0.5~5도, 구배로 보임) %4개 + 그 밖 %5개. "
                    "지금은 축에 수직인 면만 씁니다 - \"지정...\"으로 직접 찍어 주세요.")
                .arg(shapes_.size())
                .arg(counts.aligned + counts.drafted + counts.other)
                .arg(counts.aligned)
                .arg(counts.drafted)
                .arg(counts.other));
    } else {
        statusLabel_->setText(QString("형상 %1개 찾음 - 이 형상의 치수 후보 %2개").arg(shapes_.size()).arg(candidates_.size()));
        table_->setCurrentCell(0, 0);
    }
    RefreshPreview();
}

// § 진단 기록(2026-11-06) - 회사PC에서 "화면 확대도 안 되고 후보 목록도 안 생긴다"는 리포트. 무엇이 비었는지
// (모델/형상/평면/후보) 앱 로그(diag_logs/app_log.txt)에 남긴다. 도면 정보(좌표/치수/파일명)는 쓰지 않고
// 개수와 모델 대비 비율(%)만 쓴다.
void PresetDimensionDialog::LogSearchSummary(qint64 searchMs, size_t viewerModelCount) const {
    auto diagonal = [](const geometry::BoundingBox& box) {
        const double dx = box.max.x - box.min.x;
        const double dy = box.max.y - box.min.y;
        const double dz = box.max.z - box.min.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    const double modelDiagonal = diagonal(modelBox_);
    viewer::diag::Log(QString("[프리셋 자동 치수] 뷰어 모델 %1개, 모델 평면 %2개, 찾은 형상 %3개, 찾는 데 %4ms")
                          .arg(viewerModelCount)
                          .arg(modelPlanes_.size())
                          .arg(shapes_.size())
                          .arg(searchMs));
    const size_t shown = std::min<size_t>(shapes_.size(), 10);
    for (size_t i = 0; i < shown; ++i) {
        const auto& shape = shapes_[i];
        const auto candidates = rule::ComputePresetDimensionCandidates(shape, modelBox_, modelPlanes_);
        int sizeCount = 0;
        for (const auto& c : candidates) {
            sizeCount += c.kind == rule::PresetDimensionKind::ShapeSize ? 1 : 0;
        }
        const double ratio = (shape.hasBox && modelDiagonal > 0.0) ? diagonal(shape.box) / modelDiagonal * 100.0 : -1.0;
        const PlaneAngleCounts counts = CountPlaneAngles(shape.planes);
        viewer::diag::Log(QString("[프리셋 자동 치수]   형상 %1: 유사도 %2, 범위 있음 %3, 형상 크기/모델 크기 %4%, "
                                  "형상 평면 %5개(축 수직 %6 / 0.5~5도 %7 / 그 밖 %8), 후보 크기 %9개 + 위치 %10개")
                              .arg(i + 1)
                              .arg(shape.similarity, 0, 'f', 2)
                              .arg(shape.hasBox ? "예" : "아니오")
                              .arg(ratio, 0, 'f', 1)
                              .arg(shape.planes.size())
                              .arg(counts.aligned)
                              .arg(counts.drafted)
                              .arg(counts.other)
                              .arg(sizeCount)
                              .arg(static_cast<int>(candidates.size()) - sizeCount));
    }
}

std::string PresetDimensionDialog::ChooseOffsetAxis(int measuredAxis) const {
    // RuleEditorDialog::ChooseOverallSizeOffsetAxis와 같은 기준 - 나머지 두 축 중 카메라 시선과 가장 수직인 축
    // (치수선이 화면에서 한 점으로 찌그러지지 않게).
    std::vector<int> candidates;
    for (int axis = 0; axis < 3; ++axis) {
        if (axis != measuredAxis) {
            candidates.push_back(axis);
        }
    }
    const viewer::Camera& camera = viewerPanel_->sharedCamera();
    const double yawRad = camera.yawDeg * 0.017453292519943295;
    const double pitchRad = camera.pitchDeg * 0.017453292519943295;
    const double forward[3] = {std::cos(pitchRad) * std::sin(yawRad), -std::sin(pitchRad),
                               -std::cos(pitchRad) * std::cos(yawRad)};
    int best = candidates.front();
    double bestAlignment = std::numeric_limits<double>::max();
    for (int axis : candidates) {
        const double alignment = std::abs(forward[axis]);
        if (alignment < bestAlignment) {
            bestAlignment = alignment;
            best = axis;
        }
    }
    return rule::AxisName(best);
}

void PresetDimensionDialog::RefreshPreview() {
    const int row = table_->currentRow();
    const int shapeIndex = shapeCombo_->currentIndex();
    if (row < 0 || row >= static_cast<int>(candidates_.size()) || shapeIndex < 0 ||
        shapeIndex >= static_cast<int>(shapes_.size())) {
        viewerPanel_->SetMarkerPositions({});
        viewerPanel_->SetDimensionLine({});
        viewerPanel_->SetDimensionExtensionLines({}, {});
        viewerPanel_->SetDimensionLabel(QString());
        return;
    }
    const auto& c = candidates_[static_cast<size_t>(row)];
    const auto& shapeBox = shapes_[static_cast<size_t>(shapeIndex)].box;
    const int measured = c.axis;
    const int offsetAxis = AxisIndex(ChooseOffsetAxis(measured));
    const int thirdAxis = 3 - measured - offsetAxis;

    // 측정면 위의 실제 점(마커) -> 형상 바깥 여백으로 뺀 치수선(전장 사이즈와 같은 도면 표기).
    const double extent = Component(shapeBox.max, offsetAxis) - Component(shapeBox.min, offsetAxis);
    const double offsetValue = Component(shapeBox.max, offsetAxis) + std::max(5.0, extent * 0.5);
    const double thirdValue = (Component(shapeBox.min, thirdAxis) + Component(shapeBox.max, thirdAxis)) * 0.5;
    geometry::Vec3 dispA = c.refA;
    geometry::Vec3 dispB = c.refB;
    for (geometry::Vec3* p : {&dispA, &dispB}) {
        SetComponent(*p, offsetAxis, offsetValue);
        SetComponent(*p, thirdAxis, thirdValue);
    }
    viewerPanel_->SetMarkerPositions({ToQt(c.refA), ToQt(c.refB)});
    viewerPanel_->SetDimensionLine({ToQt(dispA), ToQt(dispB)});
    viewerPanel_->SetDimensionExtensionLines({ToQt(c.refA), ToQt(c.refB)}, {ToQt(dispA), ToQt(dispB)});
    viewerPanel_->SetDimensionLabel(QString::number(c.valueMm, 'f', 2));
    viewerPanel_->SetDimensionOffsetAxis(QVector3D()); // 이 창에서는 치수선 드래그 없음
}

rule::Rule PresetDimensionDialog::BuildRule(int row) const {
    const auto& c = candidates_[static_cast<size_t>(row)];
    QString name = table_->item(row, 0) ? table_->item(row, 0)->text().trimmed() : QString();
    std::string nameText = name.isEmpty() ? rule::DefaultPresetDimensionName(presetName_, c) : name.toStdString();
    rule::Rule r = rule::MakePresetDimensionRule(c, nameText, modelBox_);
    // 클릭한 평면과 같은 방법으로 기준 인치에서 주변 지문을 확정한다(다른 인치에서 엉뚱한 면을 막는 검증).
    rule::RuleEngine::FinalizePlaneRef(*adapter_, targetHandle_, *r.anchors[0].ref);
    rule::RuleEngine::FinalizePlaneRef(*adapter_, targetHandle_, *r.anchors[1].ref);
    r.tolerancePlusMm = tolerancePlus_->value();
    r.toleranceMinusMm = toleranceMinus_->value();
    return r;
}

void PresetDimensionDialog::OnCheckAllInches() {
    std::map<int, geometry::ModelHandle> handlesByInch;
    for (const auto& [label, handle] : models_) {
        const int inch = InchFromLabel(label);
        if (inch > 0) {
            handlesByInch[inch] = handle;
        }
    }
    if (handlesByInch.empty() || candidates_.empty()) {
        statusLabel_->setText("확인할 인치가 없습니다 - 인치가 지정된 STEP을 불러왔는지 확인하세요.");
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QSignalBlocker blocker(table_);
    table_->setColumnCount(kFixedColumns + static_cast<int>(handlesByInch.size()));
    int column = kFixedColumns;
    for (const auto& [inch, handle] : handlesByInch) {
        (void)handle;
        table_->setHorizontalHeaderItem(column, new QTableWidgetItem(QString("%1\"").arg(inch)));
        table_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
        ++column;
    }
    int failures = 0;
    for (int row = 0; row < static_cast<int>(candidates_.size()); ++row) {
        const auto results = rule::RuleEngine::Evaluate(*adapter_, handlesByInch, BuildRule(row));
        for (const auto& result : results) {
            const auto it = handlesByInch.find(result.inch);
            const int col = kFixedColumns + static_cast<int>(std::distance(handlesByInch.begin(), it));
            QTableWidgetItem* item = nullptr;
            if (result.ok) {
                item = new QTableWidgetItem(QString::number(result.value, 'f', 3));
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                if (!result.warning.empty()) {
                    item->setBackground(QColor(255, 235, 160));
                    item->setToolTip(QString::fromStdString(result.warning));
                }
            } else {
                ++failures;
                item = new QTableWidgetItem("실패");
                item->setBackground(QColor(255, 200, 200));
                item->setToolTip(QString::fromStdString(result.error));
            }
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            table_->setItem(row, col, item);
        }
    }
    QApplication::restoreOverrideCursor();
    statusLabel_->setText(failures == 0
                              ? QString("모든 인치에서 값을 냈습니다(노란 칸은 추세와 달라 확인 필요 - 칸에 마우스를 올리면 사유).")
                              : QString("값을 못 낸 칸 %1개 - 빨간 칸에 마우스를 올리면 사유가 보입니다. 그 항목은 체크를 빼는 "
                                        "것을 권합니다.")
                                    .arg(failures));
}

void PresetDimensionDialog::OnSaveClicked() {
    std::vector<int> rows;
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (table_->item(row, 0) && table_->item(row, 0)->checkState() == Qt::Checked) {
            rows.push_back(row);
        }
    }
    if (rows.empty()) {
        QMessageBox::information(this, "저장할 항목 없음", "저장할 치수 후보를 체크하세요.");
        return;
    }

    std::set<std::string> names;
    for (const auto& existing : db_->LoadRulesForProject(projectId_)) {
        names.insert(existing.name);
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int saved = 0;
    QString error;
    for (int row : rows) {
        rule::Rule r = BuildRule(row);
        // CTP 이미지에 들어간 행이면 그 라벨(Point)과 이미지를 같이 저장한다.
        if (const auto it = ctpLabelsByRow_.find(row); it != ctpLabelsByRow_.end() && !ctpImagePath_.empty()) {
            r.ctqCode = it->second;
            r.imagePath = ctpImagePath_;
        }
        // 같은 이름이 이미 있으면 번호를 붙인다(규칙 목록에서 구분이 안 되므로).
        const std::string base = r.name;
        for (int n = 2; names.count(r.name) > 0; ++n) {
            r.name = base + " (" + std::to_string(n) + ")";
        }
        try {
            db_->SaveRule(projectId_, r);
        } catch (const std::exception& e) {
            error = QString::fromStdString(e.what());
            break;
        }
        names.insert(r.name);
        ++saved;
    }
    QApplication::restoreOverrideCursor();
    savedRuleCount_ += saved;
    if (!error.isEmpty()) {
        QMessageBox::critical(this, "저장 실패", QString("%1개 저장 후 실패: %2").arg(saved).arg(error));
        return;
    }
    QMessageBox::information(this, "저장 완료",
                             QString("규칙 %1개를 저장했습니다. 규칙 목록에서 하나씩 열어 Point(부위)/도면 스펙을 채우세요.")
                                 .arg(saved));
    accept();
}

void PresetDimensionDialog::OnMakeCtpImage() {
    const int shapeIndex = shapeCombo_->currentIndex();
    if (shapeIndex < 0 || shapeIndex >= static_cast<int>(shapes_.size())) {
        return;
    }
    std::vector<int> rows;
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (table_->item(row, 0) && table_->item(row, 0)->checkState() == Qt::Checked) {
            rows.push_back(row);
        }
    }
    if (rows.empty()) {
        QMessageBox::information(this, "CTP 이미지", "그림에 넣을 치수 후보를 체크하세요(한 장에 최대 4개).");
        return;
    }
    if (rows.size() > 4) {
        QMessageBox::information(this, "CTP 이미지",
                                 QString("CTP 한 페이지에는 치수가 최대 4개입니다. 지금 %1개가 체크돼 있습니다 - 4개 이하로 "
                                         "골라 주세요.")
                                     .arg(rows.size()));
        return;
    }

    std::map<int, std::string> labels;
    std::vector<rule::CtpDimensionInput> dims;
    for (size_t i = 0; i < rows.size(); ++i) {
        const std::string label(1, static_cast<char>('A' + i));
        labels[rows[i]] = label;
        dims.push_back(rule::MakeCtpDimension(candidates_[static_cast<size_t>(rows[i])], label));
    }
    int inchCount = 0;
    for (const auto& model : models_) {
        inchCount += InchFromLabel(model.first) > 0 ? 1 : 0;
    }
    const auto& shape = shapes_[static_cast<size_t>(shapeIndex)];
    const double aspect = rule::CtpImageAspect(static_cast<int>(dims.size()), inchCount > 0 ? inchCount : 5);
    const rule::CtpCapturePlan plan = rule::PlanCtpCapture(dims, shape.box, shape.planes, aspect);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QImage image = RenderCtpPlan(viewerPanel_, targetHandle_, plan);
    QApplication::restoreOverrideCursor();
    if (image.isNull()) {
        QMessageBox::warning(this, "CTP 이미지", "그림을 만들지 못했습니다(그래픽 초기화 전이거나 화면 밖 그리기 실패). "
                                                 "뷰어가 한 번 그려진 뒤 다시 눌러 주세요.");
        return;
    }
    viewer::diag::Log(QString("[CTP 캡처] 치수 %1개, 시선 축 %2, 이미지 %3x%4")
                          .arg(dims.size())
                          .arg(QString::fromStdString(rule::AxisName(plan.view.viewAxis)))
                          .arg(image.width())
                          .arg(image.height()));

    // 미리보기: 화면에 맞게 줄여 보여 주고, 저장을 누르면 rule_images에 PNG로 남긴다.
    QDialog preview(this);
    preview.setWindowTitle("CTP 이미지 미리보기");
    auto* imageLabel = new QLabel(&preview);
    const QSize maxSize(1100, 650);
    imageLabel->setPixmap(QPixmap::fromImage(image).scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    imageLabel->setFrameShape(QFrame::Box);
    QStringList lines;
    for (size_t i = 0; i < rows.size(); ++i) {
        const int row = rows[i];
        lines << QString("%1 = %2 (%3 mm)")
                     .arg(QString::fromStdString(labels[row]))
                     .arg(table_->item(row, 0)->text())
                     .arg(candidates_[static_cast<size_t>(row)].valueMm, 0, 'f', 2);
    }
    auto* info = new QLabel(lines.join("\n") + (plan.warning.empty()
                                                    ? QString()
                                                    : "\n\n" + QString::fromStdString(plan.warning)),
                            &preview);
    info->setWordWrap(true);
    auto* buttons = new QDialogButtonBox(&preview);
    buttons->addButton("저장 (규칙 저장 시 함께 연결)", QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &preview, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &preview, &QDialog::reject);
    auto* layout = new QVBoxLayout(&preview);
    layout->addWidget(imageLabel, 1);
    layout->addWidget(info);
    layout->addWidget(buttons);
    if (preview.exec() != QDialog::Accepted) {
        return;
    }
    const QString path = SaveCtpImage(image);
    if (path.isEmpty()) {
        QMessageBox::warning(this, "CTP 이미지", "이미지를 저장하지 못했습니다(rule_images 폴더 확인).");
        return;
    }
    ctpLabelsByRow_ = labels;
    ctpImagePath_ = path.toStdString();
    statusLabel_->setText(QString("CTP 이미지 저장: %1 - \"체크한 항목을 규칙으로 저장\"하면 라벨(A~)과 이미지가 규칙에 "
                                  "같이 들어갑니다.")
                              .arg(QDir::toNativeSeparators(path)));
}

} // namespace ui
