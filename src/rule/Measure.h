#pragma once

#include "geometry/IGeometryAdapter.h"

#include <string>

namespace rule {

// face_to_face_gap(면 테셀레이션 기반 최단거리)만 Phase4b로 남겨두고,
// point 기반 측정 3종(point_to_point/point_to_plane/axis_projection)을 구현한다.

double ComputePointToPointDistance(const geometry::Vec3& a, const geometry::Vec3& b);

// axis는 "X" | "Y" | "Z" 중 하나.
double ComputeAxisProjectionDistance(const geometry::Vec3& a, const geometry::Vec3& b, const std::string& axis);

// normal은 단위벡터라고 가정하지 않고 내부에서 정규화한다.
double ComputePointToPlaneDistance(
    const geometry::Vec3& point, const geometry::Vec3& pointOnPlane, const geometry::Vec3& normal);

} // namespace rule
