#pragma once

#include <string>
#include <vector>

namespace geometry {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct BoundingBox {
    Vec3 min;
    Vec3 max;
};

// Rule의 Anchor(§7)에 대응하는 실제 지오메트리 후보 하나. Selector가 여러 후보 중
// 하나(또는 pair)를 고르는 대상이 된다.
struct AnchorCandidate {
    std::string anchorType; // Hole, Boss_Center, Hook_Tip_Edge, ...
    std::string partName;   // Bezel, Rear_Chassis, ...
    Vec3 position;
};

// point_to_plane 측정의 기준 평면 후보. 평면 위 한 점 + 법선벡터로 표현.
struct PlaneCandidate {
    std::string planeType; // Datum_Plane 등
    std::string partName;
    Vec3 pointOnPlane;
    Vec3 normal;
};

// face_to_face_gap 측정용 면 후보. Phase4b 초안: 실제 면 테셀레이션이 아니라
// 중심점 + 법선벡터로 근사한다 (面 형상 전체가 아닌 대표점 기준 gap 계산).
// 회사PC에서 실제 JT 테셀레이션 데이터가 들어오면 최단거리 탐색으로 교체 필요.
struct FaceCandidate {
    std::string faceType; // Rib_Top_Surface, Boss_Outer_Wall, ...
    std::string partName;
    Vec3 center;
    Vec3 normal;
};

using ModelHandle = int;
constexpr ModelHandle kInvalidModelHandle = -1;

// Core/Viewer는 이 인터페이스만 알고, 실제 구현(Mock 또는 NX/JT)은 모른다.
// 개인PC: MockGeometryAdapter, 회사PC: 실제 JT/NX 연동 어댑터로 교체.
class IGeometryAdapter {
public:
    virtual ~IGeometryAdapter() = default;

    virtual ModelHandle LoadModel(const std::string& filePath) = 0;
    virtual BoundingBox GetBoundingBox(ModelHandle handle) const = 0;
    virtual std::vector<Vec3> GetVertices(ModelHandle handle) const = 0;

    // anchorType + partName으로 후보를 찾는다. Selector가 이 중에서 골라낸다.
    virtual std::vector<AnchorCandidate> FindAnchorCandidates(
        ModelHandle handle, const std::string& anchorType, const std::string& partName) const = 0;

    virtual std::vector<PlaneCandidate> FindPlaneCandidates(
        ModelHandle handle, const std::string& planeType, const std::string& partName) const = 0;

    virtual std::vector<FaceCandidate> FindFaceCandidates(
        ModelHandle handle, const std::string& faceType, const std::string& partName) const = 0;

    // 뷰어가 실제 렌더링에 쓰는 삼각형 메시. 연속된 3개 Vec3가 삼각형 1개(flat 셰이딩,
    // 별도 법선 데이터 없이 화면공간 미분(dFdx/dFdy)으로 면 법선을 계산). 실 형상
    // 테셀레이션이 없는 어댑터(Mock, 미구현 NxJt)를 위해 바운딩박스 12삼각형 상자로
    // 기본 구현을 제공한다 — StepGeometryAdapter는 OCCT 테셀레이션 결과로 override.
    virtual std::vector<Vec3> GetRenderTriangles(ModelHandle handle) const {
        const BoundingBox box = GetBoundingBox(handle);
        const Vec3& lo = box.min;
        const Vec3& hi = box.max;
        const Vec3 corners[8] = {
            {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
            {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
        };
        constexpr int kFaces[6][4] = {
            {0, 1, 2, 3}, {4, 5, 6, 7}, // 아래/윗면
            {0, 1, 5, 4}, {2, 3, 7, 6}, // 앞/뒷면
            {1, 2, 6, 5}, {3, 0, 4, 7}, // 옆면 2개
        };
        std::vector<Vec3> triangles;
        triangles.reserve(6 * 6);
        for (const auto& face : kFaces) {
            triangles.push_back(corners[face[0]]);
            triangles.push_back(corners[face[1]]);
            triangles.push_back(corners[face[2]]);
            triangles.push_back(corners[face[0]]);
            triangles.push_back(corners[face[2]]);
            triangles.push_back(corners[face[3]]);
        }
        return triangles;
    }
};

} // namespace geometry
