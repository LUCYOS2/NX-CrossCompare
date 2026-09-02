#include "viewer/ModelViewport.h"

#include <QMouseEvent>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace viewer {

namespace {

// Solid shaded + edge overlay (NX 경량화 표시 스타일 목표, 개발계획_v2.md §17).
// 별도 법선 버퍼 없이, 뷰공간 위치의 화면공간 미분(dFdx/dFdy)으로 삼각형별
// flat normal을 계산한다 - 삼각형 안에서는 보간이 선형이라 이 값이 정확히
// 해당 삼각형의 기하 법선과 일치한다.
const char* kVertexShaderSrc = R"(
#version 330 core
layout(location = 0) in vec3 position;
uniform mat4 modelView;
uniform mat4 projection;
out vec3 vViewPos;
out vec3 vWorldPos;
void main() {
    vWorldPos = position;
    vec4 viewPos = modelView * vec4(position, 1.0);
    vViewPos = viewPos.xyz;
    gl_Position = projection * viewPos;
}
)";

// 단축키 H(섹션 뷰)의 절단 - 절단면 반대쪽 프래그먼트를 discard한다. sectionSign 유니폼은
// paintGL이 호출 목적에 따라 부호를 바꿔가며 재사용한다(일반 렌더=그대로, 스텐실 집계
// 패스=반대 부호로 "잘려나간 쪽"을 센다). isCapPass면 discard 없이 capColor로 단색 채움 -
// solid body를 자르면 뚫린 구멍이 아니라 채워진 단면이어야 한다는 요구사항 반영.
const char* kFragmentShaderSrc = R"(
#version 330 core
in vec3 vViewPos;
in vec3 vWorldPos;
out vec4 fragColor;
uniform vec3 color;
uniform bool wireframe;
uniform bool sectionEnabled;
uniform int sectionAxis;
uniform float sectionCoord;
uniform float sectionSign;
uniform bool isCapPass;
uniform vec3 capColor;
void main() {
    if (isCapPass) {
        fragColor = vec4(capColor, 1.0);
        return;
    }
    if (sectionEnabled && (vWorldPos[sectionAxis] - sectionCoord) * sectionSign < 0.0) {
        discard;
    }
    if (wireframe) {
        fragColor = vec4(0.05, 0.05, 0.05, 1.0);
        return;
    }
    vec3 normal = normalize(cross(dFdx(vViewPos), dFdy(vViewPos)));
    // 헤드라이트 방식(카메라 기준 고정 광원) - 회전해도 항상 자연스럽게 보임
    vec3 lightDir = normalize(vec3(0.3, 0.5, 0.8));
    float diffuse = max(dot(normal, lightDir), 0.0);
    vec3 shaded = color * (0.35 + 0.65 * diffuse);
    fragColor = vec4(shaded, 1.0);
}
)";

// 마우스 스크린 좌표 -> 월드 광선과 모델 bounding box의 교차점(slab method).
// 실제 삼각형이 아니라 bbox 기준 근사치지만, 섹션 위치를 잡는 용도로는 충분하다.
bool IntersectRayAabb(const QVector3D& origin, const QVector3D& dir,
                       const geometry::BoundingBox& box, QVector3D& outHit) {
    float tmin = 0.0f;
    float tmax = std::numeric_limits<float>::max();
    const float originArr[3] = {origin.x(), origin.y(), origin.z()};
    const float dirArr[3] = {dir.x(), dir.y(), dir.z()};
    const double minArr[3] = {box.min.x, box.min.y, box.min.z};
    const double maxArr[3] = {box.max.x, box.max.y, box.max.z};
    for (int i = 0; i < 3; ++i) {
        if (std::abs(dirArr[i]) < 1e-8f) {
            if (originArr[i] < static_cast<float>(minArr[i]) || originArr[i] > static_cast<float>(maxArr[i])) {
                return false;
            }
            continue;
        }
        float t1 = (static_cast<float>(minArr[i]) - originArr[i]) / dirArr[i];
        float t2 = (static_cast<float>(maxArr[i]) - originArr[i]) / dirArr[i];
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) {
            return false;
        }
    }
    outHit = origin + dir * tmin;
    return true;
}

// axis(0=X,1=Y,2=Z) 기준 나머지 두 축 인덱스.
void OtherAxes(int axis, int& otherA, int& otherB) {
    if (axis == 0) {
        otherA = 1;
        otherB = 2;
    } else if (axis == 1) {
        otherA = 0;
        otherB = 2;
    } else {
        otherA = 0;
        otherB = 1;
    }
}

