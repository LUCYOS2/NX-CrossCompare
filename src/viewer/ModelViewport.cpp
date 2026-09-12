#include "viewer/ModelViewport.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QRect>
#include <QRubberBand>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace viewer {

namespace {

// tan(45°/2) = tan(22.5°) ≈ 0.41421356 (2의 제곱근-1, 상수로 고정해 매 프레임 std::tan
// 호출 없이 계산한다 - 투영의 수직 FOV 환산값 45°가 바뀌지 않는 한 정확하다). 투영 행렬
// 계산(BuildProjectionMatrix)과 치수선 드래그의 픽셀→월드 변환(mouseMoveEvent)이
// 같은 화면 크기 기준을 써야 해서 파일 스코프 상수로 공유한다.
constexpr float kHalfFovTan = 0.41421356f;

// § 정투상(orthographic) 전환(2026-09-13) - "FRONT/TOP 같은 축 정렬 뷰인데도 면이
// 비스듬하게 보인다"는 리포트. 원근(perspective) 투영은 카메라 시점(eye)에서 갈라지는
// 광선 때문에 화면 중심이 아닌 곳은 축 정렬 뷰라도 옆면이 살짝 비쳐 보인다 - 실제 CAD
// 프로그램(NX 등)의 FRONT/TOP/RIGHT 뷰가 직교(orthographic)인 이유다. 이 앱 전체
// 렌더링/피킹을 직교로 바꾸되, distance(마우스 휠로 조절하는 그 값) 기준으로 예전
// 45° 원근과 화면상 크기가 비슷하게 보이도록 절반 높이를 환산해서 ortho 절두체를 만든다 -
// 줌 조작(마우스 휠이 distance를 바꾸는 방식) 자체는 그대로 두고 투영 방식만 바뀐다.
QMatrix4x4 BuildProjectionMatrix(float distance, float aspect) {
    const float halfHeight = distance * kHalfFovTan;
    const float halfWidth = halfHeight * aspect;
    QMatrix4x4 projection;
    projection.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, 1.0f, 20000.0f);
    return projection;
}

// Solid shaded + edge overlay (NX 경량화 표시 스타일 목표, 개발계획_v2.md §17).
// 별도 법선 버퍼 없이, 뷰공간 위치의 화면공간 미분(dFdx/dFdy)으로 삼각형별
// flat normal을 계산한다 - 삼각형 안에서는 보간이 선형이라 이 값이 정확히
// 해당 삼각형의 기하 법선과 일치한다.
const char* kVertexShaderSrc = R"(
#version 330 core
layout(location = 0) in vec3 position;
uniform mat4 modelView;
uniform mat4 projection;
uniform float pointSize; // § 포인트 마커 - GL_POINTS 그릴 때만 0보다 큰 값을 준다.
out vec3 vViewPos;
out vec3 vWorldPos;
void main() {
    vWorldPos = position;
    vec4 viewPos = modelView * vec4(position, 1.0);
    vViewPos = viewPos.xyz;
    gl_Position = projection * viewPos;
    gl_PointSize = pointSize;
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
uniform bool isMarkerPass; // § 포인트 마커 - 셰이딩/단면 무시하고 단색으로만 그린다.
uniform bool isLinePass;   // § CTQ 치수선 - GL_LINES라 gl_PointCoord 원형 discard가 의미 없어 분리.
uniform bool isHoverFacePass; // § 하이라이트 미리보기 - 면 전체를 반투명 단색으로 덮어 보여준다.
uniform vec3 markerColor;
void main() {
    if (isMarkerPass) {
        // 원형 점으로 보이게 - 사각형 점 스프라이트 모서리를 discard.
        vec2 fromCenter = gl_PointCoord - vec2(0.5);
        if (dot(fromCenter, fromCenter) > 0.25) {
            discard;
        }
        fragColor = vec4(markerColor, 1.0);
        return;
    }
    if (isLinePass) {
        fragColor = vec4(markerColor, 1.0);
        return;
    }
    if (isHoverFacePass) {
        fragColor = vec4(markerColor, 0.35);
        return;
    }
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
    edgeVbo_.destroy();
    capVbo_.destroy();
    markerVbo_.destroy();
    hoverVbo_.destroy();
    hoverFaceVbo_.destroy();
    hoverEdgeVbo_.destroy();
    lineVbo_.destroy();
    arrowVbo_.destroy();
    extLineVbo_.destroy();
    doneCurrent();
}

void ModelViewport::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    // § 뷰어 배경 흰색 변경(2026-09-11) - 어두운 배경 대신 흰 배경으로.
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    // § 포인트 마커 - core profile에서는 이걸 켜야 버텍스 셰이더의 gl_PointSize가
    // 실제로 적용된다(안 켜면 기본 크기 1px 점만 그려짐).
    glEnable(GL_PROGRAM_POINT_SIZE);

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

    // § 불필요한 선 정리 - 실제 B-rep 엣지(GetRenderEdges)만 담은 별도 버퍼.
    // GetRenderTriangles()와 달리 정적(로드 시 한 번) - 섹션 뷰/모델 자체가 안 바뀌는 한
    // 다시 채울 필요 없다.
    const auto edges = adapter_->GetRenderEdges(handle_);
    std::vector<float> edgeVertexData;
    edgeVertexData.reserve(edges.size() * 3);
    for (const auto& v : edges) {
        edgeVertexData.push_back(static_cast<float>(v.x));
        edgeVertexData.push_back(static_cast<float>(v.y));
        edgeVertexData.push_back(static_cast<float>(v.z));
    }
    edgeVertexCount_ = static_cast<int>(edges.size());

    edgeVao_.create();
    edgeVao_.bind();
    edgeVbo_.create();
    edgeVbo_.bind();
    edgeVbo_.allocate(edgeVertexData.data(), static_cast<int>(edgeVertexData.size() * sizeof(float)));
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0, 3);
    edgeVao_.release();
    edgeVbo_.release();

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

    // § 포인트 마커용 VAO/VBO - 정점 데이터는 SetMarkerPositions()가 바뀔 때마다 채운다.
    markerVao_.create();
    markerVao_.bind();
    markerVbo_.create();
    markerVbo_.bind();
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0, 3);
    markerVao_.release();
    markerVbo_.release();

    // § 라이브 호버 미리보기용 VAO/VBO - 점 1개(3 floats)만 담는다. paintGL이 매 프레임
    // hoverPickPoint_로 다시 채운다.
    hoverVao_.create();
    hoverVao_.bind();
    hoverVbo_.create();
    hoverVbo_.bind();
    hoverVbo_.allocate(3 * static_cast<int>(sizeof(float)));
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0, 3);
    hoverVao_.release();
    hoverVbo_.release();

    // § 하이라이트 미리보기용 VAO/VBO - 정점 데이터는 mouseMoveEvent가 PickFace 결과로
    // 매 프레임 다시 채운다(면 삼각형/모서리 선분).
    hoverFaceVao_.create();
    hoverFaceVao_.bind();
    hoverFaceVbo_.create();
    hoverFaceVbo_.bind();
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0, 3);
    hoverFaceVao_.release();
    hoverFaceVbo_.release();

    hoverEdgeVao_.create();
    hoverEdgeVao_.bind();
    hoverEdgeVbo_.create();
    hoverEdgeVbo_.bind();
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0, 3);
    hoverEdgeVao_.release();
    hoverEdgeVbo_.release();

    // § CTQ 치수선용 VAO/VBO - 정점 데이터는 SetDimensionLine()이 바뀔 때마다 채운다.
    lineVao_.create();
    lineVao_.bind();
    lineVbo_.create();
    lineVbo_.bind();
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0, 3);
    lineVao_.release();
    lineVbo_.release();

    // § 화살표 치수선용 VAO/VBO - 삼각형 2개(6정점) - SetDimensionLine()이 채운다.
    arrowVao_.create();
    arrowVao_.bind();
    arrowVbo_.create();
    arrowVbo_.bind();
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0, 3);
    arrowVao_.release();
    arrowVbo_.release();

    // § 도면 설계법 가이드선용 VAO/VBO - SetDimensionExtensionLines()가 채운다.
    extLineVao_.create();
    extLineVao_.bind();
    extLineVbo_.create();
    extLineVbo_.bind();
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0, 3);
    extLineVao_.release();
    extLineVbo_.release();

    program_.release();
}

