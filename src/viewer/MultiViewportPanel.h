#pragma once

#include <QPixmap>
#include <QRect>
#include <QVector3D>
#include <QWidget>

#include <string>
#include <utility>
#include <vector>

#include "geometry/IGeometryAdapter.h"
#include "viewer/Camera.h"
#include "viewer/ModelViewport.h" // CaptureInfo가 ModelViewport 안에 중첩되어 있어 전체 정의 필요

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;

namespace viewer {

// 여러 인치의 ModelViewport를 그리드로 배치하고, 하나의 Camera를 공유시켜
// 동기 회전/확대/이동을 구현한다. 모델 로드 시 정렬 QC(§5)도 함께 수행한다.
class MultiViewportPanel : public QWidget {
    Q_OBJECT

public:
    // {표시용 라벨, 이미 LoadModel까지 끝난 handle} 목록 - 호출자(MainWindow)가 파일당
    // 딱 한 번만 로드해서 넘겨준다. 여러 인치를 한번에 불러올 때 같은 파일을 뷰어가
    // 다시 파싱하지 않도록 하기 위함 (사용자 피드백: 배치 업로드가 무거워질 것 같다).
    MultiViewportPanel(geometry::IGeometryAdapter* adapter,
                        const std::vector<std::pair<std::string, geometry::ModelHandle>>& models,
                        QWidget* parent = nullptr);
    ~MultiViewportPanel() override;

    // § 화면설정 - MainWindow가 메뉴 액션에서 호출. 이 패널은 STEP 재로드마다 새로
    // 만들어지므로, MainWindow는 마지막으로 고른 값을 기억해뒀다가 매번 재적용해야 한다.
    void setRenderMode(RenderMode mode);
    void setSyncedManipulation(bool synced);

    // § 3D 클릭 피킹 - RuleEditorDialog가 "포인트 지정" 버튼을 누르면 켜고, 결과를
    // 받거나 취소하면 끈다. 켜져 있는 동안 모든 뷰포트의 좌클릭이 회전 대신 피킹으로
    // 동작한다(ModelViewport::pickModeActive_ 참고).
    void setPickModeActive(bool active);

    // § 드래그 영역 다중 선택(2026-09-23) - "프리셋 형상을 드래그로도 선택하게 해달라"는
    // 요청. pickModeActive_와 함께 켜면(ShapePresetDialog 수동 선택 모드) 좌클릭 드래그가
    // "피킹 모드 중 뷰 회전"(기존 동작) 대신 사각형 다중 선택으로 바뀐다 - 끄면(예:
    // RuleEditorDialog의 일반 "지정..." 흐름) 기존처럼 드래그=회전이 유지된다.
    void setRegionSelectModeActive(bool active);

    // § 이미지 캡쳐 뷰어 설정(2026-09-10) - RuleEditorDialog가 측정 이미지 영역에서
    // 메인 툴바를 직접 건드리지 않고도 단면뷰 on/off만 빠르게 전환할 수 있게. 축/좌표 등
    // 세부 조정은 여전히 메인 툴바(단면 보기 컨트롤 바)에서 - 켜고 끄는 것만 빠른 길을
    // 만든 것. 체크박스 토글 핸들러와 동일한 로직(초기 좌표 세팅 포함)을 재사용한다.
    void setSectionEnabled(bool enabled);
    bool sectionEnabled() const { return camera_.sectionEnabled; }

