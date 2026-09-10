#pragma once

#include <QVector3D>
#include <QWidget>

#include <string>
#include <utility>
#include <vector>

#include "geometry/IGeometryAdapter.h"
#include "viewer/Camera.h"

class QCheckBox;
class QDoubleSpinBox;
class QPushButton;
class QSlider;

namespace viewer {

class ModelViewport;

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

    // § 화면설정 - MainWindow가 메뉴 액션에서 호출. 이 패널은 STEP 재로드마다 새로
    // 만들어지므로, MainWindow는 마지막으로 고른 값을 기억해뒀다가 매번 재적용해야 한다.
    void setRenderMode(RenderMode mode);
    void setSyncedManipulation(bool synced);

    // § 3D 클릭 피킹 - RuleEditorDialog가 "포인트 지정" 버튼을 누르면 켜고, 결과를
    // 받거나 취소하면 끈다. 켜져 있는 동안 모든 뷰포트의 좌클릭이 회전 대신 피킹으로
    // 동작한다(ModelViewport::pickModeActive_ 참고).
    void setPickModeActive(bool active);

    // § 이미지 캡쳐 뷰어 설정(2026-09-10) - RuleEditorDialog가 측정 이미지 영역에서
    // 메인 툴바를 직접 건드리지 않고도 단면뷰 on/off만 빠르게 전환할 수 있게. 축/좌표 등
    // 세부 조정은 여전히 메인 툴바(단면 보기 컨트롤 바)에서 - 켜고 끄는 것만 빠른 길을
    // 만든 것. 체크박스 토글 핸들러와 동일한 로직(초기 좌표 세팅 포함)을 재사용한다.
    void setSectionEnabled(bool enabled);
    bool sectionEnabled() const { return camera_.sectionEnabled; }

signals:
    // 어느 뷰포트에서 찍었든 여기로 모여서 나간다 - RuleEditorDialog는 패널 하나에만
    // 연결하면 되고, 개별 ModelViewport를 알 필요가 없다.
    void facePicked(geometry::ModelHandle handle, QVector3D rayOrigin, QVector3D rayDir);

private:
    // F(FIT TO VIEW) 재계산에 필요 - 로드된 모든 모델의 핸들을 들고 있어야
    // 나중에라도(최초 로드 이후) bounding box 합집합을 다시 구할 수 있다.
    void fitToView();
    // I 단축키 - 표준 등각(isometric) 각도로 회전을 리셋하고 화면도 다시 맞춘다.
    void resetToIsoView();

    // 단면 뷰(H) 컨트롤 바 - 사용자 요청: 슬라이드바 + X/Y/Z 축 선택 버튼 + mm 숫자 입력.
    // 키보드 단축키(H/X/Y/Z/+/-, ModelViewport)와 같은 Camera를 공유하므로, 어느 쪽으로
    // 조작하든 cameraChanged 시그널을 타고 syncSectionControlsFromCamera()가 나머지를 맞춘다.
    QWidget* buildSectionControlBar();
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

    QCheckBox* sectionEnableCheck_ = nullptr;
    QPushButton* axisButtons_[3] = {nullptr, nullptr, nullptr};
    QPushButton* sectionFlipButton_ = nullptr;
    QSlider* sectionSlider_ = nullptr;
    QDoubleSpinBox* sectionValueSpin_ = nullptr;
    bool syncingSectionControls_ = false; // 위젯<->camera_ 역방향 시그널 루프 방지
};

} // namespace viewer
