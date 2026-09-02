#pragma once

#include <QMatrix4x4>

namespace viewer {

// 화면 설정 탭의 "전체 Solid / Solid-Edge / Wireframe" 모드. 뷰포트 렌더링 스타일이라
// 카메라 자체와는 무관하지만, Camera와 마찬가지로 모든 뷰포트가 공유하는 전역 표시
// 설정이라 여기 같이 둔다(§ 화면설정 - 화면 모드).
enum class RenderMode {
    Solid,      // 면 채우기만 (엣지 오버레이 없음)
    SolidEdge,  // 면 채우기 + 삼각형 엣지 오버레이 (기존 기본값, NX 경량화 표시 스타일)
    Wireframe,  // 엣지만 (면 채우기 없음)
};

// 여러 ModelViewport가 포인터로 공유하는 카메라 상태.
// 하나를 마우스로 조작하면 전부 같은 값을 참조하게 되어 동기 회전/확대/이동이 성립한다.
// (개발계획_v2.md §10 "동기 회전/동기 확대")
// 단, "동시 조작"이 꺼져있으면(§ 화면설정 - 뷰포트 조작 모드) 각 ModelViewport는 이
// 공유 인스턴스 대신 자기 소유의 로컬 Camera 사본으로 회전/팬/줌만 독립적으로 관리한다
// (섹션 뷰/렌더모드는 그 경우에도 항상 이 공유 인스턴스 기준 - ModelViewport 주석 참고).
struct Camera {
    float yawDeg = 30.0f;
    float pitchDeg = -20.0f;
    float distance = 2500.0f; // mm
    float panX = 0.0f;
    float panY = 0.0f;

    // 단축키 H(섹션 뷰) 상태. 월드좌표(§5 절대좌표 정렬 전제)에서 축=상수 평면으로 자르므로,
    // 카메라와 함께 공유되면 인치가 달라도 "같은 위치"에서 동시에 잘린 단면을 볼 수 있다.
    bool sectionEnabled = false;
    int sectionAxis = 0;       // 0=X, 1=Y, 2=Z
    float sectionCoord = 0.0f; // mm, 절단 평면의 축 좌표
    float sectionSign = 1.0f;  // +1/-1, 카메라 쪽 절반을 잘라내는 방향

    RenderMode renderMode = RenderMode::SolidEdge;

    QMatrix4x4 ViewMatrix() const {
        QMatrix4x4 m;
        m.translate(panX, panY, -distance);
        m.rotate(pitchDeg, 1.0f, 0.0f, 0.0f);
        m.rotate(yawDeg, 0.0f, 1.0f, 0.0f);
        return m;
    }
};

} // namespace viewer
