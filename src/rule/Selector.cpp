#include "rule/Selector.h"
#include "rule/Measure.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rule {

namespace {

double NormalDot(const geometry::Vec3& a, const geometry::Vec3& b) {
    const double lenA = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    const double lenB = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
    if (lenA < 1e-12 || lenB < 1e-12) {
        return 0.0;
    }
    return (a.x * b.x + a.y * b.y + a.z * b.z) / (lenA * lenB);
}

geometry::Vec3 AxisUnitVector(const std::string& axis) {
    if (axis == "Y") return geometry::Vec3{0.0, 1.0, 0.0};
    if (axis == "Z") return geometry::Vec3{0.0, 0.0, 1.0};
    return geometry::Vec3{1.0, 0.0, 0.0}; // "X" 또는 알 수 없는 값의 기본값
}

// 방향 벡터 두 개 사이의 각도(도). 부호는 무시한다(±축 둘 다 "평행"으로 취급) - 원통
// 축이나 평면 법선이 뒤집혀 있어도 실무적으로는 같은 방향이기 때문.
double AngleBetweenDeg(const geometry::Vec3& a, const geometry::Vec3& b) {
    constexpr double kPi = 3.14159265358979323846;
    double cosAngle = std::abs(NormalDot(a, b));
    cosAngle = std::min(1.0, std::max(-1.0, cosAngle));
    return std::acos(cosAngle) * 180.0 / kPi;
}

} // namespace

std::vector<geometry::AnchorCandidate> FilterByDiameter(
    std::vector<geometry::AnchorCandidate> candidates,
    const std::optional<std::string>& paramKey, const std::optional<double>& paramValue) {
    if (paramKey != "diameter" || !paramValue.has_value()) {
        return candidates;
    }
    const double target = *paramValue;
    std::vector<geometry::AnchorCandidate> filtered;
    for (auto& candidate : candidates) {
        if (std::abs(candidate.diameterMm - target) <= kDiameterMatchToleranceMm) {
            filtered.push_back(std::move(candidate));
        }
    }
    return filtered;
}

std::vector<geometry::AnchorCandidate> FilterByAxisDirection(
    std::vector<geometry::AnchorCandidate> candidates,
    const std::optional<std::string>& axis, const std::optional<double>& toleranceDeg) {
    if (!axis.has_value() || axis->empty()) {
        return candidates;
    }
    const geometry::Vec3 target = AxisUnitVector(*axis);
    const double tolerance = toleranceDeg.value_or(kDefaultDirectionToleranceDeg);
    std::vector<geometry::AnchorCandidate> filtered;
    for (auto& candidate : candidates) {
        if (AngleBetweenDeg(candidate.axis, target) <= tolerance) {
            filtered.push_back(std::move(candidate));
        }
    }
    return filtered;
}

std::vector<geometry::PlaneCandidate> FilterPlanesByNormal(
    std::vector<geometry::PlaneCandidate> candidates,
    const std::optional<std::string>& axis, const std::optional<double>& toleranceDeg) {
    if (!axis.has_value() || axis->empty()) {
        return candidates;
    }
    const geometry::Vec3 target = AxisUnitVector(*axis);
    const double tolerance = toleranceDeg.value_or(kDefaultDirectionToleranceDeg);
    std::vector<geometry::PlaneCandidate> filtered;
    for (auto& candidate : candidates) {
        if (AngleBetweenDeg(candidate.normal, target) <= tolerance) {
            filtered.push_back(std::move(candidate));
        }
    }
    return filtered;
}

std::optional<std::pair<geometry::AnchorCandidate, geometry::AnchorCandidate>> SelectNearestPair(
    const std::vector<geometry::AnchorCandidate>& candidatesA,
    const std::vector<geometry::AnchorCandidate>& candidatesB) {
    if (candidatesA.empty() || candidatesB.empty()) {
        return std::nullopt;
    }

    double bestDistance = std::numeric_limits<double>::max();
    std::pair<geometry::AnchorCandidate, geometry::AnchorCandidate> best;

    for (const auto& a : candidatesA) {
        for (const auto& b : candidatesB) {
            const double distance = ComputePointToPointDistance(a.position, b.position);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = {a, b};
            }
        }
    }
    return best;
}

std::optional<geometry::AnchorCandidate> SelectLeftmost(
    const std::vector<geometry::AnchorCandidate>& candidates) {
    if (candidates.empty()) {
        return std::nullopt;
    }
    auto best = candidates.front();
    for (const auto& c : candidates) {
        if (c.position.x < best.position.x) {
            best = c;
        }
    }
    return best;
}

std::optional<geometry::AnchorCandidate> SelectRightmost(
    const std::vector<geometry::AnchorCandidate>& candidates) {
    if (candidates.empty()) {
        return std::nullopt;
    }
    auto best = candidates.front();
    for (const auto& c : candidates) {
        if (c.position.x > best.position.x) {
            best = c;
        }
    }
    return best;
}

std::optional<std::pair<geometry::FaceCandidate, geometry::FaceCandidate>> SelectNearestFacePair(
    const std::vector<geometry::FaceCandidate>& candidatesA,
    const std::vector<geometry::FaceCandidate>& candidatesB) {
    if (candidatesA.empty() || candidatesB.empty()) {
        return std::nullopt;
    }

    double bestDistance = std::numeric_limits<double>::max();
    std::pair<geometry::FaceCandidate, geometry::FaceCandidate> best;

    for (const auto& a : candidatesA) {
        for (const auto& b : candidatesB) {
            const double distance = ComputePointToPointDistance(a.center, b.center);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = {a, b};
            }
        }
    }
    return best;
}

std::optional<std::pair<geometry::FaceCandidate, geometry::FaceCandidate>> SelectParallelFacePair(
    const std::vector<geometry::FaceCandidate>& candidatesA,
    const std::vector<geometry::FaceCandidate>& candidatesB) {
    constexpr double kAntiParallelThreshold = -0.9;

    double bestDistance = std::numeric_limits<double>::max();
    std::optional<std::pair<geometry::FaceCandidate, geometry::FaceCandidate>> best;

    for (const auto& a : candidatesA) {
        for (const auto& b : candidatesB) {
            if (NormalDot(a.normal, b.normal) >= kAntiParallelThreshold) {
                continue; // 법선이 반대방향이 아니면 후보에서 제외
            }
            const double distance = ComputePointToPointDistance(a.center, b.center);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = std::make_pair(a, b);
            }
        }
    }
    return best;
}

} // namespace rule
