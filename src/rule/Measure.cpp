#include "rule/Measure.h"

#include <cmath>
#include <stdexcept>

namespace rule {

double ComputePointToPointDistance(const geometry::Vec3& a, const geometry::Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double ComputeAxisProjectionDistance(const geometry::Vec3& a, const geometry::Vec3& b, const std::string& axis) {
    if (axis == "X") return std::abs(a.x - b.x);
    if (axis == "Y") return std::abs(a.y - b.y);
    if (axis == "Z") return std::abs(a.z - b.z);
    throw std::invalid_argument("axis_projection requires projection X/Y/Z, got: " + axis);
}

double ComputePointToPlaneDistance(
    const geometry::Vec3& point, const geometry::Vec3& pointOnPlane, const geometry::Vec3& normal) {
    const double normalLength = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (normalLength < 1e-12) {
        throw std::invalid_argument("plane normal must not be zero-length");
    }
    const geometry::Vec3 delta{point.x - pointOnPlane.x, point.y - pointOnPlane.y, point.z - pointOnPlane.z};
    const double dot = delta.x * normal.x + delta.y * normal.y + delta.z * normal.z;
    return std::abs(dot / normalLength);
}

} // namespace rule
