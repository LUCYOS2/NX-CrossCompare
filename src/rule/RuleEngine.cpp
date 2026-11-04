#include "rule/RuleEngine.h"

#include "rule/Matching.h"
#include "rule/Measure.h"
#include "rule/Selector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace rule {

namespace {

// anchor.paramKey=="diameter" 필터는 rule::FilterByDiameter(Selector.h)를 그대로 쓴다 -
// 규칙 관리창의 매칭 미리보기(RuleEditorDialog)도 같은 필터를 써야 해서 공용 함수로 뺐다. 이름을
// 다르게 둔 이유: 같은 이름으로 이 anonymous namespace 안에 선언하면 인수 개수가
// 다른 오버로드라도 바깥(rule::) 쪽 선언을 가려버려서(이름 은닉) 컴파일이 깨진다.
std::vector<geometry::AnchorCandidate> ApplyDiameterFilter(
    std::vector<geometry::AnchorCandidate> candidates, const Anchor& anchor) {
    return FilterByDiameter(std::move(candidates), anchor.paramKey, anchor.paramValue);
}

// 지름 필터와 축 방향 필터를 함께 적용(AND) - Anchor가 별개 필드로 들고 있어서
// 순서대로 걸어주기만 하면 된다.
//
// § 형상 프리셋 버그 수정(2026-09-18) - "hook 높이 규칙이 failed to resolve anchor pair
// (ambiguous candidates)로 실패한다"는 리포트. 프리셋 기반 후보(patchSimilarity>0)는
// candidate.axis를 채우지 않는다(원통이 아니라 "축" 개념 자체가 없는 복합 형상이라서) -
// 항상 Vec3{0,0,0}인 채로 FilterByAxisDirection에 들어가면 NormalDot이 0벡터를 0.0으로
// 취급해 각도가 90도로 계산되고, 그러면 기본 허용오차(5도)를 항상 벗어나 후보가 전부
// 걸러진다. "축 방향" 필터(nextPointDirection_)를 "(필터 안 씀)"이 아닌 걸로 두고
// 프리셋 포인트를 추가하면 항상 이렇게 됐다 - 프리셋 후보에는 이 필터를 아예 적용하지
// 않는다(애초에 축 개념이 없는 형상이라 필터링할 근거가 없음).
std::vector<geometry::AnchorCandidate> ApplyAnchorFilters(
    std::vector<geometry::AnchorCandidate> candidates, const Anchor& anchor) {
    candidates = ApplyDiameterFilter(std::move(candidates), anchor);
    if (anchor.patchDescriptor.has_value()) {
        return candidates;
    }
    candidates = FilterByAxisDirection(std::move(candidates), anchor.directionAxis, anchor.directionToleranceDeg);
    return candidates;
}

// § 형상 프리셋(2026-09-18) - anchor.patchDescriptor가 있으면 FindAnchorCandidates 대신
// adapter.FindPatchCandidates로 지문 유사도 검색을 하고, 결과(PatchCandidate)를
// AnchorCandidate로 옮겨 담는다(patchSimilarity 필드에 유사도를 실어서 - 이후
// ResolveSingleAnchor/ResolvePairAnchors가 이 값으로 자동 선택). 일반 anchorType(Hole 등)
// 이면 기존 경로 그대로. 임계값(geometry::kPatchSimilarityThreshold)은 UI 쪽 프리셋
// 등록/검색 미리보기와 공유한다(IGeometryAdapter.h 참고).
std::vector<geometry::AnchorCandidate> ResolveAnchorCandidates(
    const geometry::IGeometryAdapter& adapter, geometry::ModelHandle handle, const Anchor& anchor) {
    if (anchor.patchDescriptor.has_value()) {
        const auto patchCandidates =
            adapter.FindPatchCandidates(handle, *anchor.patchDescriptor, geometry::kPatchSimilarityThreshold);
        std::vector<geometry::AnchorCandidate> candidates;
        candidates.reserve(patchCandidates.size());
        for (const auto& pc : patchCandidates) {
            geometry::AnchorCandidate candidate;
            candidate.anchorType = anchor.anchorType;
            candidate.partName = anchor.partName;
            candidate.position = pc.position;
            candidate.patchSimilarity = pc.similarity;
            candidates.push_back(std::move(candidate));
        }
        return candidates;
    }
    return adapter.FindAnchorCandidates(handle, anchor.anchorType, anchor.partName);
}

// 후보 중 하나라도 patchSimilarity>0이면 프리셋 기반 검색 결과다(일반 FindAnchorCandidates
// 경로는 이 필드를 항상 기본값 0으로 둔다) - 그 경우 selector 설정과 무관하게 항상
// 유사도 최고점을 자동 채택한다(§ 형상 프리셋, 계획서 UI/워크플로우 3번 참고).
bool IsPatchBased(const std::vector<geometry::AnchorCandidate>& candidates) {
    return std::any_of(candidates.begin(), candidates.end(),
                        [](const geometry::AnchorCandidate& c) { return c.patchSimilarity > 0.0; });
}

std::optional<geometry::AnchorCandidate> ResolveSingleAnchor(
    const std::vector<geometry::AnchorCandidate>& candidates, const std::vector<std::string>& selectors) {
    if (IsPatchBased(candidates)) {
        return SelectBestPatchMatch(candidates);
    }
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
    // § 형상 프리셋 - A/B 어느 한쪽이라도 프리셋 기반이면, 그 쪽만 유사도 최고점으로 먼저
    // 좁히고(다른 쪽이 프리셋이 아니면 기존 로직으로 별도 해석) 조합한다. nearest_pair처럼
    // "두 후보 집합 사이 최적 조합"을 찾는 방식과는 다르다 - 프리셋은 이미 자체적으로
    // 후보를 1개로 확정하는 게 우선이라서다.
    if (IsPatchBased(candidatesA) || IsPatchBased(candidatesB)) {
        const auto a = ResolveSingleAnchor(candidatesA, selectors);
        const auto b = ResolveSingleAnchor(candidatesB, selectors);
        if (!a || !b) {
            return std::nullopt;
        }
        return std::make_pair(*a, *b);
    }
    if (std::find(selectors.begin(), selectors.end(), "nearest_pair") != selectors.end()) {
        return SelectNearestPair(candidatesA, candidatesB);
    }
    if (candidatesA.size() == 1 && candidatesB.size() == 1) {
        return std::make_pair(candidatesA.front(), candidatesB.front());
    }
    return std::nullopt;
}

// § 측정 엔진 점 후보(2026-10-30) - 클릭 스냅으로 저장된 anchorType 문자열(RuleEditorDialog::
// onFacePicked)을 엔진 검색 종류로 되돌린다. 점 종류가 아니면 None. 시작/끝점은 모서리 방향에
// 따라 바뀌어 인치 간 구분이 안정적이지 않아 꼭짓점 하나로 합친다(IGeometryAdapter 참고).
geometry::PointSubKind PointSubKindFromAnchorType(const std::string& anchorType) {
    if (anchorType == "Vertex" || anchorType == "Start_Point" || anchorType == "End_Point") {
        return geometry::PointSubKind::Vertex;
    }
    if (anchorType == "Edge_Midpoint") return geometry::PointSubKind::EdgeMidpoint;
    if (anchorType == "Center_Point") return geometry::PointSubKind::Center;
    if (anchorType == "Intersection_Point") return geometry::PointSubKind::Intersection;
    return geometry::PointSubKind::None;
}

std::string AnchorLabel(const Anchor& anchor) {
    return "포인트 " + anchor.role + "(" + anchor.anchorType + ")";
}

double Dot(const geometry::Vec3& a, const geometry::Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

double Length(const geometry::Vec3& v) {
    return std::sqrt(Dot(v, v));
}

// 두 방향의 코사인(부호 유지). 길이 0이면 0.
double CosBetween(const geometry::Vec3& a, const geometry::Vec3& b) {
    const double la = Length(a);
    const double lb = Length(b);
    if (la < 1e-12 || lb < 1e-12) {
        return 0.0;
    }
    return Dot(a, b) / (la * lb);
}

double CosOfDeg(double deg) {
    constexpr double kPi = 3.14159265358979323846;
    return std::cos(deg * kPi / 180.0);
}

geometry::Vec3 Add(const geometry::Vec3& a, const geometry::Vec3& b) {
    return geometry::Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

geometry::Vec3 Sub(const geometry::Vec3& a, const geometry::Vec3& b) {
    return geometry::Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

// § 국부 기준(측정면 원칙 3) - 기준 인치에서 first → second가 가까우면, 다른 인치에서 매칭된 first
// 위치에서 같은 상대 거리만큼 옮긴 자리를 second의 예측 위치로 쓴다. 멀면 nullopt(외곽 기준 예측).
std::optional<geometry::Vec3> LocalPrediction(const RefGeometry& firstRef, const geometry::Vec3& matchedFirst,
                                              const RefGeometry& secondRef) {
    if (!IsLocalReference(firstRef.position, secondRef.position, firstRef.sourceBox)) {
        return std::nullopt;
    }
    return Add(matchedFirst, Sub(secondRef.position, firstRef.position));
}

// § 주변 지문 검증 - 위치로 고른 자리(probe) 주변 형상이 기준 인치의 지문과 같은지 본다. 지문이 없는
// 예전 규칙/실 형상이 없는 어댑터는 건너뛴다.
void VerifyLocalPatch(const geometry::IGeometryAdapter& adapter, geometry::ModelHandle handle,
                      const RefGeometry& ref, const geometry::Vec3& probe, const std::string& label) {
    if (!ref.localPatch.has_value()) {
        return;
    }
    const geometry::FeaturePatchCapture capture = adapter.CaptureLocalPatch(handle, probe, ref.localPatchRadiusMm);
    if (!capture.valid) {
        throw std::runtime_error(label + ": 매칭된 위치의 주변 형상을 읽을 수 없음");
    }
    const double similarity = geometry::PatchSimilarity(*ref.localPatch, capture.descriptor);
    if (similarity < kLocalPatchMinSimilarity) {
        std::ostringstream msg;
        msg.precision(2);
        msg << std::fixed << label << ": 주변 형상이 기준 인치와 다름 (유사도 " << similarity << " < "
            << kLocalPatchMinSimilarity << ")";
        throw std::runtime_error(msg.str());
    }
}

// 평면 위에서 지문을 뜰 자리 - near를 그 면의 범위(faceBox) 안으로 당긴 뒤 평면에 내린다. 기준
// 인치에서는 클릭 점, 다른 인치에서는 예측 위치가 near라서 양쪽이 "같은 자리"를 가리킨다.
geometry::Vec3 PlaneProbePoint(const geometry::PlaneCandidate& plane, const geometry::Vec3& near) {
    geometry::Vec3 p = near;
    if (plane.hasFaceBox) {
        p.x = std::clamp(p.x, plane.faceBox.min.x, plane.faceBox.max.x);
        p.y = std::clamp(p.y, plane.faceBox.min.y, plane.faceBox.max.y);
        p.z = std::clamp(p.z, plane.faceBox.min.z, plane.faceBox.max.z);
    }
    const double len = Length(plane.normal);
    if (len < 1e-12) {
        return p;
    }
    const double offset = Dot(Sub(p, plane.pointOnPlane), plane.normal) / (len * len);
    return geometry::Vec3{p.x - plane.normal.x * offset, p.y - plane.normal.y * offset, p.z - plane.normal.z * offset};
}

// § 인치 간 자동 매칭 - 클릭 좌표를 이 모델로 옮겨 그 근처의 유일한 후보를 고른다. 형상
// 조건(종류/지름/축 방향)으로 먼저 거른 뒤 위치로 고르므로, 위치가 비슷해도 다른 형상은 안
// 잡힌다. 고른 뒤 주변 지문까지 확인한다. 실패하면 사유를 담아 던진다(Evaluate가 인치별 오류로
// 기록). predictedOverride가 있으면(국부 기준) 외곽 기준 예측 대신 그 위치에서 찾는다.
geometry::AnchorCandidate MatchAnchorByRef(const geometry::IGeometryAdapter& adapter, geometry::ModelHandle handle,
                                           const Anchor& anchor,
                                           const std::optional<geometry::Vec3>& predictedOverride = std::nullopt) {
    const geometry::BoundingBox targetBox = adapter.GetBoundingBox(handle);
    const geometry::Vec3 predicted = predictedOverride.value_or(
        PredictPosition(anchor.ref->position, anchor.ref->sourceBox, targetBox));
    const double radius = MatchRadiusMm(targetBox);

    std::vector<geometry::AnchorCandidate> candidates;
    const geometry::PointSubKind subKind = PointSubKindFromAnchorType(anchor.anchorType);
    if (subKind != geometry::PointSubKind::None && !anchor.patchDescriptor.has_value()) {
        // 유일성(2등 후보) 판단까지 하려면 반경보다 넓게 가져와야 한다 - 1등이 반경 안이면
        // 판단에 필요한 2등은 항상 2×반경+최소 간격 안에 있다(PickUniqueNearest 조건).
        candidates = adapter.FindPointCandidates(handle, subKind, predicted, radius * 2.0 + kMatchMinSeparationMm);
    } else {
        candidates = ApplyAnchorFilters(ResolveAnchorCandidates(adapter, handle, anchor), anchor);
    }

    std::vector<double> distances;
    distances.reserve(candidates.size());
    for (const auto& c : candidates) {
        distances.push_back(ComputePointToPointDistance(c.position, predicted));
    }
    const auto match = PickUniqueNearest(
        distances,
        [&candidates](std::size_t i, std::size_t j) {
            return ComputePointToPointDistance(candidates[i].position, candidates[j].position) <=
                   kSameFeatureToleranceMm;
        },
        radius);
    if (!match.index) {
        throw std::runtime_error(AnchorLabel(anchor) + ": " + match.error);
    }
    VerifyLocalPatch(adapter, handle, *anchor.ref, candidates[*match.index].position, AnchorLabel(anchor));
    return candidates[*match.index];
}

struct PlaneMatch {
    geometry::PlaneCandidate plane;
    geometry::Vec3 probe; // 예측 위치를 그 면 위로 옮긴 점(지문 검증 자리, 국부 기준의 출발점)
};

// 클릭 좌표 기준 평면 고르기 - 무한 평면이 아니라 실제 면(faceBox)까지의 거리로 고르고, 클릭한
// 면의 법선(부호 포함)과 같은 방향인 면만 본다(판재 앞/뒷면 구분). 같은 평면 위에 나뉜 면들은
// 어느 것을 골라도 거리 계산이 같으므로 하나로 취급한다. 고른 뒤 주변 지문까지 확인한다.
PlaneMatch MatchPlaneByRef(const geometry::IGeometryAdapter& adapter, geometry::ModelHandle handle,
                           const RefGeometry& ref, std::vector<geometry::PlaneCandidate> planes,
                           const std::string& label,
                           const std::optional<geometry::Vec3>& predictedOverride = std::nullopt) {
    const geometry::BoundingBox targetBox = adapter.GetBoundingBox(handle);
    const geometry::Vec3 predicted =
        predictedOverride.value_or(PredictPosition(ref.position, ref.sourceBox, targetBox));
    const double cosTolerance = CosOfDeg(kNormalMatchToleranceDeg);
    if (ref.normal.has_value()) {
        planes.erase(std::remove_if(planes.begin(), planes.end(),
                                    [&](const geometry::PlaneCandidate& p) {
                                        return CosBetween(p.normal, *ref.normal) < cosTolerance;
                                    }),
                     planes.end());
    }
    std::vector<double> distances;
    distances.reserve(planes.size());
    for (const auto& p : planes) {
        distances.push_back(p.hasFaceBox ? DistanceToBox(predicted, p.faceBox)
                                         : ComputePointToPlaneDistance(predicted, p.pointOnPlane, p.normal));
    }
    const auto match = PickUniqueNearest(
        distances,
        [&planes, cosTolerance](std::size_t i, std::size_t j) {
            return CosBetween(planes[i].normal, planes[j].normal) >= cosTolerance &&
                   ComputePointToPlaneDistance(planes[j].pointOnPlane, planes[i].pointOnPlane, planes[i].normal) <=
                       kSameFeatureToleranceMm;
        },
        MatchRadiusMm(targetBox));
    if (!match.index) {
        throw std::runtime_error(label + ": " + match.error);
    }
    PlaneMatch result{planes[*match.index], PlaneProbePoint(planes[*match.index], predicted)};
    VerifyLocalPatch(adapter, handle, ref, result.probe, label);
    return result;
}

// anchor 하나를 확정한다(point_to_plane의 점 등). 클릭 좌표가 있으면 매칭, 없으면 예전 selector 방식.
geometry::AnchorCandidate ResolveAnchor(const geometry::IGeometryAdapter& adapter, geometry::ModelHandle handle,
                                        const Anchor& anchor, const std::vector<std::string>& selectors) {
    if (anchor.ref.has_value()) {
        return MatchAnchorByRef(adapter, handle, anchor);
    }
    if (PointSubKindFromAnchorType(anchor.anchorType) != geometry::PointSubKind::None &&
        !anchor.patchDescriptor.has_value()) {
        throw std::runtime_error(AnchorLabel(anchor) +
                                 ": 클릭 좌표가 없는 예전 규칙 - 규칙 관리에서 포인트를 다시 지정하세요");
    }
    const auto candidates = ApplyAnchorFilters(ResolveAnchorCandidates(adapter, handle, anchor), anchor);
    const auto resolved = ResolveSingleAnchor(candidates, selectors);
    if (!resolved) {
        throw std::runtime_error(AnchorLabel(anchor) + ": 후보를 하나로 정할 수 없음 (후보 " +
                                 std::to_string(candidates.size()) + "개)");
    }
    return *resolved;
}

std::pair<geometry::AnchorCandidate, geometry::AnchorCandidate> ResolveAnchorPairForRule(
    const geometry::IGeometryAdapter& adapter, geometry::ModelHandle handle, const Rule& rule) {
    const Anchor& anchorA = rule.anchors[0];
    const Anchor& anchorB = rule.anchors[1];
    if (anchorA.ref.has_value() && anchorB.ref.has_value()) {
        // 국부 기준 - B가 A 근처면 매칭된 A에서의 상대 위치로 B를 찾는다(측정면 원칙 3).
        const auto a = MatchAnchorByRef(adapter, handle, anchorA);
        const auto b = MatchAnchorByRef(adapter, handle, anchorB, LocalPrediction(*anchorA.ref, a.position, *anchorB.ref));
        return {a, b};
    }
    if (anchorA.ref.has_value() || anchorB.ref.has_value()) {
        // 한쪽만 클릭 좌표가 있으면(예: 클릭 점 + 이름 있는 프리셋) 그쪽을 먼저 확정하고,
        // 나머지는 selector로 - nearest_pair면 확정된 점에 가장 가까운 후보를 고른다.
        const bool aHasRef = anchorA.ref.has_value();
        const Anchor& fixedAnchor = aHasRef ? anchorA : anchorB;
        const Anchor& otherAnchor = aHasRef ? anchorB : anchorA;
        const auto fixed = MatchAnchorByRef(adapter, handle, fixedAnchor);
        const auto otherCandidates =
            ApplyAnchorFilters(ResolveAnchorCandidates(adapter, handle, otherAnchor), otherAnchor);
        auto other = ResolveSingleAnchor(otherCandidates, rule.selector);
        if (!other && std::find(rule.selector.begin(), rule.selector.end(), "nearest_pair") != rule.selector.end()) {
            const auto pair = SelectNearestPair({fixed}, otherCandidates);
            if (pair) {
                other = pair->second;
            }
        }
        if (!other) {
            throw std::runtime_error(AnchorLabel(otherAnchor) + ": 후보를 하나로 정할 수 없음 (후보 " +
                                     std::to_string(otherCandidates.size()) + "개)");
        }
        return aHasRef ? std::make_pair(fixed, *other) : std::make_pair(*other, fixed);
    }
    for (const Anchor* anchor : {&anchorA, &anchorB}) {
        if (PointSubKindFromAnchorType(anchor->anchorType) != geometry::PointSubKind::None &&
            !anchor->patchDescriptor.has_value()) {
            throw std::runtime_error(AnchorLabel(*anchor) +
                                     ": 클릭 좌표가 없는 예전 규칙 - 규칙 관리에서 포인트를 다시 지정하세요");
        }
    }
    auto candidatesA = ApplyAnchorFilters(ResolveAnchorCandidates(adapter, handle, anchorA), anchorA);
    auto candidatesB = ApplyAnchorFilters(ResolveAnchorCandidates(adapter, handle, anchorB), anchorB);
    const auto pair = ResolvePairAnchors(candidatesA, candidatesB, rule.selector);
    if (!pair) {
        throw std::runtime_error("두 포인트를 하나로 정할 수 없음 (후보 A " + std::to_string(candidatesA.size()) +
                                 "개, B " + std::to_string(candidatesB.size()) + "개)");
    }
    return *pair;
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
                throw std::runtime_error("point_to_point/axis_projection은 포인트 2개가 필요함");
            }
            const auto pair = ResolveAnchorPairForRule(adapter, handle, rule);
            return rule.measurementType == MeasurementType::PointToPoint
                       ? ComputePointToPointDistance(pair.first.position, pair.second.position)
                       : ComputeAxisProjectionDistance(pair.first.position, pair.second.position, rule.projection);
        }
        case MeasurementType::PointToPlane: {
            if (rule.anchors.size() != 1 || !rule.referencePlane.has_value()) {
                throw std::runtime_error("point_to_plane은 포인트 1개 + 기준 평면이 필요함");
            }
            const auto resolved = ResolveAnchor(adapter, handle, rule.anchors[0], rule.selector);
            auto planes = adapter.FindPlaneCandidates(
                handle, rule.referencePlane->planeType, rule.referencePlane->partName);
            planes = FilterPlanesByNormal(
                std::move(planes), rule.referencePlane->normalAxis, rule.referencePlane->normalToleranceDeg);
            geometry::PlaneCandidate plane;
            if (rule.referencePlane->ref.has_value()) {
                // 국부 기준면(측정면 원칙 3) - 높이/단차의 기준면이 점 근처면 매칭된 점에서의 상대
                // 위치로 찾는다. 외곽 기준으로 찾으면 인치가 커질 때 다른 평면을 잡을 수 있다.
                std::optional<geometry::Vec3> local;
                if (rule.anchors[0].ref.has_value()) {
                    local = LocalPrediction(*rule.anchors[0].ref, resolved.position, *rule.referencePlane->ref);
                }
                plane = MatchPlaneByRef(adapter, handle, *rule.referencePlane->ref, std::move(planes), "기준 평면",
                                        local)
                            .plane;
            } else {
                // 예전 규칙(클릭 좌표 없음) - 어느 평면인지 알 방법이 없어 첫 후보를 쓴다.
                if (planes.empty()) {
                    throw std::runtime_error("기준 평면 후보 없음");
                }
                plane = planes.front();
            }
            return ComputePointToPlaneDistance(resolved.position, plane.pointOnPlane, plane.normal);
        }
        case MeasurementType::FaceToFaceGap: {
            if (rule.anchors.size() != 2) {
                throw std::runtime_error("face_to_face_gap은 면 2개가 필요함");
            }
            const Anchor& anchorA = rule.anchors[0];
            const Anchor& anchorB = rule.anchors[1];
            if (anchorA.ref.has_value() || anchorB.ref.has_value()) {
                // § 측정 재점검(2026-10-30) - 클릭한 두 평면을 각각 그 자리에서 고른다. 예전엔
                // A/B 후보 목록이 똑같이 "모델의 모든 면"이라 한 면이 자기 자신과 짝지어질 수도
                // 있었다.
                if (!anchorA.ref || !anchorB.ref || !anchorA.ref->normal || !anchorB.ref->normal) {
                    throw std::runtime_error("face_to_face_gap은 평면 두 개를 클릭해야 함");
                }
                const auto planes = adapter.FindPlaneCandidates(handle, "Datum_Plane", std::string());
                const auto matchA = MatchPlaneByRef(adapter, handle, *anchorA.ref, planes, "면 A");
                const auto planeA = matchA.plane;
                const auto planeB =
                    MatchPlaneByRef(adapter, handle, *anchorB.ref, planes, "면 B",
                                    LocalPrediction(*anchorA.ref, matchA.probe, *anchorB.ref))
                        .plane;
                const double cosAngle = std::abs(CosBetween(planeA.normal, planeB.normal));
                if (cosAngle < CosOfDeg(kParallelFaceToleranceDeg)) {
                    throw std::runtime_error("두 면이 평행하지 않음 - 평행하지 않은 면 사이 최단거리는 아직 미지원");
                }
                const double gap = ComputePointToPlaneDistance(planeB.pointOnPlane, planeA.pointOnPlane, planeA.normal);
                if (gap <= kSameFeatureToleranceMm) {
                    throw std::runtime_error("면 A와 면 B가 같은 평면으로 매칭됨");
                }
                return gap;
            }
            const auto facesA = adapter.FindFaceCandidates(handle, anchorA.anchorType, anchorA.partName);
            const auto facesB = adapter.FindFaceCandidates(handle, anchorB.anchorType, anchorB.partName);
            const auto pair = ResolveFacePair(facesA, facesB, rule.selector);
            if (!pair) {
                throw std::runtime_error("면 쌍을 하나로 정할 수 없음 (애매하거나 마주보는 면 없음)");
            }
            return ComputeFaceToFaceGap(pair->first, pair->second);
        }
        case MeasurementType::OverallSize: {
            // anchor/selector 탐색 없이 BoundingBox에서 직접 계산 (§전장 사이즈 - 기본 세팅 항목)
            const auto box = adapter.GetBoundingBox(handle);
            if (rule.projection == "X") return box.max.x - box.min.x;
            if (rule.projection == "Y") return box.max.y - box.min.y;
            if (rule.projection == "Z") return box.max.z - box.min.z;
            throw std::runtime_error("overall_size는 projection X/Y/Z가 필요함 (현재: " + rule.projection + ")");
        }
        case MeasurementType::InstanceCount: {
            if (rule.anchors.size() != 1) {
                throw std::runtime_error("instance_count는 포인트 1개가 필요함");
            }
            auto candidates = ResolveAnchorCandidates(adapter, handle, rule.anchors[0]);
            candidates = ApplyAnchorFilters(std::move(candidates), rule.anchors[0]);
            return static_cast<double>(candidates.size());
        }
        case MeasurementType::MinPitch: {
            if (rule.anchors.size() != 1) {
                throw std::runtime_error("min_pitch는 포인트 1개가 필요함");
            }
            auto candidates = ResolveAnchorCandidates(adapter, handle, rule.anchors[0]);
            candidates = ApplyAnchorFilters(std::move(candidates), rule.anchors[0]);
            if (candidates.size() < 2) {
                throw std::runtime_error(
                    "min_pitch는 조건에 맞는 후보가 2개 이상 필요함 (찾은 개수 " + std::to_string(candidates.size()) + ")");
            }
            // § 측정 재점검(2026-10-30) - 예전엔 projection 축으로 정렬한 뒤 "바로 옆" 후보끼리만
            // 거리를 재서, 정렬 축과 다른 방향으로 더 가까운 쌍을 놓쳤다. 모든 쌍의 최솟값을 쓴다.
            double minGap = std::numeric_limits<double>::max();
            for (size_t i = 0; i < candidates.size(); ++i) {
                for (size_t j = i + 1; j < candidates.size(); ++j) {
                    minGap = std::min(
                        minGap, ComputePointToPointDistance(candidates[i].position, candidates[j].position));
                }
            }
            return minGap;
        }
    }
    throw std::runtime_error("알 수 없는 측정 타입");
}

} // namespace