    // § 뷰 프리셋(2026-09-11) - "표준 직교 뷰(XY/-XY/YZ/-YZ/XZ/-XZ) + ISO가 다 있어야
    // 한다"는 요청에 따라 6개 평면뷰 + ISO 전부 추가. I 단축키가 쓰던 resetToIsoView()도
    // 공개로 돌렸다. 이름은 "Top/Front" 같은 의미 라벨이 아니라 버튼에 찍힌 것과 똑같은
    // 평면 이름으로 지었다 - 이 앱의 월드축이 Y-up/XZ-바닥(resetToIsoView 근처 주석
    // 참고)이라 "Top"이 실제로 XY뷰가 아니라 XZ뷰라서, 의미 라벨을 쓰면 오히려
    // 헷갈린다. 각도는 ViewMatrix()의 실제 회전 합성(Rx(pitch)*Ry(yaw))을 역산해서
    // 구했다: worldDir = (cos(pitch)sin(yaw), -sin(pitch), -cos(pitch)cos(yaw)).
    //   XY  : yaw=0,   pitch=0   -> worldDir=(0,0,-1)
    //   -XY : yaw=180, pitch=0   -> worldDir=(0,0,1)
    //   YZ  : yaw=90,  pitch=0   -> worldDir=(1,0,0)
    //   -YZ : yaw=-90, pitch=0   -> worldDir=(-1,0,0)
    //   XZ  : yaw=0,   pitch=90  -> worldDir=(0,-1,0) (pitch=±90에서는 yaw가 gimbal-lock)
    //   -XZ : yaw=0,   pitch=-90 -> worldDir=(0,1,0)
    // -XZ만 예외적으로 pitch<0을 쓴다 - resetToIsoView() 주석의 "pitch는 반드시 양수"는
    // 그 특정 버그(ISO에서 의도와 반대로 뒤집힘)에 대한 규칙이었지, 모든 음수 pitch가
    // 깨진다는 뜻은 아니다. -XZ(아래에서 위로 보는 뷰)는 애초에 "카메라가 원점보다
    // 아래에 위치"하는 게 맞는 그림이라 음수 pitch가 정확한 값이다 - pitch=+90에서는
    // gimbal-lock으로 worldDir이 yaw와 무관하게 (0,-1,0)만 나와서 이 방향은 구조적으로
    // pitch<0 없이는 만들 수 없다.
    void resetToIsoView(); // I 단축키 - 등각(45°, 35.264°)
    void snapToXYView();
    void snapToNegXYView();
    void snapToYZView();
    void snapToNegYZView();
    void snapToXZView();
    void snapToNegXZView();
    // § -ISO / TRIMETRIC(2026-11-04) - 요청으로 추가.
    //   -ISO     : ISO와 정확히 반대 방향(-XY가 XY의 반대인 것과 같은 규칙) - yaw=-135, pitch=-35.264,
    //              worldDir=(-1,1,1)/√3. 반대쪽 아래 모서리에서 올려다보는 그림이라 -XZ처럼 pitch<0이 맞다.
    //   TRIMETRIC: 세 축이 서로 다르게 줄어 보이는 각도(yaw=30, pitch=20) - ISO보다 정면에 가깝고
    //              덜 내려다봐서 앞면 형상이 잘 보인다.
    void resetToNegIsoView();
    void resetToTrimetricView();
    // § F4(2026-11-04) - 지금 보는 방향과 가장 가까운 직교 뷰(XY/-XY/YZ/-YZ/XZ/-XZ)로 맞춘다.
    void snapToNearestView();
    // § F1/F2/F3(2026-11-04) - 왼쪽 드래그 동작(이동/확대축소/회전)을 바꾼다(Camera.h MouseDragMode).
    void setMouseDragMode(MouseDragMode mode);

    // § 이미지 캡쳐 뷰어 설정 - "뷰어 전체 캡쳐라 배경이 너무 많다"는 피드백에 따라,
    // 툴바/라벨을 뺀 "현재 마우스가 올라가 있는(또는 마지막으로 올라갔던) 뷰포트"만
    // 잘라 찍는다. hoveredViewport_가 비어있으면(모델이 아직 없으면) 빈 픽스맵.
    // non-const인 이유: ModelViewport::grabViewportPixmap()이 grabFramebuffer()를 써서
    // non-const다(§ 캡쳐 화질 수정 - 검은 화면 버그, grabFramebuffer가 grab()보다 안전).
    QPixmap grabActiveViewport();

    // § 캡쳐된 이미지 위 클릭 피킹 - grabActiveViewport()와 "같은 순간"에 호출해서 그
    // 캡쳐가 어떤 카메라/크기/모델 기준이었는지 같이 저장해둬야 한다(RuleEditorDialog가
    // 나중에 이미지 클릭을 광선으로 역산할 때 씀).
    ModelViewport::CaptureInfo activeViewportCaptureInfo() const {
        return hoveredViewport_ ? hoveredViewport_->captureInfo() : ModelViewport::CaptureInfo{};
    }

    // § 캡쳐 영역 드래그 지정 - RuleEditorDialog가 "드래그로 캡쳐 영역 지정" 버튼으로
    // 켜고, 사용자가 뷰포트 위에서 사각형을 드래그해서 놓으면 captureRegionGrabbed가
    // 나간다(setPickModeActive와 같은 패턴 - 각 ModelViewport에 포인터로 공유).
    void setCaptureRegionModeActive(bool active);