// 캡 사각형(2삼각형) 정점 - bbox의 나머지 두 축 범위를 여유있게 덮도록 만들고,
// 실제로는 스텐실 테스트로 solid 단면 바깥은 어차피 그려지지 않는다.
std::vector<float> BuildCapQuad(int axis, float coord, const geometry::BoundingBox& box) {
    int a = 0;
    int b = 0;
    OtherAxes(axis, a, b);
    const double lo[3] = {box.min.x, box.min.y, box.min.z};
    const double hi[3] = {box.max.x, box.max.y, box.max.z};
    const double margin = std::max(1.0, ((hi[a] - lo[a]) + (hi[b] - lo[b])) * 0.15);
    const double aMin = lo[a] - margin;
    const double aMax = hi[a] + margin;
    const double bMin = lo[b] - margin;
    const double bMax = hi[b] + margin;

    std::vector<float> verts;
    verts.reserve(6 * 3);
    auto push = [&](double av, double bv) {
        float p[3];
        p[axis] = coord;
        p[a] = static_cast<float>(av);
        p[b] = static_cast<float>(bv);
        verts.push_back(p[0]);
        verts.push_back(p[1]);
        verts.push_back(p[2]);
    };
    push(aMin, bMin);
    push(aMax, bMin);
    push(aMax, bMax);
    push(aMin, bMin);
    push(aMax, bMax);
    push(aMin, bMax);
    return verts;
}

} // namespace

ModelViewport::ModelViewport(geometry::IGeometryAdapter* adapter, geometry::ModelHandle handle,
                              Camera* sharedCamera, QWidget* parent)
    : QOpenGLWidget(parent), adapter_(adapter), handle_(handle), sharedCamera_(sharedCamera),
      localCamera_(*sharedCamera) {
    setFocusPolicy(Qt::StrongFocus);
    setContextMenuPolicy(Qt::NoContextMenu); // 우클릭=회전이므로 컨텍스트 메뉴가 뜨면 안 됨
    setMouseTracking(true); // 버튼을 안 눌러도 마우스 위치를 추적 - H(섹션)가 클릭 없이 현재 커서 위치를 써야 함
}

ModelViewport::~ModelViewport() {
    makeCurrent();
    vbo_.destroy();
    capVbo_.destroy();
    doneCurrent();
}

void ModelViewport::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.15f, 0.15f, 0.17f, 1.0f);

    program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShaderSrc);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShaderSrc);
    program_.link();

    const auto triangles = adapter_->GetRenderTriangles(handle_);
    std::vector<float> vertexData;
    vertexData.reserve(triangles.size() * 3);
    for (const auto& v : triangles) {
        vertexData.push_back(static_cast<float>(v.x));
        vertexData.push_back(static_cast<float>(v.y));
        vertexData.push_back(static_cast<float>(v.z));
    }
    triangleVertexCount_ = static_cast<int>(triangles.size());

    vao_.create();
    vao_.bind();
    vbo_.create();
    vbo_.bind();
    vbo_.allocate(vertexData.data(), static_cast<int>(vertexData.size() * sizeof(float)));
    program_.bind();
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0, 3);
    vao_.release();
    vbo_.release();

    // 단면 캡용 VAO/VBO - 정점 데이터는 섹션 상태가 바뀔 때마다 paintGL에서 다시 채운다.
    capVao_.create();
    capVao_.bind();
    capVbo_.create();
    capVbo_.bind();
    capVbo_.allocate(6 * 3 * static_cast<int>(sizeof(float)));
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0, 3);
    capVao_.release();
    capVbo_.release();
    program_.release();
}

void ModelViewport::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

Camera* ModelViewport::ActiveTransform() {
    return (independentModePtr_ && *independentModePtr_) ? &localCamera_ : sharedCamera_;
}

const Camera* ModelViewport::ActiveTransform() const {
    return (independentModePtr_ && *independentModePtr_) ? &localCamera_ : sharedCamera_;
}

void ModelViewport::ResetLocalTransform() {
    localCamera_ = *sharedCamera_;
}

