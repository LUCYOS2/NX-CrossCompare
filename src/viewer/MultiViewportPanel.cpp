#include "viewer/MultiViewportPanel.h"
#include "viewer/ModelViewport.h"

#include "geometry/AlignmentCheck.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QMatrix4x4>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QString>
#include <QVBoxLayout>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace viewer {

namespace {

geometry::BoundingBox Union(const geometry::BoundingBox& a, const geometry::BoundingBox& b) {
    return geometry::BoundingBox{
        {std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y), std::min(a.min.z, b.min.z)},
        {std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y), std::max(a.max.z, b.max.z)},
    };
}

double AxisMin(const geometry::BoundingBox& box, int axis) {
    return axis == 0 ? box.min.x : (axis == 1 ? box.min.y : box.min.z);
}

double AxisMax(const geometry::BoundingBox& box, int axis) {
    return axis == 0 ? box.max.x : (axis == 1 ? box.max.y : box.max.z);
}

} // namespace

MultiViewportPanel::MultiViewportPanel(geometry::IGeometryAdapter* adapter,
                                        const std::vector<std::pair<std::string, geometry::ModelHandle>>& models,
                                        QWidget* parent)
    : QWidget(parent), adapter_(adapter) {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    outerLayout->addWidget(buildSectionControlBar());

    auto* gridContainer = new QWidget(this);
    auto* grid = new QGridLayout(gridContainer);
    outerLayout->addWidget(gridContainer, 1);

    const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(models.size())))));

    int placed = 0;
    for (const auto& [label, handle] : models) {
        // LoadModel은 호출자(MainWindow)가 이미 끝냈다 - 여기서는 다시 파싱하지 않는다.
        const auto box = adapter->GetBoundingBox(handle);

        // 정렬 QC: 어셈블리 중심이 절대좌표 원점 근처인지 확인 (개발계획_v2.md §5)
        const auto alignment = geometry::CheckCenterAlignment(box, /*toleranceMm=*/1.0);
        if (!alignment.aligned) {
            qWarning() << QString::fromStdString(label)
                       << "정렬 QC 실패 - 중심 오프셋(mm):" << alignment.offsetMm;
        } else {
            qDebug() << QString::fromStdString(label) << "정렬 QC 통과";
        }

        handles_.push_back(handle);

        auto* container = new QWidget(gridContainer);
        auto* vbox = new QVBoxLayout(container);
        vbox->setContentsMargins(2, 2, 2, 2);

        auto* labelWidget = new QLabel(QString::fromStdString(label), container);
        labelWidget->setAlignment(Qt::AlignCenter);

        auto* viewport = new ModelViewport(adapter, handle, &camera_, container);
        viewport->SetIndependentModePtr(&independentMode_);
        viewport->SetPickModePtr(&pickModeActive_);
        viewports_.push_back(viewport);

        vbox->addWidget(labelWidget);
        vbox->addWidget(viewport, 1);

        connect(viewport, &ModelViewport::cameraChanged, this, [this]() {
            for (auto* vp : viewports_) {
                vp->update();
            }
            // 단축키(H/X/Y/Z/+/-)로 바뀐 상태를 컨트롤 바에도 반영 - 두 입력 경로가 같은
            // Camera를 공유하므로 여기서 한 번에 동기화한다.
            syncSectionControlsFromCamera();
        });
        connect(viewport, &ModelViewport::hoverEntered, this, [this, viewport]() {
            hoveredViewport_ = viewport;
        });
        // § 3D 클릭 피킹 - 어느 뷰포트에서 찍었든 패널의 facePicked 하나로 모아서 내보낸다.
        connect(viewport, &ModelViewport::facePicked, this, &MultiViewportPanel::facePicked);

        const int row = placed / columns;
        const int col = placed % columns;
        grid->addWidget(container, row, col);
        ++placed;
    }

    // hoverEntered가 한 번도 안 왔을 때(마우스를 아직 안 움직였을 때)를 위한 기본값 -
    // 첫 번째 뷰포트를 미리 "현재 대상"으로 잡아둔다.
    if (!viewports_.empty()) {
        hoveredViewport_ = viewports_.front();
    }

    fitToView();
    updateSectionRangeForAxis(camera_.sectionAxis);
    syncSectionControlsFromCamera();
    setupShortcuts();
}

