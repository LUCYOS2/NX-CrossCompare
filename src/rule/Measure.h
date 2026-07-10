#pragma once

#include "geometry/IGeometryAdapter.h"

namespace rule {

// point_to_point 측정만 최소 구현. 나머지 측정 타입(point_to_plane, axis_projection,
// face_to_face_gap)은 Phase4/4b에서 Reference Frame·Selector 매칭과 함께 구현한다.
double ComputePointToPointDistance(const geometry::Vec3& a, const geometry::Vec3& b);

} // namespace rule
