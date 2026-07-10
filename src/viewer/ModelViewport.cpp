#include "viewer/ModelViewport.h"

#include <QMouseEvent>
#include <QVector3D>
#include <QWheelEvent>

#include <vector>

namespace viewer {

namespace {

constexpr int kEdges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, // 아래면
    {4, 5}, {5, 6}, {6, 7}, {7, 4}, // 윗면
    {0, 4}, {1, 5}, {2, 6}, {3, 7}  // 수직 기둥
};

const char* kVertexShaderSrc = R"(
#version 330 core
layout(location = 0) in vec3 position;
uniform mat4 mvp;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
}
)";

const char* kFragmentShaderSrc = R"(
#version 330 core
out vec4 fragColor;
uniform vec3 color;
void main() {
    fragColor = vec4(color, 1.0);
}
)";

} // namespace

ModelViewport::ModelViewport(geometry::IGeometryAdapter* adapter, geometry::ModelHandle handle,
                              Camera* sharedCamera, QWidget* parent)
    : QOpenGLWidget(parent), adapter_(adapter), handle_(handle), camera_(sharedCamera) {
}

ModelViewport::~ModelViewport() {
    makeCurrent();
    vbo_.destroy();
    doneCurrent();
}

void ModelViewport::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.15f, 0.15f, 0.17f, 1.0f);

    program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShaderSrc);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShaderSrc);
    program_.link();

    const auto vertices = adapter_->GetVertices(handle_);
    std::vector<float> lineVertices;
    if (vertices.size() >= 8) {
        lineVertices.reserve(12 * 2 * 3);
        for (const auto& edge : kEdges) {
            const auto& a = vertices[edge[0]];
            const auto& b = vertices[edge[1]];
            lineVertices.push_back(static_cast<float>(a.x));
            lineVertices.push_back(static_cast<float>(a.y));
            lineVertices.push_back(static_cast<float>(a.z));
            lineVertices.push_back(static_cast<float>(b.x));
            lineVertices.push_back(static_cast<float>(b.y));
            lineVertices.push_back(static_cast<float>(b.z));
        }
    }
    vertexCount_ = static_cast<int>(lineVertices.size() / 3);

    vao_.create();
    vao_.bind();

    vbo_.create();
    vbo_.bind();
    vbo_.allocate(lineVertices.data(), static_cast<int>(lineVertices.size() * sizeof(float)));

    program_.bind();
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0, 3);

    vao_.release();
    vbo_.release();
    program_.release();
}

void ModelViewport::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void ModelViewport::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (vertexCount_ == 0) {
        return;
    }

    const float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;

    QMatrix4x4 projection;
    projection.perspective(45.0f, aspect, 1.0f, 20000.0f);

    const QMatrix4x4 mvp = projection * camera_->ViewMatrix();

    program_.bind();
    program_.setUniformValue("mvp", mvp);
    program_.setUniformValue("color", QVector3D(0.4f, 0.8f, 1.0f));

    vao_.bind();
    glDrawArrays(GL_LINES, 0, vertexCount_);
    vao_.release();
    program_.release();
}

void ModelViewport::mousePressEvent(QMouseEvent* event) {
    lastMousePos_ = event->pos();
}

void ModelViewport::mouseMoveEvent(QMouseEvent* event) {
    const QPoint delta = event->pos() - lastMousePos_;
    lastMousePos_ = event->pos();

    if (event->buttons() & Qt::LeftButton) {
        camera_->yawDeg += delta.x() * 0.5f;
        camera_->pitchDeg += delta.y() * 0.5f;
    } else if (event->buttons() & Qt::MiddleButton) {
        camera_->panX += delta.x() * 1.0f;
        camera_->panY -= delta.y() * 1.0f;
    } else {
        return;
    }
    emit cameraChanged();
    update();
}

void ModelViewport::wheelEvent(QWheelEvent* event) {
    camera_->distance *= (event->angleDelta().y() > 0) ? 0.9f : 1.1f;
    emit cameraChanged();
    update();
}

} // namespace viewer