void ModelViewport::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void ModelViewport::SetMarkerPositions(const std::vector<QVector3D>& worldPositions) {
    markerCount_ = static_cast<int>(worldPositions.size());
    if (markerCount_ == 0) {
        update();
        return;
    }
    std::vector<float> data;
    data.reserve(worldPositions.size() * 3);
    for (const auto& p : worldPositions) {
        data.push_back(p.x());
        data.push_back(p.y());
        data.push_back(p.z());
    }
    makeCurrent();
    markerVbo_.bind();
    markerVbo_.allocate(data.data(), static_cast<int>(data.size() * sizeof(float)));
    markerVbo_.release();
    doneCurrent();
    update();
}

void ModelViewport::SetDimensionExtensionLines(
    const std::vector<QVector3D>& fromPoints, const std::vector<QVector3D>& toPoints) {
    if (fromPoints.empty() || fromPoints.size() != toPoints.size()) {
        extLineVertexCount_ = 0;
        update();
        return;
    }
    std::vector<float> data;
    data.reserve(fromPoints.size() * 6);
    for (size_t i = 0; i < fromPoints.size(); ++i) {
        for (const QVector3D& v : {fromPoints[i], toPoints[i]}) {
            data.push_back(v.x());
            data.push_back(v.y());
            data.push_back(v.z());
        }
    }
    extLineVertexCount_ = static_cast<int>(data.size() / 3);

    makeCurrent();
    extLineVbo_.bind();
    extLineVbo_.allocate(data.data(), static_cast<int>(data.size() * sizeof(float)));
    extLineVbo_.release();
    doneCurrent();
    update();
}

void ModelViewport::SetDimensionOffsetAxis(const QVector3D& axisDirWorld) {
    dimensionOffsetAxisWorld_ = axisDirWorld;
    hasDimensionOffsetAxis_ = axisDirWorld.lengthSquared() > 1e-8f;
}

void ModelViewport::SetDimensionLabel(const QString& text) {
    dimensionLabel_ = text;
    update();
}

namespace {
// § 화살표 치수선 - 한쪽 끝(tip, 점 위치)을 향해 열리는 삼각형 화살촉 6정점(2삼각형은
// 아니고 1삼각형, 3정점)을 만든다. dir는 화살촉이 "가리키는" 방향(선을 따라 tip 쪽으로).
// perp는 화살촉 폭 방향(선에 수직) - world-up 기준 고정값이라 카메라를 따라 돌지는
// 않지만, 정면/등각처럼 흔히 쓰는 뷰에서는 충분히 화살표로 보인다.
void AppendArrowTriangle(
    std::vector<float>& out, const QVector3D& tip, const QVector3D& dirToTip, const QVector3D& perp,
    float arrowLen, float arrowHalfWidth) {
    const QVector3D baseCenter = tip - dirToTip * arrowLen;
    const QVector3D baseLeft = baseCenter + perp * arrowHalfWidth;
    const QVector3D baseRight = baseCenter - perp * arrowHalfWidth;
    for (const QVector3D& v : {tip, baseLeft, baseRight}) {
        out.push_back(v.x());
        out.push_back(v.y());
        out.push_back(v.z());
    }
}
} // namespace

