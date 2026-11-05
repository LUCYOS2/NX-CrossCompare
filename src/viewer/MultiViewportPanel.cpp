#include "viewer/MultiViewportPanel.h"
#include "viewer/ModelViewport.h"

#include "geometry/AlignmentCheck.h"
#include "viewer/DiagnosticsLog.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMatrix4x4>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QString>
#include <QTextEdit>
#include <QTimer>
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
    outerLayout->addWidget(buildViewPresetBar());

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
        viewport->SetCaptureRegionModePtr(&captureRegionModeActive_);
        viewport->SetRegionSelectModePtr(&regionSelectModeActive_);
        viewport->SetCameraLockedPtr(&cameraLocked_);
        viewport->SetMouseDragModePtr(&s_mouseDragMode);
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
            // § 카메라 회전 시 치수선 축 자동 재선택 - RuleEditorDialog가 이 신호를 받아
            // "화면에 더 잘 보이는 축"으로 오프셋을 다시 고를 수 있게 바깥으로도 내보낸다.
            emit cameraChanged();
        });
        connect(viewport, &ModelViewport::hoverEntered, this, [this, viewport]() {
            hoveredViewport_ = viewport;
        });
        // § 3D 클릭 피킹 - 어느 뷰포트에서 찍었든 패널의 facePicked 하나로 모아서 내보낸다.
        connect(viewport, &ModelViewport::facePicked, this, &MultiViewportPanel::facePicked);
        // § 드래그 영역 다중 선택 - 마찬가지로 패널의 facesInRegionPicked 하나로 모은다.
        connect(viewport, &ModelViewport::facesInRegionPicked, this, &MultiViewportPanel::facesInRegionPicked);
        // § 캡쳐 영역 드래그 지정 - 마찬가지로 패널의 captureRegionGrabbed 하나로 모은다.
        connect(viewport, &ModelViewport::captureRegionGrabbed, this, &MultiViewportPanel::captureRegionGrabbed);
        // § 치수선 드래그 - 마찬가지로 패널의 dimensionOffsetDragged 하나로 모은다.
        connect(viewport, &ModelViewport::dimensionOffsetDragged, this,
                &MultiViewportPanel::dimensionOffsetDragged);

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
    s_livePanels.push_back(this);
    connect(diag::Hub::instance(), &diag::Hub::glInfoReady, this, &MultiViewportPanel::refreshMouseDragModeLabel);
}

MultiViewportPanel::~MultiViewportPanel() {
    s_livePanels.erase(std::remove(s_livePanels.begin(), s_livePanels.end(), this), s_livePanels.end());
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
    auto addShortcut = [this, shortcutScope](QKeySequence keySequence, KeyAction action, const QString& keyName) {
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
        // § "F가 안 먹는다" 리포트(2026-09-13) - WidgetWithChildrenShortcut은 "shortcutScope
        // (MainWindow) 서브트리 안의 '어떤 위젯이 실제 키보드 포커스를 갖고 있을 때'"만
        // 발동한다. 규칙 관리 다이얼로그(별도 톱레벨 창)를 열었다 닫는 등으로 MainWindow
        // 쪽에 포커스를 가진 위젯이 하나도 없는 상태가 되면(Qt가 포커스를 아무 데도 다시
        // 돌려주지 않는 경우가 흔함) 이 조건을 만족하는 위젯이 없어 F/H/I가 전혀 안 먹는다.
        // WindowShortcut은 "이 위젯이 속한 톱레벨 창이 지금 활성 창이면" 발동하므로 포커스가
        // MainWindow 안 어디에도 없어도 문제없다 - 그러면서도 RuleEditorDialog는 별도
        // 톱레벨 창이라 그게 활성 창일 땐 여전히 안 먹으므로(텍스트 입력 방해 없음) 위
        // 주석의 원래 목적은 그대로 유지된다.
        shortcut->setContext(Qt::WindowShortcut);
        connect(shortcut, &QShortcut::activated, this,
                [this, action, keyName]() { runKeyAction(action, keyName, "단축키"); });
        // § 단축키 보강(2026-11-05) - 같은 키가 어쩌다 두 번 걸려 Qt가 "애매함"으로 판정해도 버리지 않고 실행한다
        // (예전엔 이 경우 activated가 아예 안 터져 키가 통째로 먹통이었다).
        connect(shortcut, &QShortcut::activatedAmbiguously, this,
                [this, action, keyName]() { runKeyAction(action, keyName, "단축키(중복)"); });
        return shortcut;
    };

    for (const KeyBinding& binding : KeyBindings()) {
        addShortcut(QKeySequence(binding.qtKey), binding.action, binding.keyName);
    }
    EnsureKeyFallbackFilter();
}

