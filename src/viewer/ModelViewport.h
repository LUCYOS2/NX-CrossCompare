#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector3D>

#include <vector>

#include "geometry/IGeometryAdapter.h"
#include "viewer/Camera.h"

class QRubberBand;

namespace viewer {

// 인치 1개 = ModelViewport 1개. MultiViewportPanel이 여러 개를 그리드로 배치한다.
// Camera는 외부(MultiViewportPanel)가 소유하고, 이 위젯은 포인터로 참조만 한다 (동기화 목적).
class ModelViewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    ModelViewport(geometry::IGeometryAdapter* adapter, geometry::ModelHandle handle,
                  Camera* sharedCamera, QWidget* parent = nullptr);
    ~ModelViewport() override;

    // 단축키 H/X/Y/Z - 예전엔 이 위젯 자체의 keyPressEvent였는데, 그러면 "이 위젯이 실제
    // 키보드 포커스를 갖고 있을 때"만 동작했다. 컨트롤 바(체크박스/축 버튼 등)를 클릭하면
    // 포커스가 그쪽으로 넘어가버려서 그 이후엔 단축키가 하나도 안 먹는 문제로 이어졌다
    // (사용자 리포트: F/H/I가 전혀 안 먹음). 이제 MultiViewportPanel이 "마우스가 현재 어느
    // 뷰포트 위에 있는지"(hoverEntered) 기준으로 QShortcut을 통해 이 메서드들을 직접
    // 호출한다 - 포커스가 어디 있든 상관없다.
    void toggleSection();
    void switchSectionAxis(int axis);
    // +/- 키 - 현재 축 방향으로 절단면 위치를 mm 단위로 미세 조정.
    void nudgeSection(float deltaMm);

    // 뷰포트 조작 모드(§ 화면설정) - MultiViewportPanel이 소유한 independentMode 플래그를
    // 가리키는 포인터를 넘겨준다. *ptr이 true면 마우스 회전/팬/줌이 sharedCamera_가 아니라
    // 이 뷰포트 자신의 localCamera__에 반영되어 다른 뷰포트와 독립적으로 움직인다(섹션 뷰/
    // 렌더모드는 이 모드와 무관하게 항상 sharedCamera_ 기준 - 전체 공통이어야 자연스럽다).
    void SetIndependentModePtr(const bool* independentModePtr) { independentModePtr_ = independentModePtr; }
    // F(전체 맞춤)/I(ISO) 같은 "전체 공통 리셋" 동작 뒤에 MultiViewportPanel이 호출 -
    // 독립 모드 중이었더라도 이 뷰포트의 로컬 회전/팬/줌을 공유 카메라의 최신 값으로
    // 덮어써서, F/I를 누르면 개별 조작 여부와 상관없이 전부 같은 자세로 리셋되게 한다.
    void ResetLocalTransform();

    // § 3D 클릭 피킹(2026-09-09) - RuleEditorDialog가 "포인트 지정" 버튼으로 켜는 전역
    // 피킹 모드. *ptr이 true면 좌클릭이 회전 대신 facePicked를 쏜다(회전은 그동안 잠김) -
    // independentModePtr_와 같은 "MultiViewportPanel 소유, 포인터로 공유" 패턴.
    void SetPickModePtr(const bool* pickModeActive) { pickModeActive_ = pickModeActive; }

