#include "rule/Measure.h"

#include <cmath>

namespace rule {

double ComputePointToPointDistance(const geometry::Vec3& a, const geometry::Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace rule
