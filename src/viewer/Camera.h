#pragma once

#include <QMatrix4x4>

namespace viewer {

// 여러 ModelViewport가 포인터로 공유하는 카메라 상태.
// 하나를 마우스로 조작하면 전부 같은 값을 참조하게 되어 동기 회전/확대/이동이 성립한다.
// (개발계획_v2.md §10 "동기 회전/동기 확대")
struct Camera {
    float yawDeg = 30.0f;
    float pitchDeg = -20.0f;
    float distance = 2500.0f; // mm
    float panX = 0.0f;
    float panY = 0.0f;

    QMatrix4x4 ViewMatrix() const {
        QMatrix4x4 m;
        m.translate(panX, panY, -distance);
        m.rotate(pitchDeg, 1.0f, 0.0f, 0.0f);
        m.rotate(yawDeg, 0.0f, 1.0f, 0.0f);
        return m;
    }
};

} // namespace viewer