    // § 뷰어 카메라 잠금(2026-09-11) - "캡쳐 전후 카메라 on/off 기능이 있으면 좋겠다.
    // 움직이고 싶을 때 on, 고정하고 싶을 때 off"라는 요청. 켜져 있는 동안 모든 뷰포트에서
    // 좌클릭 회전/우클릭 팬/휠 줌이 무시된다(피킹은 그대로 동작 - ModelViewport 참고).
    void setCameraLocked(bool locked);
    bool cameraLocked() const { return cameraLocked_; }

    // § NX 스타일 포인트 스냅(2026-09-11) - RuleEditorDialog가 지금까지 찍은 포인트들의
    // 월드 좌표를 넘기면 로드된 모든 뷰포트(=모든 인치, §5 공통 월드좌표계 정렬 전제)에
    // 밝은 주황 점으로 표시한다.
    void SetMarkerPositions(const std::vector<QVector3D>& worldPositions);
    // § 형상 프리셋 캡처 하이라이트 - ModelViewport::SetCapturedPatchHighlight 참고
    // (ShapePresetDialog가 형상을 캡처하면 고정 하이라이트로 계속 보여준다).
    void SetCapturedPatchHighlight(const std::vector<geometry::Vec3>& worldTriangles);
    // § CTQ 치수선 - 포인트 2개를 잇는 선을 모든 뷰포트에 표시한다.
    void SetDimensionLine(const std::vector<QVector3D>& worldPoints);
    // § 치수선 중앙 라벨 - Point(부위) 이니셜을 노란 박스로 치수선 중점에 표시한다.
    void SetDimensionLabel(const QString& text);
    // § 도면 설계법 가이드선 - 실제 측정 지점과 치수선(도면 여백 오프셋 위치)을 잇는
    // 얇은 보조선. ModelViewport::SetDimensionExtensionLines 참고.
    void SetDimensionExtensionLines(
        const std::vector<QVector3D>& fromPoints, const std::vector<QVector3D>& toPoints);
    // § 치수선 드래그(2026-09-13) - "드래그로 위치 조정 가능하게 해달라"는 요청.
    // ModelViewport::SetDimensionOffsetAxis 참고 - 모든 뷰포트에 같은 오프셋 축을 건다.
    void SetDimensionOffsetAxis(const QVector3D& axisDirWorld);

    // § 카메라 회전 시 치수선 축 자동 재선택(2026-09-13) - RuleEditorDialog가 현재 카메라
    // 각도를 알아야 "화면에 더 잘 보이는 축"을 다시 고를 수 있다. 읽기 전용으로만 노출.
    const Camera& sharedCamera() const { return camera_; }

signals:
    // 어느 뷰포트에서 찍었든 여기로 모여서 나간다 - RuleEditorDialog는 패널 하나에만
    // 연결하면 되고, 개별 ModelViewport를 알 필요가 없다.
    void facePicked(geometry::ModelHandle handle, QVector3D rayOrigin, QVector3D rayDir, bool shiftHeld);
    // § 드래그 영역 다중 선택 - setRegionSelectModeActive(true) 중 드래그로 사각형을
    // 그리고 놓으면(어느 뷰포트에서든) 여기로 모여서 나간다 - ModelViewport::facesInRegionPicked 참고.
    void facesInRegionPicked(geometry::ModelHandle handle, std::vector<int> faceIndices, bool shiftHeld);
    // 캡쳐 영역 드래그가 끝났을 때(어느 뷰포트에서든) 여기로 모여서 나간다 - 픽스맵 +
    // 그 순간의 CaptureInfo(이미지 위 클릭 피킹용).
    void captureRegionGrabbed(QPixmap pixmap, ModelViewport::CaptureInfo info);
    // § 카메라 회전 시 치수선 축 자동 재선택 - 어느 뷰포트에서 회전/팬/줌이 일어났든
    // 하나로 모아서 내보낸다(ModelViewport::cameraChanged 참고).
    void cameraChanged();
    // § 치수선 드래그 - ModelViewport::dimensionOffsetDragged 참고.
    void dimensionOffsetDragged(float deltaWorldAlongAxis);

private:
    // F(FIT TO VIEW) 재계산에 필요 - 로드된 모든 모델의 핸들을 들고 있어야
    // 나중에라도(최초 로드 이후) bounding box 합집합을 다시 구할 수 있다.
    void fitToView();

