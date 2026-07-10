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
};

} // namespace geometry