// § 단축키 보강(2026-11-05) - "단축키가 하나도 안 먹는다"(회사PC) 리포트. 개발PC에서는 실제 키 입력을 흉내 낸
// 점검(diag_viewer_input)에서 메인 화면/형상 프리셋 창 모두 정상이라, QShortcut이 키를 못 알아보는 환경(원격
// 접속 프로그램이 글자를 문자 코드로만 보내거나, 한글 입력 상태 등)을 대비해 같은 동작을 QShortcut과 보조 처리
// (EnsureKeyFallbackFilter) 둘 다에서 부른다. 실행되면 안내 줄에 "방금 누른 키"를 잠깐 띄워 키가 먹었는지 보이게 한다.
const std::vector<MultiViewportPanel::KeyBinding>& MultiViewportPanel::KeyBindings() {
    // nativeKey = Windows 가상 키 코드, altText = 같은 자리의 한글 자모(두벌식) - 둘 다 보조 처리에서만 쓴다.
    static const std::vector<KeyBinding> bindings = {
        {Qt::Key_F, 0x46, "f", QString::fromUtf8("ㄹ"), KeyAction::Fit, "F"},
        {Qt::Key_I, 0x49, "i", QString::fromUtf8("ㅑ"), KeyAction::Iso, "I"},
        // § F1/F2/F3/F4(2026-11-04) - 왼쪽 드래그 조작 모드 전환 + 가장 가까운 직교 뷰로 정렬.
        {Qt::Key_F1, 0x70, "", "", KeyAction::ModePan, "F1"},
        {Qt::Key_F2, 0x71, "", "", KeyAction::ModeZoom, "F2"},
        {Qt::Key_F3, 0x72, "", "", KeyAction::ModeRotate, "F3"},
        {Qt::Key_F4, 0x73, "", "", KeyAction::SnapNearest, "F4"},
        {Qt::Key_H, 0x48, "h", QString::fromUtf8("ㅗ"), KeyAction::ToggleSection, "H"},
        {Qt::Key_X, 0x58, "x", QString::fromUtf8("ㅌ"), KeyAction::AxisX, "X"},
        {Qt::Key_Y, 0x59, "y", QString::fromUtf8("ㅛ"), KeyAction::AxisY, "Y"},
        {Qt::Key_Z, 0x5A, "z", QString::fromUtf8("ㅋ"), KeyAction::AxisZ, "Z"},
        // "+"/"-"는 물리 키 하나에 Shift로 두 문자를 겸하는 배열이 많아 Plus/Equal을 같이 등록.
        // 큰 폭 이동은 +/- 대신 Page Up/Down으로 분리해서 Shift+= 입력과 안 헷갈리게 했다.
        {Qt::Key_Plus, 0x6B, "+", "", KeyAction::NudgePlus, "+"},
        {Qt::Key_Equal, 0xBB, "=", "", KeyAction::NudgePlus, "+"},
        {Qt::Key_Minus, 0xBD, "-", "", KeyAction::NudgeMinus, "-"},
        {Qt::Key_PageUp, 0x21, "", "", KeyAction::NudgePlusBig, "PageUp"},
        {Qt::Key_PageDown, 0x22, "", "", KeyAction::NudgeMinusBig, "PageDown"},
    };
    return bindings;
}

MultiViewportPanel* MultiViewportPanel::PanelForWindow(const QWidget* window) {
    for (MultiViewportPanel* panel : s_livePanels) {
        if (panel->isVisible() && panel->window() == window) {
            return panel;
        }
    }
    return nullptr;
}