void ModelViewport::SetDimensionLine(const std::vector<QVector3D>& worldPoints) {
    if (worldPoints.size() < 2) {
        lineVertexCount_ = 0;
        arrowVertexCount_ = 0;
        hasDimensionLine_ = false;
        update();
        return;
    }
    const QVector3D& a = worldPoints[0];
    const QVector3D& b = worldPoints[1];
    const float data[6] = {a.x(), a.y(), a.z(), b.x(), b.y(), b.z()};
    lineVertexCount_ = 2;
    // § 치수선 중앙 라벨 - Point(부위) 이니셜을 그릴 위치(paintGL의 QPainter 패스).
    dimensionLineMidpoint_ = (a + b) * 0.5f;
    hasDimensionLine_ = true;

    // § 화살표 치수선(2026-09-11) - "화살표시선까지 표현됐으면"이라는 요청. 선 길이의
    // 12%를 화살촉 길이로 쓴다(짧은 선에서도, 긴 선에서도 자연스럽게 스케일).
    QVector3D dir = b - a;
    const float lineLen = dir.length();
    std::vector<float> arrowData;
    if (lineLen > 1e-6f) {
        dir /= lineLen;
        // § 화살촉 안 보임 버그 수정(2026-09-13) - "XY 화면에서는 화살표가 안 보이는데
        // 조금만 회전하면 보인다"는 리포트. 원인: 화살촉 폭 방향(perp)을 고정된
        // world-up(0,1,0)과의 외적으로만 구했는데, 전장 사이즈(X) 치수선은 Y축 쪽으로
        // 오프셋되어 있어 dir=X, worldUp=Y이면 perp가 정확히 Z축이 된다. 카메라가 정확히
        // -Z를 보는 XY 정면뷰에서는 이 Z방향 폭이 화면에 완전히 투영되어 사라져(진짜
        // 직교 투영으로 바꾼 뒤라 원근 흐림도 없어 완전히 0) 화살촉이 넓이 0인 삼각형이
        // 되어 아예 안 그려졌다. 화면과 나란한(=현재 카메라 시선과 수직인) 폭 방향을
        // 써야 어느 각도에서 봐도 항상 화면에 실제 폭으로 보인다 - 시선 방향과 먼저
        // 외적하고, 그게 실패하면(선이 시선과 거의 평행할 때만) world-up으로 대체한다.
        const Camera* camera = ActiveTransform();
        const double yawRad = camera->yawDeg * 0.017453292519943295; // deg -> rad
        const double pitchRad = camera->pitchDeg * 0.017453292519943295;
        const QVector3D viewForward(
            static_cast<float>(std::cos(pitchRad) * std::sin(yawRad)), static_cast<float>(-std::sin(pitchRad)),
            static_cast<float>(-std::cos(pitchRad) * std::cos(yawRad)));
        QVector3D perp = QVector3D::crossProduct(dir, viewForward);
        if (perp.length() < 1e-4f) {
            // 선이 현재 시선 방향과 거의 평행(카메라가 선을 정면에서 바라보는 드문
            // 경우) - world-up으로 대체.
            perp = QVector3D::crossProduct(dir, QVector3D(0.0f, 1.0f, 0.0f));
            if (perp.length() < 1e-4f) {
                perp = QVector3D::crossProduct(dir, QVector3D(1.0f, 0.0f, 0.0f));
            }
        }
        perp.normalize();
        const float arrowLen = std::min(lineLen * 0.12f, lineLen * 0.5f);
        const float arrowHalfWidth = arrowLen * 0.35f;
        arrowData.reserve(18);
        AppendArrowTriangle(arrowData, a, -dir, perp, arrowLen, arrowHalfWidth); // A에서 A쪽(바깥)을 가리킴
        AppendArrowTriangle(arrowData, b, dir, perp, arrowLen, arrowHalfWidth);  // B에서 B쪽(바깥)을 가리킴
    }
    arrowVertexCount_ = static_cast<int>(arrowData.size() / 3);

    makeCurrent();
    lineVbo_.bind();
    lineVbo_.allocate(data, static_cast<int>(sizeof(data)));
    lineVbo_.release();
    if (arrowVertexCount_ > 0) {
        arrowVbo_.bind();
        arrowVbo_.allocate(arrowData.data(), static_cast<int>(arrowData.size() * sizeof(float)));
        arrowVbo_.release();
    }
    doneCurrent();
    update();
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
    // § 치수선 중앙 라벨(2026-09-11) - Point(부위) 이니셜을 3D 장면 위에 텍스트로
    // 그리려면(지금까지 점/선 GL_POINTS/GL_LINES/GL_TRIANGLES로는 텍스트 자체를 못 그림)
    // Qt의 "QOpenGLWidget + QPainter 혼합" 공식 패턴을 쓴다 - 기존 raw GL 호출 구간
    // 전체를 beginNativePainting()/endNativePainting()으로 감싸고, 그 뒤에 이 QPainter로
    // 화면좌표 텍스트를 그린다(3D 투영은 이 함수 안에서 직접 계산).
    QPainter painter(this);
    painter.beginNativePainting();
    // § 프레임마다 깊이 테스트 상태를 명시적으로 켜서 시작 - 이전 프레임이 끝에서
    // glDisable(GL_DEPTH_TEST)로 끝났어도(치수선 라벨을 위해, 아래 endNativePainting
    // 직전 참고) 이번 프레임의 3D 솔리드 렌더링은 항상 깊이 테스트가 켜진 상태로
    // 시작해야 면이 올바른 순서로 그려진다.
    glEnable(GL_DEPTH_TEST);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    if (triangleVertexCount_ == 0) {
        painter.endNativePainting();
        return;
    }

    const float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;

    const QMatrix4x4 projection = BuildProjectionMatrix(ActiveTransform()->distance, aspect);
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
    program_.setUniformValue("isMarkerPass", false);
    program_.setUniformValue("isLinePass", false);
    program_.setUniformValue("isHoverFacePass", false);
    program_.setUniformValue("pointSize", 0.0f);

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
        // 2패스: 엣지 오버레이 - NX 경량화 표시의 "shaded + edges" 느낌을 재현.
        // Wireframe 모드에서는 이 패스가 유일한 패스가 된다(면 채우기 없음).
        // § 불필요한 선 정리(2026-09-11) - 예전엔 삼각형 전체를 GL_LINE 폴리곤 모드로
        // 그려서 사각형 면을 쪼갠 대각선까지 다 보였다. 이제 실제 B-rep 엣지만 담은
        // edgeVao_를 GL_LINES로 직접 그린다 - 폴리곤 모드 전환도 필요 없다.
        program_.setUniformValue("wireframe", true);
        edgeVao_.bind();
        glDrawArrays(GL_LINES, 0, edgeVertexCount_);
        edgeVao_.release();
        vao_.bind(); // 다음 패스(마커 등)가 기대하는 VAO로 되돌려놓는다.
    }

    // --- 4) CTQ 치수선 - § 치수 표시선(2026-09-11). "측정 포인트가 생기면 그 기준으로
    // 치수 표시선도 같이 생겼으면 좋겠다"는 요청 - 포인트 2개를 잇는 선을 캡쳐 이미지에
    // 바로 남긴다. 마커보다 먼저 그려서 마커가 선 끝 위에 겹쳐 보이게 한다.
    if (lineVertexCount_ > 0) {
        glDisable(GL_DEPTH_TEST);
        program_.setUniformValue("isLinePass", true);
        // § 뷰어 배경 흰색 변경 - 아이보리색은 흰 배경에서 거의 안 보여서 진한 남색/검정
        // 계열(기술 도면 치수선 관례색)로 바꿨다.
        program_.setUniformValue("markerColor", QVector3D(0.1f, 0.12f, 0.2f));
        glLineWidth(2.0f);
        lineVao_.bind();
        glDrawArrays(GL_LINES, 0, lineVertexCount_);
        lineVao_.release();
        // § 화살표 치수선 - 같은 색으로 양 끝 화살촉 채우기.
        if (arrowVertexCount_ > 0) {
            arrowVao_.bind();
            glDrawArrays(GL_TRIANGLES, 0, arrowVertexCount_);
            arrowVao_.release();
        }
        program_.setUniformValue("isLinePass", false);
        glEnable(GL_DEPTH_TEST);
    }

    // --- 4b) 도면 설계법 가이드선(2026-09-12) - "치수 표시선에 가이드선 양옆에 빼달라"는
    // 요청. 실제 측정 지점(모델 위)과 치수선(도면 여백으로 오프셋된 위치)을 잇는 얇은
    // 보조선 - 치수선보다 옅은 회색, 가는 선으로 구분한다. 치수선(4번)보다 먼저 그려서
    // 화살표/마커가 그 위에 덮이게 한다.
    if (extLineVertexCount_ > 0) {
        glDisable(GL_DEPTH_TEST);
        program_.setUniformValue("isLinePass", true);
        program_.setUniformValue("markerColor", QVector3D(0.55f, 0.55f, 0.55f)); // 옅은 회색(보조선)
        glLineWidth(1.0f);
        extLineVao_.bind();
        glDrawArrays(GL_LINES, 0, extLineVertexCount_);
        extLineVao_.release();
        program_.setUniformValue("isLinePass", false);
        glEnable(GL_DEPTH_TEST);
    }

    // --- 5) 포인트 마커 - § NX 스타일 포인트 스냅(2026-09-11). "찍었는데 화면에 표시가
    // 안 된다"는 피드백에 대응 - 깊이 테스트를 꺼서 모델 뒤에 가려지지 않고 항상 위에
    // 보이게 한다(정확히 어디를 찍었는지 확인하는 용도라 가려지면 안 됨).
    if (markerCount_ > 0) {
        glDisable(GL_DEPTH_TEST);
        program_.setUniformValue("isMarkerPass", true);
        program_.setUniformValue("markerColor", QVector3D(1.0f, 0.55f, 0.0f)); // 밝은 주황
        program_.setUniformValue("pointSize", 16.0f);
        markerVao_.bind();
        glDrawArrays(GL_POINTS, 0, markerCount_);
        markerVao_.release();
        program_.setUniformValue("isMarkerPass", false);
        program_.setUniformValue("pointSize", 0.0f);
        glEnable(GL_DEPTH_TEST);
    }

    // --- 6) 라이브 호버 미리보기 - § 클릭 전 미리보기(2026-09-11). 확정 마커(진한 주황)와
    // 구분되도록 더 옅은 주황을 쓴다. 피킹 모드가 꺼지면(pickModeActive_) 더 이상 갱신되지
    // 않는 낡은 값이 남아있을 수 있어 조건에 pickModeActive_도 같이 건다.
    if (hoverPickValid_ && pickModeActive_ && *pickModeActive_) {
        const float hoverData[3] = {hoverPickPoint_.x(), hoverPickPoint_.y(), hoverPickPoint_.z()};
        hoverVbo_.bind();
        hoverVbo_.allocate(hoverData, static_cast<int>(sizeof(hoverData)));
        hoverVbo_.release();

        glDisable(GL_DEPTH_TEST);
        program_.setUniformValue("isMarkerPass", true);
        program_.setUniformValue("markerColor", QVector3D(1.0f, 0.8f, 0.35f)); // 옅은 주황(미리보기)
        program_.setUniformValue("pointSize", 22.0f); // 확정 마커보다 크게 - "지금 이게 후보"라는 느낌
        hoverVao_.bind();
        glDrawArrays(GL_POINTS, 0, 1);
        hoverVao_.release();
        program_.setUniformValue("isMarkerPass", false);
        program_.setUniformValue("pointSize", 0.0f);
        glEnable(GL_DEPTH_TEST);
    }

    // --- 6b) 하이라이트 미리보기(2026-09-13) - "면/포인트/EDGE 근처에 가면 그 패턴을
    // 통째로 주황색으로 하이라이트해달라"는 요청("면에 마우스를 가져가도 점으로만
    // 찍힌다"는 리포트). 면이면 반투명 주황 면 채우기를, 모서리 중간점 스냅이면 굵은
    // 주황 선을 그린다 - hoverPickValid_(점 미리보기)와는 독립 조건이다(면/원통 히트는
    // 이제 점을 안 켜므로 - "점이 마우스에 계속 붙어다닌다"는 리포트 참고). 벡터가
    // 비어있으면(스냅 대상이 없거나 시작점/끝점/중앙점/교차점처럼 확장 하이라이트가
    // 없는 점 종류) 안쪽 empty 체크에서 자연히 아무것도 안 그린다.
    if (pickModeActive_ && *pickModeActive_) {
        if (!hoverFaceTriangles_.empty()) {
            std::vector<float> faceData;
            faceData.reserve(hoverFaceTriangles_.size() * 3);
            for (const auto& v : hoverFaceTriangles_) {
                faceData.push_back(v.x());
                faceData.push_back(v.y());
                faceData.push_back(v.z());
            }
            hoverFaceVbo_.bind();
            hoverFaceVbo_.allocate(faceData.data(), static_cast<int>(faceData.size() * sizeof(float)));
            hoverFaceVbo_.release();

            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            program_.setUniformValue("isHoverFacePass", true);
            program_.setUniformValue("markerColor", QVector3D(1.0f, 0.55f, 0.0f)); // 밝은 주황
            hoverFaceVao_.bind();
            glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(hoverFaceTriangles_.size()));
            hoverFaceVao_.release();
            program_.setUniformValue("isHoverFacePass", false);
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
        }
        if (!hoverEdgeSegments_.empty()) {
            std::vector<float> edgeData;
            edgeData.reserve(hoverEdgeSegments_.size() * 3);
            for (const auto& v : hoverEdgeSegments_) {
                edgeData.push_back(v.x());
                edgeData.push_back(v.y());
                edgeData.push_back(v.z());
            }
            hoverEdgeVbo_.bind();
            hoverEdgeVbo_.allocate(edgeData.data(), static_cast<int>(edgeData.size() * sizeof(float)));
            hoverEdgeVbo_.release();

            glDisable(GL_DEPTH_TEST);
            program_.setUniformValue("isLinePass", true);
            program_.setUniformValue("markerColor", QVector3D(1.0f, 0.55f, 0.0f)); // 밝은 주황
            glLineWidth(4.0f);
            hoverEdgeVao_.bind();
            glDrawArrays(GL_LINES, 0, static_cast<int>(hoverEdgeSegments_.size()));
            hoverEdgeVao_.release();
            program_.setUniformValue("isLinePass", false);
            glEnable(GL_DEPTH_TEST);
        }
    }

    vao_.release();
    program_.release();

    // § 치수선 라벨 안 보임 버그 수정(2026-09-12) - "노란색 박스에는 아무것도 보이지
    // 않아"(텍스트가 안 그려짐) 리포트. 위 마커/화살표 패스들이 GL_DEPTH_TEST를 다시
    // 켜둔 채로 끝나는데(4/5/6단계 각각 마지막에 glEnable(GL_DEPTH_TEST)), 그 상태로
    // beginNativePainting을 나가면 이후 QPainter가 그리는 2D 도형(라벨 배경/텍스트)이
    // 3D 패스가 남긴 깊이버퍼값에 의해 부분적으로 가려질 수 있다(Qt 공식 문서가 경고하는
    // "GL 상태를 정리하지 않고 endNativePainting 호출" 문제). endNativePainting 직전에
    // 명시적으로 꺼서 이후 2D 오버레이가 항상 위에 그려지게 한다.
    glDisable(GL_DEPTH_TEST);

    painter.endNativePainting();

    // --- 7) 치수선 중앙 라벨 - § "검은 박스 정리하고, 치수 표시선 중앙에 Point(부위)
    // 이니셜을 노란 배경+볼드로 잘 보이게"라는 요청. 마커별 라벨(예전 A/B) 대신 치수선
    // 중점 하나에만 그린다.
    // § 치수선 드래그 - 이번 프레임에 라벨을 실제로 그렸는지에 따라 히트테스트용 사각형을
    // 갱신(못 그렸으면 빈 사각형으로 비워서 드래그 시작을 막는다 - mousePressEvent 참고).
    lastLabelScreenRect_ = QRectF();
    if (hasDimensionLine_ && !dimensionLabel_.isEmpty()) {
        painter.setRenderHint(QPainter::Antialiasing);
        QFont font = painter.font();
        font.setBold(true);
        font.setPointSize(12);
        painter.setFont(font);
        const QFontMetrics fm(font);
        const QMatrix4x4 viewProj = projection * modelView;
        const QVector4D clip = viewProj * QVector4D(dimensionLineMidpoint_, 1.0f);
        if (clip.w() > 0.0001f) {
            const float ndcX = clip.x() / clip.w();
            const float ndcY = clip.y() / clip.w();
            const float screenX = (ndcX * 0.5f + 0.5f) * static_cast<float>(width());
            const float screenY = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(height());

            const QRect textRect = fm.boundingRect(dimensionLabel_);
            constexpr int kPadX = 8;
            constexpr int kPadY = 4;
            const QRectF box(
                screenX - (textRect.width() + kPadX * 2) / 2.0, screenY - (textRect.height() + kPadY * 2) / 2.0,
                textRect.width() + kPadX * 2, textRect.height() + kPadY * 2);
            painter.setPen(QPen(QColor(120, 96, 0), 1.5));
            painter.setBrush(QColor(255, 221, 51)); // 노란 배경
            painter.drawRoundedRect(box, 4.0, 4.0);
            painter.setPen(QColor(40, 30, 0));
            painter.drawText(box, Qt::AlignCenter, dimensionLabel_);
            lastLabelScreenRect_ = box;
        }
    }

    // --- 8) 좌표계 기즈모(2026-09-12, NX 스타일로 재작업) - "너무 못생겼다, NX 프로그램
    // 참고해서 동일하게 반영해달라"는 요청. NX의 좌하단 WCS 트라이어드는 배경 원판이나
    // 채워진 동그라미 없이, 원점에서 뻗어나가는 가는 축선 + 끝의 작은 삼각형 화살촉 +
    // 축 옆 글자만으로 구성된다. 배경 원/채워진 점을 없애고 그 구성을 그대로 따랐다.
    {
        painter.setRenderHint(QPainter::Antialiasing);
        constexpr float kAxisLength = 36.0f;
        constexpr float kArrowLen = 9.0f;
        constexpr float kArrowHalfWidth = 3.5f;
        constexpr float kMargin = 44.0f;
        const QPointF anchor(kMargin, static_cast<float>(height()) - kMargin);

        struct AxisDef {
            QVector3D dir;
            QColor color;
            QString label;
        };
        // NX 관례색: X=빨강, Y=초록, Z=파랑.
        const AxisDef axes[3] = {
            {QVector3D(1, 0, 0), QColor(200, 40, 40), "X"},
            {QVector3D(0, 1, 0), QColor(40, 150, 50), "Y"},
            {QVector3D(0, 0, 1), QColor(40, 90, 210), "Z"},
        };

        // 원점(작은 점) - 세 축이 만나는 기준점만 표시, NX도 원점에 작은 사각/점을 둔다.
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(70, 70, 70));
        painter.drawEllipse(anchor, 2.0, 2.0);

        // 카메라에 더 가까운(view-space z가 큰) 축을 나중에 그려서 겹칠 때 위에 보이게.
        int order[3] = {0, 1, 2};
        std::sort(order, order + 3, [&](int a, int b) {
            return modelView.mapVector(axes[a].dir).z() < modelView.mapVector(axes[b].dir).z();
        });

        QFont axisFont = painter.font();
        axisFont.setBold(true);
        axisFont.setPointSize(9);
        painter.setFont(axisFont);

        for (int idx : order) {
            const AxisDef& ax = axes[idx];
            const QVector3D viewDir = modelView.mapVector(ax.dir).normalized();
            // 화면 좌표는 Y가 아래로 증가하므로 부호를 뒤집는다(다른 화면좌표 계산과 동일 관례).
            QPointF dir2D(viewDir.x(), -viewDir.y());
            const double len2D = std::hypot(dir2D.x(), dir2D.y());
            if (len2D > 1e-4) {
                dir2D /= len2D; // 화면 평면에 투영된 방향(정규화) - 축이 카메라를 거의
                                // 정면으로 향하면(len2D≈0) 이전 방향을 그대로 재사용.
            }
            const QPointF perp2D(-dir2D.y(), dir2D.x());
            const QPointF tip = anchor + dir2D * kAxisLength;
            const QPointF arrowBase = tip - dir2D * kArrowLen;

            QPen linePen(ax.color, 1.6);
            painter.setPen(linePen);
            painter.drawLine(anchor, arrowBase);

            const QPolygonF arrowHead({
                tip,
                arrowBase + perp2D * kArrowHalfWidth,
                arrowBase - perp2D * kArrowHalfWidth,
            });
            painter.setPen(Qt::NoPen);
            painter.setBrush(ax.color);
            painter.drawPolygon(arrowHead);

            painter.setPen(ax.color);
            const QPointF labelPos = tip + dir2D * 11.0;
            painter.drawText(QRectF(labelPos.x() - 8, labelPos.y() - 8, 16, 16), Qt::AlignCenter, ax.label);
        }
    }
}