std::vector<InchResult> RuleEngine::Evaluate(
    const geometry::IGeometryAdapter& adapter,
    const std::map<int, geometry::ModelHandle>& handlesByInch,
    const Rule& rule) {
    std::vector<InchResult> results;
    std::optional<double> baseline;

    for (const auto& [inch, handle] : handlesByInch) {
        InchResult result;
        result.inch = inch;
        try {
            result.value = EvaluateSingleModel(adapter, handle, rule);
        } catch (const std::exception& e) {
            result.ok = false;
            result.error = e.what();
            results.push_back(std::move(result));
            continue;
        }
        if (!baseline.has_value()) {
            baseline = result.value;
        }
        result.deltaFromBaseline = result.value - *baseline;

        const auto specIt = rule.specByInch.find(inch);
        if (specIt != rule.specByInch.end()) {
            result.spec = specIt->second;
            result.deviation = result.value - specIt->second;
            result.withinTolerance =
                result.deviation <= rule.tolerancePlusMm && result.deviation >= -rule.toleranceMinusMm;
        } else {
            result.deviation = result.deltaFromBaseline;
            // 전장 사이즈는 인치마다 다른 게 정상(설계 의도)이라 도면 스펙 없이는 공차 판정 자체가
            // 의미 없음 - 값/편차는 그대로 보여주되 pass/fail 표시는 하지 않는다.
            result.withinTolerance = rule.measurementType == MeasurementType::OverallSize
                ? true
                : (result.deltaFromBaseline <= rule.tolerancePlusMm &&
                   result.deltaFromBaseline >= -rule.toleranceMinusMm);
        }
        results.push_back(std::move(result));
    }

    AddConsistencyWarnings(rule, results);
    return results;
}