    // § 캡쳐 영역 드래그 지정(2026-09-11) - RuleEditorDialog가 "드래그로 캡쳐 영역
    // 지정" 버튼으로 켜는 모드. *ptr이 true면 좌클릭 드래그가 회전 대신 고무줄
    // 사각형(QRubberBand)을 그리고, 뗄 때 그 영역만 잘라 captureRegionGrabbed로
    // 내보낸다 - pickModeActive_와 같은 패턴.
    // § 뷰어 카메라 잠금(2026-09-11) - "캡쳐 전후에 카메라 on/off가 있으면 좋겠다 -
    // 움직이고 싶을 때 on, 고정하고 싶을 때 off"라는 요청. *ptr이 true면 좌클릭
    // 회전/우클릭 팬/휠 줌을 전부 무시한다 - pickModeActive_/captureRegionModeActive_와
    // 같은 패턴이지만 이건 그 둘과 달리 "피킹은 그대로 되고 카메라 조작만 막는다"(순서상
    // 피킹/캡쳐영역 체크보다 나중에 검사 - mouseMoveEvent/wheelEvent 참고).
    void SetCameraLockedPtr(const bool* cameraLocked) { cameraLockedPtr_ = cameraLocked; }

    void SetCaptureRegionModePtr(const bool* captureRegionModeActive) {
        captureRegionModeActive_ = captureRegionModeActive;
    }

    // § 캡쳐 화질 수정(2026-09-11) - 드래그 캡쳐가 검은 화면만 나오는 버그 수정. 일반
    // QWidget::grab()은 QOpenGLWidget에서는 위젯 합성(backing store) 경로를 타는데, 이
    // 위젯 자신의 마우스 이벤트 핸들러 안에서 재진입 호출하면 타이밍에 따라 아직 갱신 안
    // 된(또는 빈) 프레임을 찍을 수 있다. grabFramebuffer()는 OpenGL 프레임버퍼를 직접
    // 읽어오므로 이 문제가 없다 - 두 캡쳐 경로(활성 뷰포트 전체 캡쳐/드래그 영역 캡쳐)가
    // 전부 이걸 통해서만 픽스맵을 얻도록 통일했다. non-const인 이유: grabFramebuffer()
    // 자체가 non-const(내부적으로 다시 그릴 수 있어서).
    QPixmap grabViewportPixmap();

    // § 캡쳐된 이미지 위 클릭 피킹(2026-09-11) - 캡쳐 시점의 카메라/뷰포트 상태를 그대로
    // 얼려서 들고 있다가, 나중에(캡쳐 이미지가 화면에 떠 있는 동안 사용자가 그 위를
    // 클릭했을 때) 그 "당시" 상태 기준으로 광선을 다시 계산하는 데 쓴다 - 라이브
    // computeSectionRay()는 "지금 이 순간"의 카메라/크기만 쓸 수 있어서 이 용도로는
    // 못 쓴다(캡쳐 이후 사용자가 뷰를 돌리거나 창 크기를 바꿀 수 있으므로).
    struct CaptureInfo {
        Camera camera;
        int viewportWidth = 0;
        int viewportHeight = 0;
        // 드래그로 일부 영역만 캡쳐했으면 그 영역의 좌상단(뷰포트 논리좌표 기준) - 전체
        // 뷰포트를 캡쳐했으면 (0,0). 이미지 위 클릭 좌표에 이걸 더해야 "뷰포트 전체
        // 기준" 좌표가 되어 광선 계산에 쓸 수 있다.
        QPoint regionOffset;
        geometry::ModelHandle handle = geometry::kInvalidModelHandle;
    };
    // 지금 이 뷰포트의 카메라/크기/모델 정보를 스냅샷으로 뜬다 - 캡쳐(grabViewportPixmap()
    // 또는 드래그 크롭)와 "같은 순간"에 호출해야 나중에 정확한 역산이 된다.
    CaptureInfo captureInfo(QPoint regionOffset = QPoint(0, 0)) const;

    // 저장해둔 CaptureInfo와 "그 캡쳐 이미지 안에서의(뷰포트 논리좌표 기준)" 위치로부터
    // 월드 광선을 계산한다 - computeSectionRay()와 같은 수학이지만 라이브 상태가 아니라
    // 임의의 스냅샷을 받는 정적 버전. RuleEditorDialog가 캡쳐 이미지 클릭을 처리할 때 쓴다.
    static bool ComputeRayForView(const Camera& camera, int viewportWidth, int viewportHeight,
                                   const QPointF& logicalPos, QVector3D& outOrigin, QVector3D& outDir);

