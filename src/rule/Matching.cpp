#include "rule/Matching.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace rule {

namespace {

// 한 축에서 클릭 좌표를 옮긴다 - min/center/max 중 가장 가까운 기준에서의 거리를 유지.
double PredictAxis(double value, double srcMin, double srcMax, double dstMin, double dstMax) {
    const double srcCenter = (srcMin + srcMax) * 0.5;
    const double dstCenter = (dstMin + dstMax) * 0.5;
    const double fromMin = std::abs(value - srcMin);
    const double fromCenter = std::abs(value - srcCenter);
    const double fromMax = std::abs(value - srcMax);
    if (fromMin <= fromCenter && fromMin <= fromMax) {
        return dstMin + (value - srcMin);
    }
    if (fromMax <= fromCenter) {
        return dstMax + (value - srcMax);
    }
    return dstCenter + (value - srcCenter);
}

} // namespace

geometry::Vec3 PredictPosition(
    const geometry::Vec3& refPosition, const geometry::BoundingBox& sourceBox, const geometry::BoundingBox& targetBox) {
    return geometry::Vec3{
        PredictAxis(refPosition.x, sourceBox.min.x, sourceBox.max.x, targetBox.min.x, targetBox.max.x),
        PredictAxis(refPosition.y, sourceBox.min.y, sourceBox.max.y, targetBox.min.y, targetBox.max.y),
        PredictAxis(refPosition.z, sourceBox.min.z, sourceBox.max.z, targetBox.min.z, targetBox.max.z),
    };
}

double MatchRadiusMm(const geometry::BoundingBox& targetBox) {
    const double dx = targetBox.max.x - targetBox.min.x;
    const double dy = targetBox.max.y - targetBox.min.y;
    const double dz = targetBox.max.z - targetBox.min.z;
    return std::max(kMatchMinRadiusMm, std::sqrt(dx * dx + dy * dy + dz * dz) * kMatchRadiusDiagonalRatio);
}

double DistanceToBox(const geometry::Vec3& point, const geometry::BoundingBox& box) {
    const double dx = std::max({box.min.x - point.x, 0.0, point.x - box.max.x});
    const double dy = std::max({box.min.y - point.y, 0.0, point.y - box.max.y});
    const double dz = std::max({box.min.z - point.z, 0.0, point.z - box.max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

MatchResult PickUniqueNearest(const std::vector<double>& distances,
                              const std::function<bool(std::size_t, std::size_t)>& sameFeature, double radiusMm) {
    MatchResult result;
    if (distances.empty()) {
        result.error = "조건에 맞는 후보가 없음";
        return result;
    }
    std::size_t best = 0;
    for (std::size_t i = 1; i < distances.size(); ++i) {
        if (distances[i] < distances[best]) {
            best = i;
        }
    }
    const double bestDist = distances[best];
    if (bestDist > radiusMm) {
        std::ostringstream msg;
        msg.precision(2);
        msg << std::fixed << "예상 위치 " << radiusMm << "mm 안에 후보 없음 (가장 가까운 후보 " << bestDist << "mm)";
        result.error = msg.str();
        return result;
    }
    double secondDist = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < distances.size(); ++i) {
        if (i != best && !sameFeature(best, i)) {
            secondDist = std::min(secondDist, distances[i]);
        }
    }
    if (secondDist < std::max(2.0 * bestDist, bestDist + kMatchMinSeparationMm)) {
        std::ostringstream msg;
        msg.precision(2);
        msg << std::fixed << "후보가 애매함 (" << bestDist << "mm / " << secondDist << "mm)";
        result.error = msg.str();
        return result;
    }
    result.index = best;
    result.distanceMm = bestDist;
    return result;
}

} // namespace rule