void RuleEngine::AddConsistencyWarnings(const Rule& rule, std::vector<InchResult>& results) {
    // 전장 사이즈는 매칭 없이 bbox에서 바로 계산하므로 "엉뚱한 형상을 잡을" 일이 없다.
    if (rule.measurementType == MeasurementType::OverallSize) {
        return;
    }
    const double band = std::max(rule.tolerancePlusMm, rule.toleranceMinusMm);
    const double allowed = std::max(kSuspectToleranceMultiple * band, kSuspectMinDeviationMm);

    for (auto& result : results) {
        if (result.ok && result.spec.has_value() && std::abs(result.deviation) > allowed) {
            std::ostringstream msg;
            msg.precision(3);
            msg << std::fixed << "도면 스펙과 차이가 큼(" << result.deviation
                << " mm) - 다른 형상을 잡았는지 확인";
            result.warning = msg.str();
        }
    }

    // 형상 개수는 인치가 커지면 계단식으로 늘어나는 게 정상이라 직선 추세로 볼 수 없다.
    if (rule.measurementType == MeasurementType::InstanceCount) {
        return;
    }
    std::vector<std::pair<int, double>> points;
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (results[i].ok) {
            points.emplace_back(results[i].inch, results[i].value);
            indices.push_back(i);
        }
    }
    const auto outlier = FindTrendOutlier(points, allowed);
    if (!outlier) {
        return;
    }
    InchResult& suspect = results[indices[outlier->index]];
    // 도면 스펙이 있는 인치는 위에서 도면 스펙 기준으로 이미 봤다(설계상 추세를 안 따를 수도 있음).
    if (suspect.spec.has_value() || !suspect.warning.empty()) {
        return;
    }
    std::ostringstream msg;
    msg.precision(3);
    msg << std::fixed << "다른 인치들의 추세와 다름(추세상 " << outlier->predicted << " mm) - 다른 형상을 잡았는지 확인";
    suspect.warning = msg.str();
}

