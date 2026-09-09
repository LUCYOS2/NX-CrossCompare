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
    // 원통면(Hole/Boss_Center) 후보의 지름(mm). 평면/edge 기반 등 원통이 아닌 타입은 0.
    // Rule의 Anchor.paramKey=="diameter"일 때 후보를 걸러내는 데 쓰인다(RuleEngine 참고) -
    // "비슷한 지름의 구멍만 다 찾기" 요청에 대응하는 필드.
    double diameterMm = 0.0;
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

// § 3D 클릭 피킹(2026-09-09) - 텍스트로 anchor_type/지름을 타이핑하는 대신, 뷰어에서
// 마우스로 실제 면을 클릭해 그 자리에서 형상을 판별한다. kind는 지금은 원통("Cylinder")과
// 평면("Plane") 두 가지만 구분 - 후크처럼 여러 면이 합쳐진 복합 형상 인식(§ 대화 기록,
// "①단계"로 범위를 좁힌 부분)은 원격PC에서 실제 후크 샘플이 생기면 다음 단계로 확장.
enum class PickedFaceKind {
    None,     // 아무것도 안 맞음(광선이 형상을 비껴감)
    Cylinder, // FindAnchorCandidates의 Hole/Boss_Center와 같은 판별 기준
    Plane,    // FindPlaneCandidates의 Datum_Plane과 같은 판별 기준
};

struct PickResult {
    PickedFaceKind kind = PickedFaceKind::None;
    Vec3 point;           // 광선이 면과 만난 지점(원통이면 축 위 가장 가까운 점 - AnchorCandidate.position과 동일 기준)
    Vec3 normal;          // Plane일 때만 의미 있음
    double diameterMm = 0.0; // Cylinder일 때만 의미 있음
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

    // § 3D 클릭 피킹 - 월드좌표 광선(origin+dir)과 형상의 실제 면 사이 정확한 교차를 구해
    // 어떤 면인지 판별한다. 기본 구현은 "아무것도 못 찾음"을 반환 - 실 형상 B-rep이 없는
    // 어댑터(Mock, 미구현 NxJt)는 이 자체가 의미 없어서 override를 강제하지 않는다.
    // StepGeometryAdapter만 OCCT로 override.
    virtual PickResult PickFace(ModelHandle handle, const Vec3& rayOrigin, const Vec3& rayDir) const {
        (void)handle;
        (void)rayOrigin;
        (void)rayDir;
        return PickResult{};
    }

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