void ModelViewport::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    if (triangleVertexCount_ == 0) {
        return;
    }

    const float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;

    QMatrix4x4 projection;
    projection.perspective(45.0f, aspect, 1.0f, 20000.0f);
    const QMatrix4x4 modelView = ActiveTransform()->ViewMatrix();

    program_.bind();
    program_.setUniformValue("modelView", modelView);
    program_.setUniformValue("projection", projection);
    program_.setUniformValue("color", QVector3D(0.55f, 0.72f, 0.85f));
    // 섹션 뷰는 회전/팬/줌(ActiveTransform)과 달리 독립 조작 모드에서도 항상 sharedCamera_
    // 기준 - 뷰포트마다 단면 위치가 따로 놀면 비교 목적에 안 맞는다.
    program_.setUniformValue("sectionEnabled", sharedCamera_->sectionEnabled);
    program_.setUniformValue("sectionAxis", sharedCamera_->sectionAxis);
    program_.setUniformValue("sectionCoord", sharedCamera_->sectionCoord);
    program_.setUniformValue("sectionSign", sharedCamera_->sectionSign);
    program_.setUniformValue("isCapPass", false);
    program_.setUniformValue("capColor", QVector3D(0.85f, 0.35f, 0.22f)); // 단면 캡 관례색(주황/붉은 계열)

    vao_.bind();

    if (sharedCamera_->sectionEnabled) {
        // --- 1) 스텐실 집계 패스: 색/깊이 버퍼는 안 건드리고, "잘려나간(카메라 쪽)" 조각만
        // 반대 부호로 그려서 앞/뒷면 통과 횟수 패리티를 stencil에 남긴다. 짝수(0)면 그
        // 픽셀에서 절단면이 solid 바깥, 홀수(!=0)면 solid 내부를 지나간다는 뜻 -
        // 이 판정이 곧 "단면 캡을 어디에 그릴지"가 된다.
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_STENCIL_TEST);
        glClear(GL_STENCIL_BUFFER_BIT);
        glStencilFunc(GL_ALWAYS, 0, 0xFF);

        program_.setUniformValue("sectionSign", -sharedCamera_->sectionSign);

        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT); // 뒷면만 그림
        glStencilOp(GL_KEEP, GL_KEEP, GL_INCR_WRAP);
        glDrawArrays(GL_TRIANGLES, 0, triangleVertexCount_);

        glCullFace(GL_BACK); // 앞면만 그림
        glStencilOp(GL_KEEP, GL_KEEP, GL_DECR_WRAP);
        glDrawArrays(GL_TRIANGLES, 0, triangleVertexCount_);
        glDisable(GL_CULL_FACE);

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        program_.setUniformValue("sectionSign", sharedCamera_->sectionSign);

        // --- 2) 캡 사각형: 스텐실이 0이 아닌(=solid 내부) 픽셀에만 채워진다.
        const geometry::BoundingBox box = adapter_->GetBoundingBox(handle_);
        const auto capVerts = BuildCapQuad(sharedCamera_->sectionAxis, sharedCamera_->sectionCoord, box);
        capVao_.bind();
        capVbo_.bind();
        capVbo_.allocate(capVerts.data(), static_cast<int>(capVerts.size() * sizeof(float)));

        glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        program_.setUniformValue("isCapPass", true);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        program_.setUniformValue("isCapPass", false);

        capVbo_.release();
        capVao_.release();
        vao_.bind();
        glDisable(GL_STENCIL_TEST);
    }

    // --- 3) fill + wireframe 패스 (잘려나간 쪽은 그대로 discard). § 화면설정 - 화면 모드에
    // 따라 둘 중 하나만 그리거나(Solid/Wireframe) 둘 다 그린다(SolidEdge, 기존 기본 동작).
    const RenderMode renderMode = sharedCamera_->renderMode;

    if (renderMode != RenderMode::Wireframe) {
        // 1패스: 면 채우기(셰이딩). SolidEdge에서는 폴리곤 오프셋으로 살짝 뒤로 밀어
        // 다음 엣지 패스가 z-fighting 없이 위에 겹쳐 보이게 한다.
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        if (renderMode == RenderMode::SolidEdge) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 1.0f);
        }
        program_.setUniformValue("wireframe", false);
        glDrawArrays(GL_TRIANGLES, 0, triangleVertexCount_);
        if (renderMode == RenderMode::SolidEdge) {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
    }

    if (renderMode != RenderMode::Solid) {
        // 2패스: 삼각형 엣지 오버레이 - NX 경량화 표시의 "shaded + edges" 느낌을 재현.
        // Wireframe 모드에서는 이 패스가 유일한 패스가 된다(면 채우기 없음).
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        program_.setUniformValue("wireframe", true);
        glDrawArrays(GL_TRIANGLES, 0, triangleVertexCount_);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    vao_.release();
    program_.release();
}