void RuleEngine::FinalizeAnchorRef(
    const geometry::IGeometryAdapter& adapter, geometry::ModelHandle handle, Anchor& anchor) {
    if (!anchor.ref.has_value()) {
        return;
    }
    // 기준 인치에서는 지문 검증 없이(아직 지문이 없거나 예전 것) 평가와 같은 방법으로 매칭한다.
    Anchor probe = anchor;
    probe.ref->localPatch.reset();
    geometry::AnchorCandidate matched;
    try {
        matched = MatchAnchorByRef(adapter, handle, probe);
    } catch (const std::exception&) {
        return; // 기준 인치에서도 못 찾으면 평가할 때 같은 사유로 실패가 표시된다.
    }
    anchor.ref->position = matched.position;
    const auto capture = adapter.CaptureLocalPatch(handle, matched.position, kLocalPatchRadiusMm);
    if (capture.valid) {
        anchor.ref->localPatch = capture.descriptor;
        anchor.ref->localPatchRadiusMm = kLocalPatchRadiusMm;
    } else {
        anchor.ref->localPatch.reset();
    }
}

void RuleEngine::FinalizePlaneRef(
    const geometry::IGeometryAdapter& adapter, geometry::ModelHandle handle, RefGeometry& ref) {
    RefGeometry probe = ref;
    probe.localPatch.reset();
    geometry::Vec3 probePoint;
    try {
        probePoint = MatchPlaneByRef(adapter, handle, probe,
                                     adapter.FindPlaneCandidates(handle, "Datum_Plane", std::string()), "평면")
                         .probe;
    } catch (const std::exception&) {
        return;
    }
    const auto capture = adapter.CaptureLocalPatch(handle, probePoint, kLocalPatchRadiusMm);
    if (capture.valid) {
        ref.localPatch = capture.descriptor;
        ref.localPatchRadiusMm = kLocalPatchRadiusMm;
    } else {
        ref.localPatch.reset();
    }
}

std::optional<geometry::Vec3> RuleEngine::ResolveAnchorPosition(
    const geometry::IGeometryAdapter& adapter, geometry::ModelHandle handle, const Anchor& anchor,
    std::string* error) {
    try {
        return ResolveAnchor(adapter, handle, anchor, {}).position;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return std::nullopt;
    }
}

} // namespace rule
