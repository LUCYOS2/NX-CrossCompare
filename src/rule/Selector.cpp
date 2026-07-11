#include "rule/Selector.h"
#include "rule/Measure.h"

#include <limits>

namespace rule {

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

} // namespace rule