    // 단면 뷰(H) 컨트롤 바 - 사용자 요청: 슬라이드바 + X/Y/Z 축 선택 버튼 + mm 숫자 입력.
    // 키보드 단축키(H/X/Y/Z/+/-, ModelViewport)와 같은 Camera를 공유하므로, 어느 쪽으로
    // 조작하든 cameraChanged 시그널을 타고 syncSectionControlsFromCamera()가 나머지를 맞춘다.
    QWidget* buildSectionControlBar();
    // § 뷰 프리셋 - 단면 컨트롤 바와 분리된 행. XY/-XY/YZ/-YZ/XZ/-XZ/ISO 7개 버튼.
    QWidget* buildViewPresetBar();
    void syncSectionControlsFromCamera();
    // 축이 바뀔 때(버튼/키보드 공통) 슬라이더/스핀박스의 min/max를 그 축의 bounding box
    // 범위(여유 포함)로 다시 잡는다. 값 자체는 안 건드린다 - 호출부가 필요하면 따로 설정.
    void updateSectionRangeForAxis(int axis);
    // handles_ 전체의 bounding box 합집합 - fitToView()와 단면 컨트롤 범위 계산이 공유.
    geometry::BoundingBox unionBoxOfHandles() const;
    // H/X/Y/Z/+/- 단축키를 등록 - QShortcut(WidgetWithChildrenShortcut)이라 컨트롤 바
    // 위젯이든 뷰포트든 이 패널 안 어디에 포커스가 있어도 먹는다(사용자 리포트: 포커스
    // 기반 keyPressEvent 방식일 때 F/H/I가 전혀 안 먹었던 문제의 근본 수정).
    void setupShortcuts();

    geometry::IGeometryAdapter* adapter_ = nullptr;
    std::vector<geometry::ModelHandle> handles_;
    Camera camera_;
    std::vector<ModelViewport*> viewports_;
    // H/X/Y/Z(레이캐스팅 필요)를 어느 뷰포트에 적용할지 - 가장 최근에 마우스가 들어온
    // 뷰포트. F/I는 전체 인치 공통이라 이거 없이 바로 처리한다.
    ModelViewport* hoveredViewport_ = nullptr;
    // § 화면설정 - 뷰포트 조작 모드. false(기본)면 전체 뷰포트가 camera_를 공유해서 동시
    // 조작되고, true면 각 ModelViewport가 자기 localCamera_로 독립 조작된다. 포인터로
    // 각 ModelViewport에 넘겨서 공유한다(ModelViewport::SetIndependentModePtr 참고).
    bool independentMode_ = false;
    // § 3D 클릭 피킹 - setPickModeActive()로 켜고 끄며, 각 ModelViewport에 포인터로
    // 공유한다(independentMode_와 같은 패턴).
    bool pickModeActive_ = false;
    // § 캡쳐 영역 드래그 지정 - setCaptureRegionModeActive()로 켜고 끄며, 각
    // ModelViewport에 포인터로 공유한다(pickModeActive_와 같은 패턴).
    bool captureRegionModeActive_ = false;
    // § 드래그 영역 다중 선택 - setRegionSelectModeActive()로 켜고 끄며, 각
    // ModelViewport에 포인터로 공유한다(pickModeActive_와 같은 패턴).
    bool regionSelectModeActive_ = false;
    // § 뷰어 카메라 잠금 - setCameraLocked()로 켜고 끄며, 각 ModelViewport에 포인터로
    // 공유한다(pickModeActive_와 같은 패턴).
    bool cameraLocked_ = false;
    // § F1/F2/F3 조작 모드 - "모든 창에서 유지되게" 요청(2026-11-04)으로 앱 전체가 하나를 공유한다
    // (메인 화면/규칙 관리/형상 프리셋 뷰어, STEP 다시 불러오기 후에도 그대로). 각 ModelViewport가 이
    // 값을 포인터로 읽고, 바뀌면 살아 있는 모든 패널의 안내 문구를 다시 쓴다(s_livePanels).
    inline static MouseDragMode s_mouseDragMode = MouseDragMode::Rotate;
    inline static std::vector<MultiViewportPanel*> s_livePanels;
    void refreshMouseDragModeLabel();
    QLabel* mouseDragModeLabel_ = nullptr; // 뷰 프리셋 줄 오른쪽 - 지금 모드와 단축키 안내

    QCheckBox* sectionEnableCheck_ = nullptr;
    QPushButton* axisButtons_[3] = {nullptr, nullptr, nullptr};
    QPushButton* sectionFlipButton_ = nullptr;
    QSlider* sectionSlider_ = nullptr;
    QDoubleSpinBox* sectionValueSpin_ = nullptr;
    bool syncingSectionControls_ = false; // 위젯<->camera_ 역방향 시그널 루프 방지
};

} // namespace viewer
