#include "rule/RuleEngine.h"

#include "rule/Measure.h"
#include "rule/Selector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace rule {

namespace {

// anchor.paramKey=="diameter" 필터는 rule::FilterByDiameter(Selector.h)를 그대로 쓴다 -
// 포인트 검색(AnchorSearchDialog)도 같은 필터를 써야 해서 공용 함수로 뺐다. 이름을
// 다르게 둔 이유: 같은 이름으로 이 anonymous namespace 안에 선언하면 인수 개수가
// 다른 오버로드라도 바깥(rule::) 쪽 선언을 가려버려서(이름 은닉) 컴파일이 깨진다.
std::vector<geometry::AnchorCandidate> ApplyDiameterFilter(
    std::vector<geometry::AnchorCandidate> candidates, const Anchor& anchor) {
    return FilterByDiameter(std::move(candidates), anchor.paramKey, anchor.paramValue);
}

// 지름 필터와 축 방향 필터를 함께 적용(AND) - Anchor가 별개 필드로 들고 있어서
// 순서대로 걸어주기만 하면 된다.
std::vector<geometry::AnchorCandidate> ApplyAnchorFilters(
    std::vector<geometry::AnchorCandidate> candidates, const Anchor& anchor) {
    candidates = ApplyDiameterFilter(std::move(candidates), anchor);
    candidates = FilterByAxisDirection(std::move(candidates), anchor.directionAxis, anchor.directionToleranceDeg);
    return candidates;
}

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
            auto candidatesA = adapter.FindAnchorCandidates(
                handle, rule.anchors[0].anchorType, rule.anchors[0].partName);
            auto candidatesB = adapter.FindAnchorCandidates(
                handle, rule.anchors[1].anchorType, rule.anchors[1].partName);
            candidatesA = ApplyAnchorFilters(std::move(candidatesA), rule.anchors[0]);
            candidatesB = ApplyAnchorFilters(std::move(candidatesB), rule.anchors[1]);
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
            auto candidates = adapter.FindAnchorCandidates(
                handle, rule.anchors[0].anchorType, rule.anchors[0].partName);
            candidates = ApplyAnchorFilters(std::move(candidates), rule.anchors[0]);
            const auto resolved = ResolveSingleAnchor(candidates, rule.selector);
            if (!resolved) {
                throw std::runtime_error(rule.name + ": failed to resolve anchor (ambiguous candidates)");
            }
            auto planes = adapter.FindPlaneCandidates(
                handle, rule.referencePlane->planeType, rule.referencePlane->partName);
            planes = FilterPlanesByNormal(
                std::move(planes), rule.referencePlane->normalAxis, rule.referencePlane->normalToleranceDeg);
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
        case MeasurementType::OverallSize: {
            // anchor/selector 탐색 없이 BoundingBox에서 직접 계산 (§전장 사이즈 - 기본 세팅 항목)
            const auto box = adapter.GetBoundingBox(handle);
            if (rule.projection == "X") return box.max.x - box.min.x;
            if (rule.projection == "Y") return box.max.y - box.min.y;
            if (rule.projection == "Z") return box.max.z - box.min.z;
            throw std::runtime_error(rule.name + ": overall_size requires projection X/Y/Z, got: " + rule.projection);
        }
        case MeasurementType::InstanceCount: {
            if (rule.anchors.size() != 1) {
                throw std::runtime_error(rule.name + ": instance_count needs 1 anchor");
            }
            auto candidates = adapter.FindAnchorCandidates(
                handle, rule.anchors[0].anchorType, rule.anchors[0].partName);
            candidates = ApplyAnchorFilters(std::move(candidates), rule.anchors[0]);
            return static_cast<double>(candidates.size());
        }
        case MeasurementType::MinPitch: {
            if (rule.anchors.size() != 1) {
                throw std::runtime_error(rule.name + ": min_pitch needs 1 anchor");
            }
            auto candidates = adapter.FindAnchorCandidates(
                handle, rule.anchors[0].anchorType, rule.anchors[0].partName);
            candidates = ApplyAnchorFilters(std::move(candidates), rule.anchors[0]);
            if (candidates.size() < 2) {
                throw std::runtime_error(
                    rule.name + ": min_pitch needs at least 2 matching candidates (found " +
                    std::to_string(candidates.size()) + ")");
            }
            // projection을 정렬 축(X/Y/Z)으로 재사용 - 비어있으면 X를 기본값으로 쓴다.
            const std::string& axis = rule.projection;
            auto axisValue = [&axis](const geometry::Vec3& p) {
                if (axis == "Y") return p.y;
                if (axis == "Z") return p.z;
                return p.x;
            };
            std::sort(candidates.begin(), candidates.end(),
                      [&axisValue](const geometry::AnchorCandidate& a, const geometry::AnchorCandidate& b) {
                          return axisValue(a.position) < axisValue(b.position);
                      });
            double minGap = std::numeric_limits<double>::max();
            for (size_t i = 1; i < candidates.size(); ++i) {
                minGap = std::min(
                    minGap, ComputePointToPointDistance(candidates[i - 1].position, candidates[i].position));
            }
            return minGap;
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
        // 전장 사이즈는 인치마다 다른 게 정상(설계 의도)이라 공차 판정 자체가 의미 없음 -
        // 값/편차는 그대로 보여주되 pass/fail 표시는 하지 않는다.
        result.withinTolerance = rule.measurementType == MeasurementType::OverallSize
            ? true
            : (result.deltaFromBaseline <= rule.tolerancePlusMm && result.deltaFromBaseline >= -rule.toleranceMinusMm);
        results.push_back(result);
    }

    return results;
}

} // namespace rule
