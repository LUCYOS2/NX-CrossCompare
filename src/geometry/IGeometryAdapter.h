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
};

} // namespace geometry
