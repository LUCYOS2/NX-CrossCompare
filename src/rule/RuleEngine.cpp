#include "rule/RuleEngine.h"

#include "rule/Measure.h"
#include "rule/Selector.h"

#include <algorithm>
#include <optional>
#include <stdexcept>

namespace rule {

namespace {

std::optional<geometry::AnchorCandidate> ResolveSingleAnchor(
    const std::vector<geometry::AnchorCandidate>& candidates, const std::vector<std::string>& selectors) {
    if (std::find(selectors.begin(), selectors.end(), "leftmost") != selectors.end()) {
        return SelectLeftmost(candidates);
    }
    if (std::find(selectors.begin(), selectors.end(), "rightmost") != selectors.end()) {
        return SelectRightmost(candidates);
    }
    if (candidates.size() == 1) {
        return candidates.front();
    }
    return std::nullopt;
}

std::optional<std::pair<geometry::AnchorCandidate, geometry::AnchorCandidate>> ResolvePairAnchors(
    const std::vector<geometry::AnchorCandidate>& candidatesA,
    const std::vector<geometry::AnchorCandidate>& candidatesB,
    const std::vector<std::string>& selectors) {
    if (std::find(selectors.begin(), selectors.end(), "nearest_pair") != selectors.end()) {
        return SelectNearestPair(candidatesA, candidatesB);
    }
    if (candidatesA.size() == 1 && candidatesB.size() == 1) {
        return std::make_pair(candidatesA.front(), candidatesB.front());
    }
    return std::nullopt;
}

std::optional<std::pair<geometry::FaceCandidate, geometry::FaceCandidate>> ResolveFacePair(
    const std::vector<geometry::FaceCandidate>& candidatesA,
    const std::vector<geometry::FaceCandidate>& candidatesB,
    const std::vector<std::string>& selectors) {
    if (std::find(selectors.begin(), selectors.end(), "nearest_face_pair") != selectors.end()) {
        return SelectNearestFacePair(candidatesA, candidatesB);
    }
    if (std::find(selectors.begin(), selectors.end(), "parallel_face_pair") != selectors.end()) {
        return SelectParallelFacePair(candidatesA, candidatesB);
    }
    if (candidatesA.size() == 1 && candidatesB.size() == 1) {
        return std::make_pair(candidatesA.front(), candidatesB.front());
    }
    return std::nullopt;
}

double EvaluateSingleModel(const geometry::IGeometryAdapter& adapter, geometry::ModelHandle handle,
                            const Rule& rule) {
    switch (rule.measurementType) {
        case MeasurementType::PointToPoint:
        case MeasurementType::AxisProjection: {
            if (rule.anchors.size() != 2) {
                throw std::runtime_error(rule.name + ": point_to_point/axis_projection needs 2 anchors");
            }
            const auto candidatesA = adapter.FindAnchorCandidates(
                handle, rule.anchors[0].anchorType, rule.anchors[0].partName);
            const auto candidatesB = adapter.FindAnchorCandidates(
                handle, rule.anchors[1].anchorType, rule.anchors[1].partName);
            const auto pair = ResolvePairAnchors(candidatesA, candidatesB, rule.selector);
            if (!pair) {
                throw std::runtime_error(rule.name + ": failed to resolve anchor pair (ambiguous candidates)");
            }
            return rule.measurementType == MeasurementType::PointToPoint
                       ? ComputePointToPointDistance(pair->first.position, pair->second.position)
                       : ComputeAxisProjectionDistance(pair->first.position, pair->second.position, rule.projection);
        }
        case MeasurementType::PointToPlane: {
            if (rule.anchors.size() != 1 || !rule.referencePlane.has_value()) {
                throw std::runtime_error(rule.name + ": point_to_plane needs 1 anchor + referencePlane");
            }
            const auto candidates = adapter.FindAnchorCandidates(
                handle, rule.anchors[0].anchorType, rule.anchors[0].partName);
            const auto resolved = ResolveSingleAnchor(candidates, rule.selector);
            if (!resolved) {
                throw std::runtime_error(rule.name + ": failed to resolve anchor (ambiguous candidates)");
            }
            const auto planes = adapter.FindPlaneCandidates(
                handle, rule.referencePlane->planeType, rule.referencePlane->partName);
            if (planes.empty()) {
                throw std::runtime_error(rule.name + ": no plane candidate found");
            }
            return ComputePointToPlaneDistance(resolved->position, planes.front().pointOnPlane, planes.front().normal);
        }
        case MeasurementType::FaceToFaceGap: {
            if (rule.anchors.size() != 2) {
                throw std::runtime_error(rule.name + ": face_to_face_gap needs 2 anchors (face refs)");
            }
            const auto facesA = adapter.FindFaceCandidates(handle, rule.anchors[0].anchorType, rule.anchors[0].partName);
            const auto facesB = adapter.FindFaceCandidates(handle, rule.anchors[1].anchorType, rule.anchors[1].partName);
            const auto pair = ResolveFacePair(facesA, facesB, rule.selector);
            if (!pair) {
                throw std::runtime_error(rule.name + ": failed to resolve face pair (ambiguous or no anti-parallel match)");
            }
            return ComputeFaceToFaceGap(pair->first, pair->second);
        }
    }
    throw std::runtime_error(rule.name + ": unknown measurement type");
}

} // namespace

std::vector<InchResult> RuleEngine::Evaluate(
    const geometry::IGeometryAdapter& adapter,
    const std::map<int, geometry::ModelHandle>& handlesByInch,
    const Rule& rule) {
    std::vector<InchResult> results;
    std::optional<double> baseline;

    for (const auto& [inch, handle] : handlesByInch) {
        const double value = EvaluateSingleModel(adapter, handle, rule);
        if (!baseline.has_value()) {
            baseline = value;
        }

        InchResult result;
        result.inch = inch;
        result.value = value;
        result.deltaFromBaseline = value - *baseline;
        result.withinTolerance =
            result.deltaFromBaseline <= rule.tolerancePlusMm && result.deltaFromBaseline >= -rule.toleranceMinusMm;
        results.push_back(result);
    }

    return results;
}

} // namespace rule