namespace {

// 글자를 입력하는 칸에 포커스가 있으면 보조 처리는 손대지 않는다(이름 칸에 h/f를 칠 수 있어야 함).
bool IsTextInputWidget(const QWidget* widget) {
    if (!widget) {
        return false;
    }
    if (const auto* edit = qobject_cast<const QLineEdit*>(widget)) {
        return !edit->isReadOnly();
    }
    if (const auto* edit = qobject_cast<const QTextEdit*>(widget)) {
        return !edit->isReadOnly();
    }
    if (const auto* edit = qobject_cast<const QPlainTextEdit*>(widget)) {
        return !edit->isReadOnly();
    }
    if (qobject_cast<const QAbstractSpinBox*>(widget)) {
        return true;
    }
    if (const auto* combo = qobject_cast<const QComboBox*>(widget)) {
        return combo->isEditable();
    }
    return false;
}

// QShortcut이 못 알아본 키(KeyPress가 그대로 위젯까지 내려온 경우)를 받아서 같은 동작을 실행한다. QShortcut이
// 정상 동작하면 KeyPress가 위젯까지 오지 않으므로 두 번 실행되지 않는다. Qt 키 코드 -> Windows 가상 키 코드 ->
// 입력된 글자(영문/한글 자모) 순서로 맞춰 본다.
class ViewerKeyFallbackFilter : public QObject {
public:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() != QEvent::KeyPress || !watched->isWidgetType()) {
            return false;
        }
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
            return false;
        }
        if (IsTextInputWidget(QApplication::focusWidget())) {
            return false;
        }
        MultiViewportPanel* panel = MultiViewportPanel::PanelForWindow(static_cast<QWidget*>(watched)->window());
        if (!panel) {
            return false;
        }
        const QString text = keyEvent->text().toLower();
        for (const auto& binding : MultiViewportPanel::KeyBindings()) {
            const bool matches = keyEvent->key() == binding.qtKey ||
#ifdef _WIN32
                                 (keyEvent->nativeVirtualKey() != 0 && keyEvent->nativeVirtualKey() == binding.nativeKey) ||
#endif
                                 (!text.isEmpty() && (text == binding.text || text == binding.altText));
            if (matches) {
                panel->runKeyAction(binding.action, binding.keyName, "보조 처리");
                return true;
            }
        }
        return false;
    }
};

} // namespace

void MultiViewportPanel::EnsureKeyFallbackFilter() {
    static ViewerKeyFallbackFilter* filter = nullptr;
    if (!filter && qApp) {
        filter = new ViewerKeyFallbackFilter;
        filter->setParent(qApp);
        qApp->installEventFilter(filter);
    }
}