    // § NX 스타일 포인트 스냅(2026-09-11) - "찍었는데 화면에 표시가 안 된다"는 피드백에
    // 따라, RuleEditorDialog가 지금까지 찍은 포인트들의 월드 좌표를 여기로 넘겨주면
    // paintGL()이 밝은 주황색 점으로 항상 위(깊이 테스트 무시)에 그려준다.
    void SetMarkerPositions(const std::vector<QVector3D>& worldPositions);

    // § CTQ 치수선(2026-09-11) - 포인트 2개가 다 찍히면 그 사이를 잇는 치수선을 그려서
    // 캡쳐한 이미지가 바로 CTQ 문서용 그림이 되게 한다. 2개 미만이면 아무것도 안 그림.
    // § 화살표 치수선(2026-09-11) - "화살표시선까지 표현됐으면"이라는 요청에 따라 양 끝에
    // 삼각형 화살표도 같이 만든다(perpendicular는 world-up 기준 - 카메라를 따라 매 프레임
    // 다시 계산하진 않지만, 보통 쓰는 정면/등각 뷰에서는 충분히 화살표로 보인다).
    void SetDimensionLine(const std::vector<QVector3D>& worldPoints);

    // § 치수선 중앙 라벨(2026-09-11) - "검은 박스(마커별 라벨) 정리하고, 치수 표시선
    // 중앙에 Point(부위) 이니셜을 노란 배경+볼드로 보여달라"는 요청에 따라 마커별 라벨을
    // 이걸로 대체했다. SetDimensionLine()이 계산해둔 치수선 중점에 그린다(선이 없으면
    // 아무것도 안 그림).
    void SetDimensionLabel(const QString& text);

    // § 도면 설계법 가이드선(2026-09-12) - "치수 표시선에 가이드선 양옆에 빼달라"는 요청.
    // 전장 사이즈 자동 포인트처럼 실제 측정 지점(마커)과 치수선이 그려지는 위치(도면
    // 여백으로 오프셋된 자리)가 다를 때, 그 둘을 잇는 얇은 보조선(연장선)을 그린다.
    // fromPoints[i] -> toPoints[i] 쌍으로 그리며, 개수가 다르거나 비어있으면 아무것도
    // 안 그린다. 일반 수동 피킹(포인트=치수선 위치가 같음)에서는 호출할 필요 없음.
    void SetDimensionExtensionLines(
        const std::vector<QVector3D>& fromPoints, const std::vector<QVector3D>& toPoints);

