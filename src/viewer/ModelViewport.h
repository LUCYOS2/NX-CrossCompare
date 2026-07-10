#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>

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

signals:
    void cameraChanged();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    geometry::IGeometryAdapter* adapter_;
    geometry::ModelHandle handle_;
    Camera* camera_;
    QPoint lastMousePos_;

    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLShaderProgram program_;
    int vertexCount_ = 0;
};

} // namespace viewer