void MultiViewportPanel::runKeyAction(KeyAction action, const QString& keyName, const QString& via) {
    QString what;
    auto nudge = [this, &what](float deltaMm) {
        if (hoveredViewport_ && camera_.sectionEnabled) {
            hoveredViewport_->nudgeSection(deltaMm);
            what = QString("단면 %1%2mm").arg(deltaMm > 0 ? "+" : "").arg(deltaMm);
        } else {
            what = "단면이 꺼져 있어 무시(H로 켜기)";
        }
    };
    auto axis = [this, &what](int index) {
        if (hoveredViewport_ && camera_.sectionEnabled) {
            hoveredViewport_->switchSectionAxis(index);
            what = QString("단면 축 %1").arg(QChar('X' + index));
        } else {
            what = "단면이 꺼져 있어 무시(H로 켜기)";
        }
    };
    switch (action) {
        case KeyAction::Fit: fitToView(); what = "화면 맞춤"; break;
        case KeyAction::Iso: resetToIsoView(); what = "ISO 뷰"; break;
        case KeyAction::ModePan: setMouseDragMode(MouseDragMode::Pan); what = "왼쪽 드래그 = 이동"; break;
        case KeyAction::ModeZoom: setMouseDragMode(MouseDragMode::Zoom); what = "왼쪽 드래그 = 확대/축소"; break;
        case KeyAction::ModeRotate: setMouseDragMode(MouseDragMode::Rotate); what = "왼쪽 드래그 = 회전"; break;
        case KeyAction::SnapNearest: snapToNearestView(); what = "가까운 뷰 정렬"; break;
        case KeyAction::ToggleSection:
            if (hoveredViewport_) {
                hoveredViewport_->toggleSection();
            }
            what = camera_.sectionEnabled ? "단면 켬" : "단면 끔";
            break;
        case KeyAction::AxisX: axis(0); break;
        case KeyAction::AxisY: axis(1); break;
        case KeyAction::AxisZ: axis(2); break;
        case KeyAction::NudgePlus: nudge(1.0f); break;
        case KeyAction::NudgeMinus: nudge(-1.0f); break;
        case KeyAction::NudgePlusBig: nudge(10.0f); break;
        case KeyAction::NudgeMinusBig: nudge(-10.0f); break;
    }
    lastKeyFeedback_ = QString("%1 → %2").arg(keyName, what);
    if (!keyFeedbackTimer_) {
        keyFeedbackTimer_ = new QTimer(this);
        keyFeedbackTimer_->setSingleShot(true);
        keyFeedbackTimer_->setInterval(2500);
        connect(keyFeedbackTimer_, &QTimer::timeout, this, [this]() {
            lastKeyFeedback_.clear();
            refreshMouseDragModeLabel();
        });
    }
    keyFeedbackTimer_->start();
    refreshMouseDragModeLabel();
    diag::Hub::instance()->NotifyShortcut(keyName, what, via);
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

    // § 조작 안내 문구(2026-09-18) - "가운데 클릭으로 단면 위치를 지정한다"는 게 발견하기
    // 어렵다는 피드백 - 작은 회색 라벨로 상시 노출.
    auto* sectionHintLabel = new QLabel("휠 버튼(가운데) 클릭 = 도면에서 단면 위치 지정", bar);
    QFont hintFont = sectionHintLabel->font();
    hintFont.setPointSizeF(hintFont.pointSizeF() * 0.85);
    sectionHintLabel->setFont(hintFont);
    sectionHintLabel->setStyleSheet("color: gray;");
    layout->addSpacing(12);
    layout->addWidget(sectionHintLabel);

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

// § 뷰 프리셋(2026-09-11) - 단면 컨트롤 바와 별개 행으로 둔다. 6개 평면뷰 + ISO를 전부
// 같은 줄에 욱여넣으면 단면 컨트롤(축/슬라이더 등)과 합쳐 화면 폭을 넘어갈 수 있어서
// 분리했다. 토글 상태를 유지할 필요가 없는 1회성 액션이라(단면 축 버튼과 달리) 멤버로
// 안 갖고 로컬 변수로 충분하다.
QWidget* MultiViewportPanel::buildViewPresetBar() {
    auto* bar = new QWidget(this);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 4, 8, 4);

    layout->addWidget(new QLabel("뷰:", bar));
    struct PresetButton {
        const char* label;
        void (MultiViewportPanel::*slot)();
    };
    const PresetButton presets[] = {
        {"XY", &MultiViewportPanel::snapToXYView},     {"-XY", &MultiViewportPanel::snapToNegXYView},
        {"YZ", &MultiViewportPanel::snapToYZView},     {"-YZ", &MultiViewportPanel::snapToNegYZView},
        {"XZ", &MultiViewportPanel::snapToXZView},     {"-XZ", &MultiViewportPanel::snapToNegXZView},
        {"ISO", &MultiViewportPanel::resetToIsoView},  {"-ISO", &MultiViewportPanel::resetToNegIsoView},
        {"TRIMETRIC", &MultiViewportPanel::resetToTrimetricView},
    };
    for (const auto& preset : presets) {
        auto* button = new QPushButton(preset.label, bar);
        connect(button, &QPushButton::clicked, this, preset.slot);
        layout->addWidget(button);
    }
    layout->addStretch(1);

    // § F1/F2/F3/F4 안내 - 키로 모드를 바꾸면 눈에 안 보여서, 지금 왼쪽 드래그가 무엇인지 항상 보여준다.
    mouseDragModeLabel_ = new QLabel(bar);
    layout->addWidget(mouseDragModeLabel_);
    refreshMouseDragModeLabel();

    return bar;
}