// H/X/Y/Z/+/- 단축키 - QShortcut(Qt::WidgetWithChildrenShortcut)이라 이 패널(컨트롤 바
// 포함) 안 어디에 포커스가 있어도 동작한다. F/I는 로드된 전체 인치에 적용되는 동작이라
// hoveredViewport_ 없이 바로 처리하고, H/X/Y/Z(+/-도 결국 축 하나를 다루므로 동일)는
// "현재 마우스가 올라가 있는 뷰포트" 기준으로 레이캐스팅해야 해서 hoveredViewport_를 쓴다.
void MultiViewportPanel::setupShortcuts() {
    // WidgetWithChildrenShortcut를 this(MultiViewportPanel)에 걸면 포커스가 이 패널
    // 서브트리 밖(예: MainWindow 쪽 비교 테이블 dock)으로 나가는 순간 전혀 안 먹었다
    // (사용자 리포트: 테이블에 포커스가 있을 때 I를 누르면 ISO 리셋 대신 셀 편집 모드로
    // 들어감). ApplicationShortcut까지 넓히면 이 문제는 없어지지만, 대신 RuleEditorDialog
    // 등 모달 다이얼로그의 QLineEdit(규칙 이름 등)에 h/i/x/y/z/f/+/- 같은 흔한 글자를 칠
    // 때도 단축키가 가로채버려서 텍스트 입력이 깨진다. 절충점: 부모를 this가 아니라
    // MultiViewportPanel의 부모(MainWindow, setCentralWidget으로 붙임)로 걸어서 범위를
    // "중앙 위젯 + 그 형제인 dock들"까지만 넓힌다 - 비교 테이블은 커버하고, 별도
    // 톱레벨 윈도우인 모달 다이얼로그는 여전히 범위 밖이라 텍스트 입력을 방해하지 않는다.
    QWidget* shortcutScope = parentWidget() ? parentWidget() : static_cast<QWidget*>(this);
    auto addShortcut = [this, shortcutScope](QKeySequence keySequence, auto slot) {
        // MainWindow::setupCentralViewer()가 STEP 재로드마다 새 MultiViewportPanel을 만들고
        // 그때마다 setupShortcuts()도 다시 호출된다. shortcutScope(MainWindow)는 패널과 달리
        // 재로드해도 안 죽는 위젯이라, 예전 패널이 걸어둔 같은 키의 QShortcut을 안 지우고
        // 두면 같은 키에 단축키가 2개 이상 걸려 Qt가 "ambiguous"로 판정해서 activated()가
        // 아예 안 터진다(재현: STEP 파일을 다시 불러온 뒤로는 F/H/I가 전부 먹통이 됨) - 새로
        // 걸기 전에 같은 키를 쓰던 이전 것부터 지운다.
        for (QShortcut* existing : shortcutScope->findChildren<QShortcut*>(
                 QString(), Qt::FindDirectChildrenOnly)) {
            if (existing->key() == keySequence) {
                delete existing;
            }
        }
        auto* shortcut = new QShortcut(keySequence, shortcutScope);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, slot);
        return shortcut;
    };

    addShortcut(QKeySequence(Qt::Key_F), [this]() { fitToView(); });
    addShortcut(QKeySequence(Qt::Key_I), [this]() { resetToIsoView(); });
    addShortcut(QKeySequence(Qt::Key_H), [this]() {
        if (hoveredViewport_) {
            hoveredViewport_->toggleSection();
        }
    });
    addShortcut(QKeySequence(Qt::Key_X), [this]() {
        if (hoveredViewport_ && camera_.sectionEnabled) {
            hoveredViewport_->switchSectionAxis(0);
        }
    });
    addShortcut(QKeySequence(Qt::Key_Y), [this]() {
        if (hoveredViewport_ && camera_.sectionEnabled) {
            hoveredViewport_->switchSectionAxis(1);
        }
    });
    addShortcut(QKeySequence(Qt::Key_Z), [this]() {
        if (hoveredViewport_ && camera_.sectionEnabled) {
            hoveredViewport_->switchSectionAxis(2);
        }
    });
    auto nudge = [this](float deltaMm) {
        if (hoveredViewport_ && camera_.sectionEnabled) {
            hoveredViewport_->nudgeSection(deltaMm);
        }
    };
    // "+"/"-"는 물리 키 하나에 Shift로 두 문자를 겸하는 배열이 많아 Plus/Equal을 같이 등록.
    // 큰 폭 이동은 +/- 대신 Page Up/Down으로 분리해서 Shift+= 입력과 안 헷갈리게 했다.
    addShortcut(QKeySequence(Qt::Key_Plus), [nudge]() { nudge(1.0f); });
    addShortcut(QKeySequence(Qt::Key_Equal), [nudge]() { nudge(1.0f); });
    addShortcut(QKeySequence(Qt::Key_Minus), [nudge]() { nudge(-1.0f); });
    addShortcut(QKeySequence(Qt::Key_PageUp), [nudge]() { nudge(10.0f); });
    addShortcut(QKeySequence(Qt::Key_PageDown), [nudge]() { nudge(-10.0f); });
}