    // § 치수선 드래그(2026-09-13) - "사용자가 드래그해서 위치 조정도 가능하게 해달라"는
    // 요청. RuleEditorDialog가 오프셋 축(도면 여백 쪽으로 미는 방향, 월드 단위벡터)을
    // 알려주면, 사용자가 치수선 라벨을 클릭+드래그할 때 그 축 방향으로만 얼마나 움직였는지
    // (월드 단위) 계산해서 dimensionOffsetDragged로 흘려보낸다. 축을 모르면(호출 안 됨)
    // 드래그 자체가 비활성 상태로 남는다.
    void SetDimensionOffsetAxis(const QVector3D& axisDirWorld);

signals:
    void cameraChanged();
    // § 치수선 드래그 - SetDimensionOffsetAxis 참고. 델타(월드 단위, 오프셋 축 방향
    // 부호 포함)만 보내고 절대 위치는 호출자(RuleEditorDialog)가 누적해서 관리한다.
    void dimensionOffsetDragged(float deltaWorldAlongAxis);
    // 마우스가 이 뷰포트 위로 들어옴 - MultiViewportPanel이 "단축키를 어느 뷰포트에
    // 적용할지" 판단하는 데 쓴다(호버된 뷰포트 = 가장 최근에 커서가 들어온 뷰포트).
    void hoverEntered();
    // 피킹 모드 중 좌클릭 - 클릭 순간의 월드 광선(origin/dir)을 그대로 넘긴다. 실제
    // "이게 무슨 면이냐" 판별은 IGeometryAdapter::PickFace(OCCT)가 하므로 여기선 광선만.
    void facePicked(geometry::ModelHandle handle, QVector3D rayOrigin, QVector3D rayDir);
    // 캡쳐 영역 드래그가 끝났을 때 - 그 사각형만 잘라낸 픽스맵 + 그 순간의 CaptureInfo
    // (이미지 위 클릭 피킹용).
    void captureRegionGrabbed(QPixmap pixmap, ModelViewport::CaptureInfo info);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void enterEvent(QEnterEvent* event) override;

private:
    // toggleSection/switchSectionAxis가 공유하는 "현재 마우스 위치 -> 월드 광선 -> bbox 교차점" 계산.
    bool computeSectionRay(QVector3D& outOrigin, QVector3D& outDir) const;
    // 중클릭 - 단면이 켜진 상태에서 클릭한 지점의 "현재 축" 좌표로 절단면 위치만 옮긴다
    // (축/방향은 그대로 유지 - toggleSection처럼 축을 다시 고르지 않는다).
    void pickSectionPositionAtCursor();
    // 회전/팬/줌(마우스 조작) 대상 - 동시 조작 모드면 sharedCamera_, 독립 모드면 localCamera_.
    // 섹션 뷰/렌더모드는 이 함수를 거치지 않고 항상 sharedCamera_를 직접 읽는다(전체 공통).
    Camera* ActiveTransform();
    const Camera* ActiveTransform() const;

    geometry::IGeometryAdapter* adapter_;
    geometry::ModelHandle handle_;
    Camera* sharedCamera_;
    Camera localCamera_;                       // 독립 조작 모드에서만 사용
    const bool* independentModePtr_ = nullptr; // MultiViewportPanel 소유, 생성 직후 SetIndependentModePtr로 연결됨
    const bool* pickModeActive_ = nullptr;     // MultiViewportPanel 소유, RuleEditorDialog가 켬/끔
    const bool* captureRegionModeActive_ = nullptr; // MultiViewportPanel 소유, RuleEditorDialog가 켬/끔
    const bool* cameraLockedPtr_ = nullptr;    // MultiViewportPanel 소유, RuleEditorDialog가 켬/끔
    QPoint lastMousePos_;
    QRubberBand* rubberBand_ = nullptr; // 캡쳐 영역 드래그 중에만 lazy 생성
    QPoint captureDragStart_;

    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLShaderProgram program_;
    int triangleVertexCount_ = 0;

    // § 불필요한 선 정리(2026-09-11) - "엣지 오버레이"가 예전엔 삼각형 외곽선을 그대로
    // 그려서 사각형 면을 둘로 쪼갠 대각선까지 다 보였다. 이제 실제 B-rep 엣지만
    // 담은 별도 버퍼(GetRenderEdges, GL_LINES)로 그린다.
    QOpenGLVertexArrayObject edgeVao_;
    QOpenGLBuffer edgeVbo_{QOpenGLBuffer::VertexBuffer};
    int edgeVertexCount_ = 0;

    // 단면 캡(§ 사용자 요청: solid body 절단면은 뚫린 구멍이 아니라 채워진 단면이어야 함) -
    // 스텐실 버퍼로 절단면 위에서 "솔리드 내부"인 픽셀만 골라 이 사각형을 그린다.
    QOpenGLVertexArrayObject capVao_;
    QOpenGLBuffer capVbo_{QOpenGLBuffer::VertexBuffer};

    // § NX 스타일 포인트 스냅 - 지금까지 찍은 포인트 마커(월드 좌표, GL_POINTS로 그림).
    QOpenGLVertexArrayObject markerVao_;
    QOpenGLBuffer markerVbo_{QOpenGLBuffer::VertexBuffer};
    int markerCount_ = 0;