void ModelViewport::mousePressEvent(QMouseEvent* event) {
    lastMousePos_ = event->pos();
    // 단축키(F/H/I/X/Y/Z/+/-)는 이제 이 위젯의 키보드 포커스가 아니라 MultiViewportPanel의
    // "마우스가 올라가 있는 뷰포트"(hoverEntered) 기준 QShortcut으로 처리한다 - 컨트롤 바를
    // 클릭해서 포커스가 다른 위젯으로 넘어가도 단축키가 계속 먹는다(사용자 리포트: 포커스
    // 기반 방식일 때 F/H/I가 전혀 안 먹었음). 그래도 시각적 포커스 표시는 자연스럽도록 유지.
    setFocus(Qt::MouseFocusReason);

    // 중클릭 = 단면 위치 지정(사용자 요청: 축 선택 후 도면의 마우스 포인트 기준으로 자르기).
    // 좌/우클릭은 이미 회전/팬이라 비어있는 휠버튼 클릭을 썼다. 단면이 꺼져 있으면 아무 일도
    // 안 한다.
    if (event->button() == Qt::MiddleButton && sharedCamera_->sectionEnabled) {
        pickSectionPositionAtCursor();
    }
}

void ModelViewport::mouseMoveEvent(QMouseEvent* event) {
    const QPoint delta = event->pos() - lastMousePos_;
    lastMousePos_ = event->pos(); // setMouseTracking(true)라 버튼 안 눌러도 항상 갱신됨 (H가 씀)

    // 마우스 매핑: 좌클릭=회전, 우클릭=팬, 중클릭=단면 위치 지정, 휠=줌(wheelEvent).
    // §16 2차 버전(면/점 피킹)은 나중에 다른 입력(더블클릭 등)으로 붙여야 한다.
    // 독립 조작 모드(§ 화면설정)면 sharedCamera_가 아니라 이 뷰포트의 localCamera_만 바뀐다.
    Camera* transform = ActiveTransform();
    if (event->buttons() & Qt::LeftButton) {
        transform->yawDeg += delta.x() * 0.5f;
        transform->pitchDeg += delta.y() * 0.5f;
    } else if (event->buttons() & Qt::RightButton) {
        transform->panX += delta.x() * 1.0f;
        transform->panY -= delta.y() * 1.0f;
    } else {
        return;
    }
    emit cameraChanged();
    update();
}

void ModelViewport::wheelEvent(QWheelEvent* event) {
    // 방향 반대로: 휠을 위로(양수 delta) 굴리면 줌아웃, 아래로(음수) 굴리면 줌인.
    ActiveTransform()->distance *= (event->angleDelta().y() > 0) ? 1.1f : 0.9f;
    emit cameraChanged();
    update();
}

void ModelViewport::enterEvent(QEnterEvent* event) {
    QOpenGLWidget::enterEvent(event);
    emit hoverEntered();
}

bool ModelViewport::computeSectionRay(QVector3D& outOrigin, QVector3D& outDir) const {
    if (width() <= 0 || height() <= 0) {
        return false;
    }
    const float aspect = static_cast<float>(width()) / static_cast<float>(height());
    QMatrix4x4 projection;
    projection.perspective(45.0f, aspect, 1.0f, 20000.0f);
    bool invertible = false;
    // 독립 조작 모드에서도 "지금 화면에 실제로 보이는 각도" 기준으로 광선을 쏴야 클릭
    // 위치와 일치하므로 ActiveTransform() 사용 (섹션 결과 자체는 sharedCamera_에 쓴다).
    const QMatrix4x4 inverseViewProj = (projection * ActiveTransform()->ViewMatrix()).inverted(&invertible);
    if (!invertible) {
        return false;
    }

    // 마우스 스크린 좌표(현재 커서 위치, setMouseTracking으로 항상 최신) -> NDC -> 월드 광선.
    const float ndcX = (2.0f * lastMousePos_.x() / static_cast<float>(width())) - 1.0f;
    const float ndcY = 1.0f - (2.0f * lastMousePos_.y() / static_cast<float>(height()));
    QVector4D nearPoint = inverseViewProj * QVector4D(ndcX, ndcY, -1.0f, 1.0f);
    QVector4D farPoint = inverseViewProj * QVector4D(ndcX, ndcY, 1.0f, 1.0f);
    if (qFuzzyIsNull(nearPoint.w()) || qFuzzyIsNull(farPoint.w())) {
        return false;
    }
    nearPoint /= nearPoint.w();
    farPoint /= farPoint.w();
    outOrigin = nearPoint.toVector3D();
    outDir = farPoint.toVector3D() - outOrigin;
    if (outDir.isNull()) {
        return false;
    }
    outDir.normalize();
    return true;
}

