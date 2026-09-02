#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector3D>

#include "geometry/IGeometryAdapter.h"
#include "viewer/Camera.h"

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

signals:
    void cameraChanged();
    // 마우스가 이 뷰포트 위로 들어옴 - MultiViewportPanel이 "단축키를 어느 뷰포트에
    // 적용할지" 판단하는 데 쓴다(호버된 뷰포트 = 가장 최근에 커서가 들어온 뷰포트).
    void hoverEntered();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
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
    QPoint lastMousePos_;

    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLShaderProgram program_;
    int triangleVertexCount_ = 0;

    // 단면 캡(§ 사용자 요청: solid body 절단면은 뚫린 구멍이 아니라 채워진 단면이어야 함) -
    // 스텐실 버퍼로 절단면 위에서 "솔리드 내부"인 픽셀만 골라 이 사각형을 그린다.
    QOpenGLVertexArrayObject capVao_;
    QOpenGLBuffer capVbo_{QOpenGLBuffer::VertexBuffer};
};

} // namespace viewer