    // § 라이브 호버 미리보기(2026-09-11) - "마우스가 근처에 갔을 때 먼저 주황색으로
    // 가이드가 보이면 좋겠다"는 요청. 피킹 모드 중 mouseMoveEvent가 매번 PickFace를 다시
    // 불러 갱신한다(확정 전 미리보기라 markerVbo_와는 별개 버퍼). 확정 마커보다 옅은
    // 주황으로 구분한다.
    bool hoverPickValid_ = false;
    QVector3D hoverPickPoint_;
    QOpenGLVertexArrayObject hoverVao_;
    QOpenGLBuffer hoverVbo_{QOpenGLBuffer::VertexBuffer};

    // § 하이라이트 미리보기(2026-09-13) - "면/포인트/EDGE 근처에 가면 그 패턴을 통째로
    // 주황색으로 하이라이트해달라"는 요청(예전엔 뭘 가리키든 작은 점 하나만 찍혔음).
    // PickResult::highlightTriangles/highlightEdgeSegments를 그대로 업로드해서 그린다 -
    // hoverVao_(점)와는 별개 버퍼, 매 마우스 이동마다 mouseMoveEvent가 다시 채운다.
    QOpenGLVertexArrayObject hoverFaceVao_;
    QOpenGLBuffer hoverFaceVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject hoverEdgeVao_;
    QOpenGLBuffer hoverEdgeVbo_{QOpenGLBuffer::VertexBuffer};
    // mouseMoveEvent가 채우고(GL 컨텍스트 없이도 안전), paintGL이 실제 GL 컨텍스트 안에서
    // 이 데이터를 hoverFaceVbo_/hoverEdgeVbo_로 업로드한다(hoverPickPoint_와 같은 패턴).
    std::vector<QVector3D> hoverFaceTriangles_;
    std::vector<QVector3D> hoverEdgeSegments_;

    // § CTQ 치수선 - 포인트 2개 사이를 잇는 선(GL_LINES, 항상 위에 보이게 깊이 테스트 무시).
    QOpenGLVertexArrayObject lineVao_;
    QOpenGLBuffer lineVbo_{QOpenGLBuffer::VertexBuffer};
    int lineVertexCount_ = 0;
    // § 화살표 치수선 - 치수선 양 끝의 삼각형 화살촉(GL_TRIANGLES, 6정점=삼각형 2개).
    QOpenGLVertexArrayObject arrowVao_;
    QOpenGLBuffer arrowVbo_{QOpenGLBuffer::VertexBuffer};
    int arrowVertexCount_ = 0;

    // § 도면 설계법 가이드선 - 실제 측정 지점과 치수선 사이를 잇는 얇은 보조선(GL_LINES).
    QOpenGLVertexArrayObject extLineVao_;
    QOpenGLBuffer extLineVbo_{QOpenGLBuffer::VertexBuffer};
    int extLineVertexCount_ = 0;

    // § 치수선 중앙 라벨 - 텍스트를 그리려면 매 프레임 월드 좌표를 화면 좌표로 다시
    // 투영해야 해서(QPainter 텍스트 패스, paintGL 참고) CPU 쪽에 중점 좌표를 들고 있는다.
    QVector3D dimensionLineMidpoint_;
    bool hasDimensionLine_ = false;
    QString dimensionLabel_;
    // § 치수선 드래그 - paintGL이 라벨을 그릴 때마다 실제 화면 사각형을 여기 기록해둬야
    // mousePressEvent가 "라벨을 클릭했는지" 판정할 수 있다.
    QRectF lastLabelScreenRect_;
    // SetDimensionOffsetAxis()로 받은 월드 단위벡터 - 드래그 중 화면 이동량을 이 축
    // 방향 성분으로 변환하는 데 쓴다. hasDimensionOffsetAxis_==false면 드래그 비활성.
    QVector3D dimensionOffsetAxisWorld_;
    bool hasDimensionOffsetAxis_ = false;
    bool draggingDimensionOffset_ = false;
};

} // namespace viewer