void ModelViewport::toggleSection() {
    if (sharedCamera_->sectionEnabled) {
        sharedCamera_->sectionEnabled = false;
        emit cameraChanged();
        update();
        return;
    }

    QVector3D rayOrigin;
    QVector3D rayDir;
    if (!computeSectionRay(rayOrigin, rayDir)) {
        return;
    }

    const geometry::BoundingBox box = adapter_->GetBoundingBox(handle_);
    QVector3D hit;
    if (!IntersectRayAabb(rayOrigin, rayDir, box, hit)) {
        // 광선이 bbox를 비껴가면(모델 바깥을 클릭) bbox 중심을 지나는 평면으로 대체.
        hit = QVector3D(static_cast<float>((box.min.x + box.max.x) * 0.5),
                         static_cast<float>((box.min.y + box.max.y) * 0.5),
                         static_cast<float>((box.min.z + box.max.z) * 0.5));
    }

    // 절단면은 고정 월드축(X/Y/Z) 중 현재 시선 방향에 가장 가까운 축을 자동 선택
    // (필요하면 켠 뒤 X/Y/Z 키로 직접 바꿀 수 있다 - switchSectionAxis 참고).
    const float rayDirArr[3] = {rayDir.x(), rayDir.y(), rayDir.z()};
    const float hitArr[3] = {hit.x(), hit.y(), hit.z()};
    int axis = 0;
    for (int i = 1; i < 3; ++i) {
        if (std::abs(rayDirArr[i]) > std::abs(rayDirArr[axis])) {
            axis = i;
        }
    }

    sharedCamera_->sectionEnabled = true;
    sharedCamera_->sectionAxis = axis;
    sharedCamera_->sectionCoord = hitArr[axis];
    // 카메라(광선 시작점)에서 클릭점 방향이 sectionSign 부호 - 그 반대편(카메라 쪽)을 잘라낸다.
    sharedCamera_->sectionSign = (rayDirArr[axis] >= 0.0f) ? 1.0f : -1.0f;

    emit cameraChanged();
    update();
}

void ModelViewport::switchSectionAxis(int axis) {
    QVector3D rayOrigin;
    QVector3D rayDir;
    if (!computeSectionRay(rayOrigin, rayDir)) {
        return;
    }

    const geometry::BoundingBox box = adapter_->GetBoundingBox(handle_);
    QVector3D hit;
    if (!IntersectRayAabb(rayOrigin, rayDir, box, hit)) {
        hit = QVector3D(static_cast<float>((box.min.x + box.max.x) * 0.5),
                         static_cast<float>((box.min.y + box.max.y) * 0.5),
                         static_cast<float>((box.min.z + box.max.z) * 0.5));
    }
    const float rayDirArr[3] = {rayDir.x(), rayDir.y(), rayDir.z()};
    const float hitArr[3] = {hit.x(), hit.y(), hit.z()};

    sharedCamera_->sectionAxis = axis;
    sharedCamera_->sectionCoord = hitArr[axis];
    sharedCamera_->sectionSign = (rayDirArr[axis] >= 0.0f) ? 1.0f : -1.0f;

    emit cameraChanged();
    update();
}

void ModelViewport::nudgeSection(float deltaMm) {
    sharedCamera_->sectionCoord += deltaMm;
    emit cameraChanged();
    update();
}

void ModelViewport::pickSectionPositionAtCursor() {
    QVector3D rayOrigin;
    QVector3D rayDir;
    if (!computeSectionRay(rayOrigin, rayDir)) {
        return;
    }

    const geometry::BoundingBox box = adapter_->GetBoundingBox(handle_);
    QVector3D hit;
    if (!IntersectRayAabb(rayOrigin, rayDir, box, hit)) {
        return; // 모델 바깥을 클릭하면 무시 - toggleSection과 달리 엉뚱한 위치로 튀지 않게.
    }

    // switchSectionAxis와 달리 축/방향은 그대로 두고 위치만 옮긴다 - 사용자가 이미
    // X/Y/Z로 축을 골라둔 상태에서 "그 축 기준으로 이 지점을 자르고 싶다"는 요청이라
    // 축을 다시 자동 판단하면 오히려 방해가 된다.
    const float hitArr[3] = {hit.x(), hit.y(), hit.z()};
    sharedCamera_->sectionCoord = hitArr[sharedCamera_->sectionAxis];

    emit cameraChanged();
    update();
}

} // namespace viewer