// 단면 뷰(H) 컨트롤 바 - 체크박스(켜기) + X/Y/Z 축 버튼 + 방향 반전 + 슬라이더 + mm 숫자
// 입력(사용자 요청). ModelViewport의 키보드 단축키(H/X/Y/Z/+/-)와 같은 Camera를 공유해서
// 조작하므로, 여기서 값을 바꾸면 바로 반영되고 반대로 키보드로 바꾼 값도
// syncSectionControlsFromCamera()로 이 위젯들에 반영된다.
QWidget* MultiViewportPanel::buildSectionControlBar() {
    auto* bar = new QWidget(this);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 4, 8, 4);

    sectionEnableCheck_ = new QCheckBox("단면 보기(H)", bar);
    layout->addWidget(sectionEnableCheck_);

    layout->addSpacing(12);
    layout->addWidget(new QLabel("축:", bar));
    auto* axisGroup = new QButtonGroup(bar);
    const char* axisLabels[3] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
        axisButtons_[i] = new QPushButton(axisLabels[i], bar);
        axisButtons_[i]->setCheckable(true);
        axisButtons_[i]->setFixedWidth(28);
        axisGroup->addButton(axisButtons_[i], i);
        layout->addWidget(axisButtons_[i]);
    }
    axisButtons_[0]->setChecked(true);

    sectionFlipButton_ = new QPushButton("방향 반전", bar);
    sectionFlipButton_->setCheckable(true);
    layout->addWidget(sectionFlipButton_);

    layout->addSpacing(12);
    layout->addWidget(new QLabel("위치:", bar));
    sectionSlider_ = new QSlider(Qt::Horizontal, bar);
    layout->addWidget(sectionSlider_, 1);

    sectionValueSpin_ = new QDoubleSpinBox(bar);
    sectionValueSpin_->setDecimals(1);
    sectionValueSpin_->setSuffix(" mm");
    sectionValueSpin_->setRange(-1e6, 1e6);
    layout->addWidget(sectionValueSpin_);

    connect(sectionEnableCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (syncingSectionControls_) {
            return;
        }
        camera_.sectionEnabled = checked;
        if (checked) {
            // 켤 때 좌표가 0(uninitialized 상태)이면 의미가 없으니 현재 축의 bbox
            // 중심으로 시작한다 - 마우스 클릭 없이 켠 경우(H 키/체크박스 공통).
            const auto box = unionBoxOfHandles();
            camera_.sectionCoord = static_cast<float>(
                (AxisMin(box, camera_.sectionAxis) + AxisMax(box, camera_.sectionAxis)) * 0.5);
        }
        for (auto* vp : viewports_) {
            vp->update();
        }
        syncSectionControlsFromCamera();
    });

    connect(axisGroup, &QButtonGroup::idClicked, this, [this](int axisId) {
        if (syncingSectionControls_) {
            return;
        }
        camera_.sectionAxis = axisId;
        updateSectionRangeForAxis(axisId);
        const auto box = unionBoxOfHandles();
        camera_.sectionCoord = static_cast<float>((AxisMin(box, axisId) + AxisMax(box, axisId)) * 0.5);
        for (auto* vp : viewports_) {
            vp->update();
        }
        syncSectionControlsFromCamera();
    });

    connect(sectionFlipButton_, &QPushButton::toggled, this, [this](bool flipped) {
        if (syncingSectionControls_) {
            return;
        }
        camera_.sectionSign = flipped ? -1.0f : 1.0f;
        for (auto* vp : viewports_) {
            vp->update();
        }
    });

    connect(sectionSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (syncingSectionControls_) {
            return;
        }
        camera_.sectionCoord = static_cast<float>(value);
        syncingSectionControls_ = true;
        sectionValueSpin_->setValue(value);
        syncingSectionControls_ = false;
        for (auto* vp : viewports_) {
            vp->update();
        }
    });

    connect(sectionValueSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (syncingSectionControls_) {
            return;
        }
        camera_.sectionCoord = static_cast<float>(value);
        syncingSectionControls_ = true;
        sectionSlider_->setValue(static_cast<int>(std::lround(value)));
        syncingSectionControls_ = false;
        for (auto* vp : viewports_) {
            vp->update();
        }
    });

    return bar;
}

