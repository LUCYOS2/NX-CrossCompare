#include "rule/Selector.h"
#include "rule/Measure.h"

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
