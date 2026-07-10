#pragma once

#include "geometry/IGeometryAdapter.h"

namespace geometry {

struct AlignmentResult {
    bool aligned = false;
    Vec3 center;
    double offsetMm = 0.0;
};

// 어셈블리 중심(bounding box center)이 절대좌표 원점 근처인지 검증한다.
// 개발계획_v2.md §5 절대좌표 중심정렬 전제를 모델 입고 시마다 자동으로 확인하기 위한 QC.
// 축방향(X/Y/Z 컨벤션) 검증은 Mock 데이터로는 의미 있게 재현할 수 없어
// 실제 어댑터가 축 정보를 제공하는 Phase4(회사PC)로 보류한다.
AlignmentResult CheckCenterAlignment(const BoundingBox& box, double toleranceMm);

} // namespace geometry