// 컨트롤 위젯 상태를 camera_에서 다시 읽어와 맞춘다 - 키보드 단축키로 바뀐 값도
// 이 함수 하나로 체크박스/축 버튼/방향 반전/슬라이더/스핀박스에 전부 반영된다.
void MultiViewportPanel::syncSectionControlsFromCamera() {
    syncingSectionControls_ = true;

    sectionEnableCheck_->setChecked(camera_.sectionEnabled);
    for (int i = 0; i < 3; ++i) {
        axisButtons_[i]->setChecked(i == camera_.sectionAxis);
    }
    sectionFlipButton_->setChecked(camera_.sectionSign < 0.0f);

    // 슬라이더 범위가 현재 좌표를 못 담으면(축 전환 등) 넓혀준다.
    if (camera_.sectionCoord < sectionSlider_->minimum() || camera_.sectionCoord > sectionSlider_->maximum()) {
        updateSectionRangeForAxis(camera_.sectionAxis);
    }
    sectionSlider_->setValue(static_cast<int>(std::lround(camera_.sectionCoord)));
    sectionValueSpin_->setValue(camera_.sectionCoord);

    const bool enabled = camera_.sectionEnabled;
    for (auto* btn : axisButtons_) {
        btn->setEnabled(enabled);
    }
    sectionFlipButton_->setEnabled(enabled);
    sectionSlider_->setEnabled(enabled);
    sectionValueSpin_->setEnabled(enabled);

    syncingSectionControls_ = false;
}

void MultiViewportPanel::updateSectionRangeForAxis(int axis) {
    const auto box = unionBoxOfHandles();
    const double lo = AxisMin(box, axis);
    const double hi = AxisMax(box, axis);
    const double margin = std::max(1.0, (hi - lo) * 0.2);
    const int sliderMin = static_cast<int>(std::floor(lo - margin));
    const int sliderMax = static_cast<int>(std::ceil(hi + margin));

    const bool wasSyncing = syncingSectionControls_;
    syncingSectionControls_ = true;
    sectionSlider_->setRange(sliderMin, sliderMax);
    sectionValueSpin_->setRange(sliderMin, sliderMax);
    syncingSectionControls_ = wasSyncing;
}

geometry::BoundingBox MultiViewportPanel::unionBoxOfHandles() const {
    if (handles_.empty()) {
        return geometry::BoundingBox{};
    }
    geometry::BoundingBox box = adapter_->GetBoundingBox(handles_.front());
    for (size_t i = 1; i < handles_.size(); ++i) {
        box = Union(box, adapter_->GetBoundingBox(handles_[i]));
    }
    return box;
}

// 단축키 I(ISO 뷰) - 바닥면=X-Z 평면, 수직=+Y 축이라는 좌표계 정의(사용자 확인 완료,
// 다이어그램 참고)에서 표준 등각 투영 각도(yaw=45°, pitch=35.264° - 세 축이 동일하게
// 축소되어 보이는 진짜 isometric 각도)로 회전을 리셋한 뒤 fitToView()로 다시 맞춘다.
// pitch는 반드시 양수여야 한다 - Camera::ViewMatrix()가 world를 rotateX(pitch)로 돌린
// 뒤 -distance만큼 밀어내는 구조라, pitch가 음수면 카메라가 원점보다 아래(-Y)에 위치해
// 바닥을 뚫고 위로 올려다보는 그림이 된다(최초 구현 버그, 사용자 리포트: ISO 뷰가
// 바닥/수직 방향이 뒤집혀 보임). fitToView()의 팬 계산이 "현재 회전값 기준"이라, 회전만
// 바꾸고 팬을 그대로 두면 모델이 화면 밖으로 밀려나므로 항상 같이 호출해야 한다.
void MultiViewportPanel::resetToIsoView() {
    camera_.yawDeg = 45.0f;
    camera_.pitchDeg = 35.264f;
    fitToView();
}