void ModelViewport::mousePressEvent(QMouseEvent* event) {
    lastMousePos_ = event->pos();
    // 단축키(F/H/I/X/Y/Z/+/-)는 이제 이 위젯의 키보드 포커스가 아니라 MultiViewportPanel의
    // "마우스가 올라가 있는 뷰포트"(hoverEntered) 기준 QShortcut으로 처리한다 - 컨트롤 바를
    // 클릭해서 포커스가 다른 위젯으로 넘어가도 단축키가 계속 먹는다(사용자 리포트: 포커스
    // 기반 방식일 때 F/H/I가 전혀 안 먹었음). 그래도 시각적 포커스 표시는 자연스럽도록 유지.
    setFocus(Qt::MouseFocusReason);

    // § 3D 클릭 피킹 - 피킹 모드 중이면 좌클릭이 회전을 시작하지 않고 광선만 쏜다.
    // 정밀하게 면을 짚어야 하는데 살짝 흔들려도 회전이 같이 걸리면 방해가 되므로,
    // 피킹 모드 동안은 좌클릭 회전을 완전히 잠근다(mouseMoveEvent에서도 동일하게 체크).
    if (pickModeActive_ && *pickModeActive_ && event->button() == Qt::LeftButton) {
        QVector3D rayOrigin;
        QVector3D rayDir;
        if (computeSectionRay(rayOrigin, rayDir)) {
            emit facePicked(handle_, rayOrigin, rayDir);
        }
        return;
    }

    // § 치수선 드래그(2026-09-13) - "사용자가 드래그해서 위치 조정도 가능하게 해달라"는
    // 요청. 라벨(노란 박스)을 직접 클릭했을 때만 드래그를 시작한다 - 카메라 잠금 여부와
    // 무관하게 항상 동작(의도적 조작이라 카메라 잠금과 별개). 오프셋 축을 모르면
    // (hasDimensionOffsetAxis_==false, 예: 수동 피킹 포인트는 오프셋이 없음) 라벨을
    // 클릭해도 드래그가 시작되지 않는다.
    if (event->button() == Qt::LeftButton && hasDimensionOffsetAxis_ &&
        lastLabelScreenRect_.contains(event->pos())) {
        draggingDimensionOffset_ = true;
        return;
    }

    // § 캡쳐 영역 드래그 지정 - 이 모드 중엔 좌클릭 드래그가 회전 대신 고무줄 사각형을
    // 그린다(mouseMoveEvent/mouseReleaseEvent에서 이어짐). 피킹 모드와 같은 "이 모드
    // 동안은 좌클릭 회전을 완전히 잠근다" 패턴.
    if (captureRegionModeActive_ && *captureRegionModeActive_ && event->button() == Qt::LeftButton) {
        captureDragStart_ = event->pos();
        if (!rubberBand_) {
            rubberBand_ = new QRubberBand(QRubberBand::Rectangle, this);
        }
        rubberBand_->setGeometry(QRect(captureDragStart_, QSize()));
        rubberBand_->show();
        return;
    }

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
    // 독립 조작 모드(§ 화면설정)면 sharedCamera_가 아니라 이 뷰포트의 localCamera_만 바뀐다.
    if (pickModeActive_ && *pickModeActive_) {
        // § 피킹 모드 중 뷰어 조작 허용(2026-09-13) - "측정 부위를 지정할 때도 화면
        // 뷰어를 마우스로 조작할 수 있게 해달라"는 요청. 예전엔 피킹 모드 동안 좌클릭
        // 드래그가 완전히 막혀 있어서(정밀 클릭 방해 방지 의도) 찍기 전에 각도를 다시
        // 잡고 싶어도 피킹을 잠깐 꺼야 했다. 좌클릭 피킹 자체는 mousePressEvent에서
        // "누르는 순간" 이미 끝나므로, 그 뒤 버튼을 유지한 채 드래그가 이어지면 회전/팬
        // 으로 이어받아도 피킹 결과와는 충돌하지 않는다 - 휠 줌은 원래도 피킹 모드와
        // 무관하게 항상 동작했다(wheelEvent). 카메라 잠금 중이면 기존처럼 무시.
        if (!(cameraLockedPtr_ && *cameraLockedPtr_) && (event->buttons() & (Qt::LeftButton | Qt::RightButton))) {
            Camera* transform = ActiveTransform();
            if (event->buttons() & Qt::LeftButton) {
                transform->yawDeg += delta.x() * 0.5f;
                transform->pitchDeg += delta.y() * 0.5f;
            } else {
                transform->panX += delta.x() * 1.0f;
                transform->panY -= delta.y() * 1.0f;
            }
            emit cameraChanged();
            update();
            return;
        }

        // § 라이브 호버 미리보기(2026-09-11) - "마우스가 근처에 갔을 때 먼저 주황색으로
        // 가이드가 보이면 좋겠다"는 요청. 클릭(mousePressEvent)과 완전히 같은 광선
        // 계산으로 PickFace를 미리 불러보고, 결과를 paintGL이 옅은 주황 점으로 그린다 -
        // 실제 클릭 전에 "여기를 찍게 된다"를 미리 확인할 수 있게.
        QVector3D rayOrigin;
        QVector3D rayDir;
        hoverPickValid_ = false;
        hoverFaceTriangles_.clear();
        hoverEdgeSegments_.clear();
        if (computeSectionRay(rayOrigin, rayDir)) {
            const geometry::Vec3 origin{rayOrigin.x(), rayOrigin.y(), rayOrigin.z()};
            const geometry::Vec3 dir{rayDir.x(), rayDir.y(), rayDir.z()};
            const geometry::PickResult result = adapter_->PickFace(handle_, origin, dir);
            if (result.kind != geometry::PickedFaceKind::None) {
                // § 면/선/점 하이라이트 배타적 구분(2026-09-13) - "면만 표시되고, 선만
                // 표시되고, 포인트만도 표시되게 해달라"는 요청. 세 범주가 서로 겹치지
                // 않게: 면/원통 히트는 면 하이라이트만(점 없음 - kind!=Point라 자동으로
                // 제외됨), 모서리 중간점은 그 모서리 선만(점 없음 - pointSubKind로
                // 명시적 제외), 나머지 점 종류(시작점/끝점/중앙점/교차점)는 점만(선 없음 -
                // StepGeometryAdapter가 이 경우 highlightEdgeSegments를 아예 안 채움).
                hoverPickValid_ = result.kind == geometry::PickedFaceKind::Point &&
                    result.pointSubKind != geometry::PointSubKind::EdgeMidpoint;
                hoverPickPoint_ = QVector3D(
                    static_cast<float>(result.point.x), static_cast<float>(result.point.y),
                    static_cast<float>(result.point.z));

                // § 하이라이트 미리보기(2026-09-13) - "면/포인트/EDGE 근처에 가면 그
                // 패턴을 통째로 주황색으로 하이라이트해달라"는 요청. 면이면 그 면 전체
                // 삼각형을, 모서리 중간점 스냅이면 그 모서리 전체 선분을 CPU 쪽에 들고
                // 있다가 paintGL이 실제 GL 컨텍스트 안에서 업로드한다(hoverVbo_와 같은
                // 패턴 - mouseMoveEvent 시점엔 GL 컨텍스트가 current가 아니라서
                // makeCurrent() 없이 직접 VBO를 만지면 안 된다). 둘 다 비어있으면
                // 기존처럼 점만 표시(예: 시작점/끝점/중앙점/교차점 스냅).
                hoverFaceTriangles_.reserve(result.highlightTriangles.size());
                for (const auto& v : result.highlightTriangles) {
                    hoverFaceTriangles_.emplace_back(
                        static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
                }
                hoverEdgeSegments_.reserve(result.highlightEdgeSegments.size());
                for (const auto& v : result.highlightEdgeSegments) {
                    hoverEdgeSegments_.emplace_back(
                        static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
                }
            }
        }
        update();
        return;
    }
    // § 캡쳐 영역 드래그 지정 - 회전 대신 고무줄 사각형 크기만 갱신.
    if (captureRegionModeActive_ && *captureRegionModeActive_) {
        if (rubberBand_ && rubberBand_->isVisible()) {
            rubberBand_->setGeometry(QRect(captureDragStart_, event->pos()).normalized());
        }
        return;
    }
    // § 치수선 드래그 - mousePressEvent에서 라벨을 클릭해 시작된 드래그. 화면 이동량
    // (delta, 논리 픽셀)을 오프셋 축의 "화면상 투영 방향"에 내적해서 그 축 방향으로 얼마나
    // 움직였는지(픽셀)를 뽑아내고, 직교 투영의 픽셀당 월드 단위(현재 distance 기준, 렌더링에
    // 쓰는 것과 같은 kHalfFovTan)로 환산해 델타를 흘려보낸다. 카메라는 절대 건드리지 않는다.
    if (draggingDimensionOffset_) {
        if (height() > 0) {
            const QMatrix4x4 modelView = ActiveTransform()->ViewMatrix();
            const QVector3D viewDir = modelView.mapVector(dimensionOffsetAxisWorld_);
            QVector2D screenDir(viewDir.x(), -viewDir.y()); // 화면 좌표는 Y가 아래로 증가(다른 계산과 동일 관례)
            if (screenDir.length() > 1e-4f) {
                screenDir.normalize();
                const float pixelAlongAxis =
                    static_cast<float>(delta.x()) * screenDir.x() + static_cast<float>(delta.y()) * screenDir.y();
                const float worldPerPixel =
                    (2.0f * ActiveTransform()->distance * kHalfFovTan) / static_cast<float>(height());
                emit dimensionOffsetDragged(pixelAlongAxis * worldPerPixel);
            }
        }
        return;
    }
    // § 뷰어 카메라 잠금 - "캡쳐 전후 카메라 on/off" 요청. 잠겨 있으면 회전/팬을 그냥
    // 무시한다(피킹은 위에서 이미 처리하고 return했으므로 영향 없음).
    if (cameraLockedPtr_ && *cameraLockedPtr_) {
        return;
    }
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

void ModelViewport::mouseReleaseEvent(QMouseEvent* event) {
    // § 치수선 드래그 - 버튼을 떼면 드래그 종료.
    if (event->button() == Qt::LeftButton && draggingDimensionOffset_) {
        draggingDimensionOffset_ = false;
        return;
    }

    // § 캡쳐 영역 드래그 지정 - 뗀 순간 고무줄 사각형만큼 grabFramebuffer()로 통째로
    // 찍은 뒤 그 영역으로 잘라서 내보낸다.
    //
    // 실제 원인(검은 화면 버그) - rubberBand_->geometry()는 위젯의 "논리(DPI-독립)
    // 좌표"인데, grabFramebuffer()가 돌려주는 QImage는 "실제 장치 픽셀" 크기다
    // (예: 배율 150%면 실제 픽셀이 논리 좌표의 1.5배). 이 둘을 안 맞추고 그대로
    // QPixmap::copy(region)에 넘기면 고배율 화면에서는 실제 그려진 영역보다 훨씬
    // 작고 엉뚱한 좌표를 잘라내서 대부분 배경(클리어 컬러)만 걸린다 - "일반 캡쳐"
    // (자르기 없이 전체를 그대로 쓰는 grabActiveViewport)는 멀쩡했던 이유가 바로 이
    // 배율 불일치가 자르기에서만 문제가 됐기 때문. devicePixelRatioF()를 곱해
    // 논리 좌표를 장치 픽셀 좌표로 변환한 뒤 QImage::copy()(픽셀 좌표 기준, 배율
    // 개념 자체가 없어 모호함이 없음)로 잘라낸다.
    if (captureRegionModeActive_ && *captureRegionModeActive_ && event->button() == Qt::LeftButton &&
        rubberBand_) {
        const QRect region = rubberBand_->geometry();
        rubberBand_->hide();
        if (region.width() >= 4 && region.height() >= 4) {
            const qreal dpr = devicePixelRatioF();
            const QRect deviceRegion(
                QPoint(qRound(region.left() * dpr), qRound(region.top() * dpr)),
                QSize(qRound(region.width() * dpr), qRound(region.height() * dpr)));
            const QImage full = grabFramebuffer();
            QImage cropped = full.copy(deviceRegion.intersected(full.rect()));
            // full.copy()는 devicePixelRatio 메타데이터를 안 넘겨준다(기본값 1.0) - 안
            // 맞춰주면 이미지 위 클릭 좌표 역산(§ 캡쳐된 이미지 위 클릭 피킹)과 미리보기
            // 크기 계산이 고배율 화면에서 다시 어긋난다.
            cropped.setDevicePixelRatio(dpr);
            emit captureRegionGrabbed(QPixmap::fromImage(cropped), captureInfo(region.topLeft()));
        }
    }
}

QPixmap ModelViewport::grabViewportPixmap() {
    QImage image = grabFramebuffer();
    image.setDevicePixelRatio(devicePixelRatioF());
    return QPixmap::fromImage(image);
}

void ModelViewport::wheelEvent(QWheelEvent* event) {
    // § 뷰어 카메라 잠금 - 잠겨 있으면 휠 줌도 무시.
    if (cameraLockedPtr_ && *cameraLockedPtr_) {
        return;
    }
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
    // 독립 조작 모드에서도 "지금 화면에 실제로 보이는 각도" 기준으로 광선을 쏴야 클릭
    // 위치와 일치하므로 ActiveTransform() 사용 (섹션 결과 자체는 sharedCamera_에 쓴다).
    return ComputeRayForView(*ActiveTransform(), width(), height(), QPointF(lastMousePos_), outOrigin, outDir);
}

// static - 라이브 위젯 상태(ActiveTransform()/width()/height())가 아니라 임의의 카메라
// 스냅샷+크기를 받는 버전. computeSectionRay()와 CaptureInfo 기반 이미지 클릭 피킹이
// 이 함수 하나를 공유한다.
bool ModelViewport::ComputeRayForView(const Camera& camera, int viewportWidth, int viewportHeight,
                                       const QPointF& logicalPos, QVector3D& outOrigin, QVector3D& outDir) {
    if (viewportWidth <= 0 || viewportHeight <= 0) {
        return false;
    }
    const float aspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
    const QMatrix4x4 projection = BuildProjectionMatrix(camera.distance, aspect);
    bool invertible = false;
    const QMatrix4x4 inverseViewProj = (projection * camera.ViewMatrix()).inverted(&invertible);
    if (!invertible) {
        return false;
    }

    const float ndcX = (2.0f * static_cast<float>(logicalPos.x()) / static_cast<float>(viewportWidth)) - 1.0f;
    const float ndcY = 1.0f - (2.0f * static_cast<float>(logicalPos.y()) / static_cast<float>(viewportHeight));
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

ModelViewport::CaptureInfo ModelViewport::captureInfo(QPoint regionOffset) const {
    CaptureInfo info;
    info.camera = *ActiveTransform();
    info.viewportWidth = width();
    info.viewportHeight = height();
    info.regionOffset = regionOffset;
    info.handle = handle_;
    return info;
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