void MultiViewportPanel::setMouseDragMode(MouseDragMode mode) {
    s_mouseDragMode = mode;
    for (auto* panel : s_livePanels) {
        panel->refreshMouseDragModeLabel();
    }
}

void MultiViewportPanel::refreshMouseDragModeLabel() {
    if (!mouseDragModeLabel_) {
        return;
    }
    const MouseDragMode mode = s_mouseDragMode;
    const char* name = mode == MouseDragMode::Pan ? "이동" : (mode == MouseDragMode::Zoom ? "확대/축소" : "회전");
    QString text = QString("왼쪽 드래그: <b>%1</b>  |  F1 이동 · F2 확대/축소 · F3 회전 · F4 가까운 뷰 정렬").arg(name);
    // § 단축키 보강(2026-11-05) - 키가 실제로 먹었는지 바로 보이게 "방금 누른 키"를 2.5초 동안 앞에 띄운다.
    if (!lastKeyFeedback_.isEmpty()) {
        text = QString("<span style='color:#1a7f37'>방금 누른 키: <b>%1</b></span>  |  ").arg(lastKeyFeedback_.toHtmlEscaped()) +
               text;
    }
    // § 앱 진단 기록 - 그래픽 가속 없이 CPU로 그리는 중이면(원격 접속 등) 느린 이유를 화면에 바로 알린다.
    if (diag::CurrentGlInfo().software) {
        text += "  |  <span style='color:#c0392b'><b>그래픽 가속 없음</b>(소프트웨어 렌더링) - 큰 도면은 느릴 수 있음</span>";
    }
    mouseDragModeLabel_->setText(text);
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

// § 뷰 프리셋(2026-09-11) - 각도 유도는 헤더의 클래스 선언부 주석 참고. 6개 다 같은
// 패턴(회전만 바꾸고 fitToView()로 다시 맞춤)이라 각각은 두 줄뿐이다.
void MultiViewportPanel::snapToXYView() {
    camera_.yawDeg = 0.0f;
    camera_.pitchDeg = 0.0f;
    fitToView();
}

void MultiViewportPanel::snapToNegXYView() {
    camera_.yawDeg = 180.0f;
    camera_.pitchDeg = 0.0f;
    fitToView();
}

void MultiViewportPanel::snapToYZView() {
    camera_.yawDeg = 90.0f;
    camera_.pitchDeg = 0.0f;
    fitToView();
}

void MultiViewportPanel::snapToNegYZView() {
    camera_.yawDeg = -90.0f;
    camera_.pitchDeg = 0.0f;
    fitToView();
}

void MultiViewportPanel::snapToXZView() {
    camera_.yawDeg = 0.0f;
    camera_.pitchDeg = 90.0f;
    fitToView();
}

void MultiViewportPanel::snapToNegXZView() {
    // 의도적 예외 - 헤더 주석 참고("pitch는 반드시 양수" 규칙은 이 뷰엔 적용 안 됨).
    camera_.yawDeg = 0.0f;
    camera_.pitchDeg = -90.0f;
    fitToView();
}

void MultiViewportPanel::resetToNegIsoView() {
    // ISO(yaw=45, pitch=35.264)의 정확한 반대 방향 - 헤더 주석 참고(-XZ와 같은 이유로 pitch<0).
    camera_.yawDeg = -135.0f;
    camera_.pitchDeg = -35.264f;
    fitToView();
}

void MultiViewportPanel::resetToTrimetricView() {
    camera_.yawDeg = 30.0f;
    camera_.pitchDeg = 20.0f;
    fitToView();
}

// § F4 - 지금 보는 방향(worldDir, 헤더 주석의 공식)과 6개 직교 뷰 방향의 내적이 가장 큰 쪽으로 맞춘다.
// 마우스로 계속 돌리면 pitch가 ±90을 넘어갈 수 있지만, 공식이 삼각함수라 그대로 맞는 방향이 나온다.
// 독립 조작 모드에서는 마우스가 올라가 있는 뷰포트의 각도를 기준으로 판단한다(다른 프리셋처럼
// 결과는 전체 공통 - fitToView가 각 뷰포트의 로컬 각도를 덮어쓴다).
void MultiViewportPanel::snapToNearestView() {
    const Camera& reference =
        (independentMode_ && hoveredViewport_) ? hoveredViewport_->captureInfo().camera : camera_;
    constexpr double kDegToRad = 0.017453292519943295;
    const double yaw = reference.yawDeg * kDegToRad;
    const double pitch = reference.pitchDeg * kDegToRad;
    const QVector3D viewDir(static_cast<float>(std::cos(pitch) * std::sin(yaw)), static_cast<float>(-std::sin(pitch)),
                            static_cast<float>(-std::cos(pitch) * std::cos(yaw)));
    struct Candidate {
        QVector3D dir;
        void (MultiViewportPanel::*snap)();
    };
    const Candidate candidates[] = {
        {{0, 0, -1}, &MultiViewportPanel::snapToXYView}, {{0, 0, 1}, &MultiViewportPanel::snapToNegXYView},
        {{1, 0, 0}, &MultiViewportPanel::snapToYZView},  {{-1, 0, 0}, &MultiViewportPanel::snapToNegYZView},
        {{0, -1, 0}, &MultiViewportPanel::snapToXZView}, {{0, 1, 0}, &MultiViewportPanel::snapToNegXZView},
    };
    const Candidate* best = &candidates[0];
    for (const auto& candidate : candidates) {
        if (QVector3D::dotProduct(candidate.dir, viewDir) > QVector3D::dotProduct(best->dir, viewDir)) {
            best = &candidate;
        }
    }
    (this->*(best->snap))();
}

// 단축키 F 및 최초 로드 시 공용 - 로드된 모든 인치의 bounding box 합집합 "중심"에 카메라를
// 맞춘다. 예전엔 월드 원점 기준 거리만 조절하고 팬은 항상 0으로 리셋했는데, §5 컨벤션대로
// 원점 정렬된 모델에서는 문제없지만 ROI로 잘라낸 부분 형상처럼 원점에서 멀리 떨어진 모델은
// 화면 프레임 안에 들어와도 한쪽 구석에 점처럼 작게 찍혀 사실상 안 보이는 문제가 있었다
// (사용자 리포트: STEP을 불러왔는데 뷰포트에 아무것도 안 보임). 이제 bounding box 중심을
// 구해서 현재 회전(yaw/pitch) 기준으로 화면 정중앙에 오도록 팬을 계산한다.
//
// § 화면 채움 비율 개선(2026-09-18) - 예전엔 bbox의 "3D 대각선" 절반(halfDiagonal)을
// 기준으로 거리를 잡았는데, 이건 어느 각도에서 봐도 안 잘리는 최악의 경우(정확히
// 대각선 방향으로 보는 경우) 기준이라 FRONT/TOP/ISO처럼 실제로는 대각선보다 훨씬 작게
// 보이는 각도에서도 항상 여유가 넘쳐서 모델이 화면에 작게 찍히는 문제가 있었다(사용자
// 리포트: "F 눌러도 너무 멀다/작다"). 대신 bbox 8개 꼭짓점을 현재 회전으로 실제 투영해
// 화면상 보이는 가로/세로 절반 크기를 직접 구하고, 그 값에 맞춰 거리를 잡는다 - 지금
// 보고 있는 각도에서 실제로 필요한 만큼만 줌인된다.
void MultiViewportPanel::fitToView() {
    if (handles_.empty()) {
        return;
    }

    const geometry::BoundingBox unionBox = unionBoxOfHandles();
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

    // bbox 8개 꼭짓점을 회전시켜 화면(x,y) 평면상 center 기준 최대 편차를 구한다 -
    // 이게 "지금 각도에서 실제로 보이는" 절반 너비/높이다.
    const QVector3D corners[8] = {
        {static_cast<float>(unionBox.min.x), static_cast<float>(unionBox.min.y), static_cast<float>(unionBox.min.z)},
        {static_cast<float>(unionBox.max.x), static_cast<float>(unionBox.min.y), static_cast<float>(unionBox.min.z)},
        {static_cast<float>(unionBox.min.x), static_cast<float>(unionBox.max.y), static_cast<float>(unionBox.min.z)},
        {static_cast<float>(unionBox.max.x), static_cast<float>(unionBox.max.y), static_cast<float>(unionBox.min.z)},
        {static_cast<float>(unionBox.min.x), static_cast<float>(unionBox.min.y), static_cast<float>(unionBox.max.z)},
        {static_cast<float>(unionBox.max.x), static_cast<float>(unionBox.min.y), static_cast<float>(unionBox.max.z)},
        {static_cast<float>(unionBox.min.x), static_cast<float>(unionBox.max.y), static_cast<float>(unionBox.max.z)},
        {static_cast<float>(unionBox.max.x), static_cast<float>(unionBox.max.y), static_cast<float>(unionBox.max.z)},
    };
    float halfVisibleWidth = 0.0f;
    float halfVisibleHeight = 0.0f;
    for (const auto& corner : corners) {
        const QVector3D rotated = rotationOnly * corner;
        halfVisibleWidth = std::max(halfVisibleWidth, std::abs(rotated.x() - rotatedCenter.x()));
        halfVisibleHeight = std::max(halfVisibleHeight, std::abs(rotated.y() - rotatedCenter.y()));
    }
    if (halfVisibleWidth <= 0.0f && halfVisibleHeight <= 0.0f) {
        return;
    }

    // 뷰포트 종횡비(가로/세로) - 여러 뷰포트가 폭을 나눠 쓰므로 대표로 첫 번째 것을 쓴다
    // (레이아웃상 서로 거의 동일한 비율).
    float aspect = 1.0f;
    if (!viewports_.empty() && viewports_.front()->height() > 0) {
        aspect = static_cast<float>(viewports_.front()->width()) /
                 static_cast<float>(viewports_.front()->height());
    }
    constexpr float kHalfFovTan = 0.414f; // ModelViewport.cpp의 동일 상수와 맞춤(45도 수직 FOV 절반)
    constexpr float kMargin = 1.15f;
    const float distanceForHeight = (halfVisibleHeight * kMargin) / kHalfFovTan;
    const float distanceForWidth = (halfVisibleWidth * kMargin) / (kHalfFovTan * aspect);
    camera_.distance = std::max(distanceForHeight, distanceForWidth);

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

void MultiViewportPanel::setRegionSelectModeActive(bool active) {
    regionSelectModeActive_ = active;
}

QPixmap MultiViewportPanel::grabActiveViewport() {
    if (!hoveredViewport_) {
        return QPixmap();
    }
    return hoveredViewport_->grabViewportPixmap();
}

void MultiViewportPanel::setCaptureRegionModeActive(bool active) {
    captureRegionModeActive_ = active;
}

void MultiViewportPanel::setCameraLocked(bool locked) {
    cameraLocked_ = locked;
}

void MultiViewportPanel::SetMarkerPositions(const std::vector<QVector3D>& worldPositions) {
    for (auto* vp : viewports_) {
        vp->SetMarkerPositions(worldPositions);
    }
}

void MultiViewportPanel::SetCapturedPatchHighlight(const std::vector<geometry::Vec3>& worldTriangles) {
    for (auto* vp : viewports_) {
        vp->SetCapturedPatchHighlight(worldTriangles);
    }
}

void MultiViewportPanel::SetDimensionLabel(const QString& text) {
    for (auto* vp : viewports_) {
        vp->SetDimensionLabel(text);
    }
}

void MultiViewportPanel::SetDimensionExtensionLines(
    const std::vector<QVector3D>& fromPoints, const std::vector<QVector3D>& toPoints) {
    for (auto* vp : viewports_) {
        vp->SetDimensionExtensionLines(fromPoints, toPoints);
    }
}

void MultiViewportPanel::SetDimensionLine(const std::vector<QVector3D>& worldPoints) {
    for (auto* vp : viewports_) {
        vp->SetDimensionLine(worldPoints);
    }
}

void MultiViewportPanel::SetDimensionOffsetAxis(const QVector3D& axisDirWorld) {
    for (auto* vp : viewports_) {
        vp->SetDimensionOffsetAxis(axisDirWorld);
    }
}

} // namespace viewer