// 단축키 F 및 최초 로드 시 공용 - 로드된 모든 인치의 bounding box 합집합 "중심"에 카메라를
// 맞춘다. 예전엔 월드 원점 기준 거리만 조절하고 팬은 항상 0으로 리셋했는데, §5 컨벤션대로
// 원점 정렬된 모델에서는 문제없지만 ROI로 잘라낸 부분 형상처럼 원점에서 멀리 떨어진 모델은
// 화면 프레임 안에 들어와도 한쪽 구석에 점처럼 작게 찍혀 사실상 안 보이는 문제가 있었다
// (사용자 리포트: STEP을 불러왔는데 뷰포트에 아무것도 안 보임). 이제 bounding box 중심을
// 구해서 현재 회전(yaw/pitch) 기준으로 화면 정중앙에 오도록 팬을 계산한다.
void MultiViewportPanel::fitToView() {
    if (handles_.empty()) {
        return;
    }

    const geometry::BoundingBox unionBox = unionBoxOfHandles();
    const double dx = unionBox.max.x - unionBox.min.x;
    const double dy = unionBox.max.y - unionBox.min.y;
    const double dz = unionBox.max.z - unionBox.min.z;
    const double halfDiagonal = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5;
    if (halfDiagonal <= 0.0) {
        return;
    }
    // tan(22.5deg) ~= 0.414 (paintGL의 45도 수직 FOV 절반) 만큼 여유를 두고 맞춘다.
    camera_.distance = static_cast<float>(halfDiagonal / 0.414 * 1.15);

    const QVector3D center(static_cast<float>((unionBox.min.x + unionBox.max.x) * 0.5),
                            static_cast<float>((unionBox.min.y + unionBox.max.y) * 0.5),
                            static_cast<float>((unionBox.min.z + unionBox.max.z) * 0.5));
    // Camera::ViewMatrix()는 translate(pan) * rotateX(pitch) * rotateY(yaw) 순서로 곱해지므로
    // (한 점에 적용하면 회전이 먼저 적용됨), 여기서도 같은 순서로 회전만 미리 적용해 본 뒤
    // 그 결과가 화면 중앙(x=0,y=0)에 오도록 팬을 역산한다.
    QMatrix4x4 rotationOnly;
    rotationOnly.rotate(camera_.pitchDeg, 1.0f, 0.0f, 0.0f);
    rotationOnly.rotate(camera_.yawDeg, 0.0f, 1.0f, 0.0f);
    const QVector3D rotatedCenter = rotationOnly * center;
    camera_.panX = -rotatedCenter.x();
    camera_.panY = -rotatedCenter.y();

    for (auto* vp : viewports_) {
        // F/I는 "전체 공통 리셋" 동작이라, 독립 조작 모드 중이었더라도 각 뷰포트의 로컬
        // 회전/팬/줌을 방금 계산한 공유 카메라 값으로 되돌린 뒤 다시 그린다.
        vp->ResetLocalTransform();
        vp->update();
    }
}

// § 화면설정 - 화면 모드(Solid/Solid-Edge/Wireframe). MainWindow가 STEP을 다시 불러와
// 새 MultiViewportPanel을 만들 때마다 마지막으로 고른 모드를 이 메서드로 재적용한다
// (그렇게 안 하면 재로드마다 기본값(SolidEdge)으로 되돌아가 사용자가 고른 설정이 사라진다).
void MultiViewportPanel::setRenderMode(RenderMode mode) {
    camera_.renderMode = mode;
    for (auto* vp : viewports_) {
        vp->update();
    }
}

// § 이미지 캡쳐 뷰어 설정 - 단면 보기 체크박스 토글 핸들러와 동일한 로직(켤 때 좌표를
// 현재 축의 bbox 중심으로 초기화)을 그대로 재사용 - 메인 툴바 체크박스와 RuleEditorDialog의
// 단축 체크박스 어느 쪽에서 켜도 똑같이 동작해야 하므로.
void MultiViewportPanel::setSectionEnabled(bool enabled) {
    camera_.sectionEnabled = enabled;
    if (enabled) {
        const auto box = unionBoxOfHandles();
        camera_.sectionCoord = static_cast<float>(
            (AxisMin(box, camera_.sectionAxis) + AxisMax(box, camera_.sectionAxis)) * 0.5);
    }
    for (auto* vp : viewports_) {
        vp->update();
    }
    syncSectionControlsFromCamera();
}

// § 화면설정 - 뷰포트 조작 모드. true(기본값)면 전체 뷰포트가 같은 camera_를 공유해서
// 동시에 회전/팬/줌 된다. false면 각 뷰포트가 자기 localCamera_로 독립 조작된다
// (ModelViewport::ActiveTransform 참고). 켜는 순간에는 각 뷰포트의 로컬 카메라를 현재
// 공유 카메라 값으로 맞춰서 시작하게 하여, 전환 직후 갑자기 다른 각도로 튀지 않게 한다.
void MultiViewportPanel::setSyncedManipulation(bool synced) {
    independentMode_ = !synced;
    if (!synced) {
        for (auto* vp : viewports_) {
            vp->ResetLocalTransform();
        }
    }
}

void MultiViewportPanel::setPickModeActive(bool active) {
    pickModeActive_ = active;
}

} // namespace viewer
