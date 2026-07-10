#include "geometry/AlignmentCheck.h"

#include <cmath>

namespace geometry {

AlignmentResult CheckCenterAlignment(const BoundingBox& box, double toleranceMm) {
    const Vec3 center{
        (box.min.x + box.max.x) / 2.0,
        (box.min.y + box.max.y) / 2.0,
        (box.min.z + box.max.z) / 2.0
    };
    const double offset = std::sqrt(center.x * center.x + center.y * center.y + center.z * center.z);

    AlignmentResult result;
    result.center = center;
    result.offsetMm = offset;
    result.aligned = offset <= toleranceMm;
    return result;
}

} // namespace geometry
