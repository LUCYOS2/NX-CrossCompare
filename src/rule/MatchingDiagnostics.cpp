#include "rule/MatchingDiagnostics.h"

#include "rule/Matching.h"
#include "rule/Measure.h"
#include "rule/RuleEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

namespace rule {

namespace {

Anchor MakeRefAnchor(const geometry::IGeometryAdapter& adapter, geometry::ModelHandle source, const std::string& type,
                     const geometry::Vec3& position, const geometry::BoundingBox& box, double diameter = 0.0) {
    Anchor a;
    a.role = "A";
    a.anchorType = type;
    if (diameter > 0.0) {
        a.paramKey = "diameter";
        a.paramValue = diameter;
    }
    a.ref = RefGeometry{position, std::nullopt, box};
    RuleEngine::FinalizeAnchorRef(adapter, source, a);
    return a;
}

void Count(DiagnosticTally& tally, const std::string& error) {
    ++tally.failed;
    ++tally.reasons[DiagnosticReasonKey(error)];
}

} // namespace

std::string DiagnosticReasonKey(const std::string& error) {
    const auto colon = error.find(": ");
    std::string reason = colon == std::string::npos ? error : error.substr(colon + 2);
    const auto paren = reason.find(" (");
    return paren == std::string::npos ? reason : reason.substr(0, paren);
}

MatchingDiagnosticsReport RunMatchingDiagnostics(
    const geometry::IGeometryAdapter& adapter, geometry::ModelHandle source,
    const std::vector<geometry::ModelHandle>& others,
    const std::function<bool(std::size_t done, std::size_t total)>& progress) {
    const auto started = std::chrono::steady_clock::now();
    MatchingDiagnosticsReport report;

    const geometry::BoundingBox box = adapter.GetBoundingBox(source);
    const geometry::Vec3 center{(box.min.x + box.max.x) * 0.5, (box.min.y + box.max.y) * 0.5,
                                (box.min.z + box.max.z) * 0.5};
    const double everywhere = ComputePointToPointDistance(box.min, box.max) + 1.0;

    // 검사할 자리(종류, 위치, 지름)를 먼저 다 모아서 전체 개수를 안다(진행률 표시용).
    struct Probe {
        std::size_t section;
        std::string type;
        geometry::Vec3 position;
        double diameter = 0.0;
    };
    std::vector<Probe> probes;
    const auto holes = adapter.FindAnchorCandidates(source, "Hole", "");
    report.sections.push_back(DiagnosticSection{"구멍", holes.size()});
    for (const auto& hole : holes) {
        if (report.sections.back().sampled >= kMaxDiagnosticSamples) break;
        probes.push_back(Probe{0, "Hole", hole.position, hole.diameterMm});
        ++report.sections.back().sampled;
    }
    const std::pair<geometry::PointSubKind, const char*> pointKinds[] = {
        {geometry::PointSubKind::Vertex, "Vertex"},
        {geometry::PointSubKind::EdgeMidpoint, "Edge_Midpoint"},
        {geometry::PointSubKind::Center, "Center_Point"},
    };
    const char* pointLabels[] = {"꼭짓점", "모서리 중간점", "원 중심"};
    for (std::size_t k = 0; k < 3; ++k) {
        const auto points = adapter.FindPointCandidates(source, pointKinds[k].first, center, everywhere);
        report.sections.push_back(DiagnosticSection{pointLabels[k], points.size()});
        for (const auto& p : points) {
            if (report.sections.back().sampled >= kMaxDiagnosticSamples) break;
            probes.push_back(Probe{report.sections.size() - 1, pointKinds[k].second, p.position});
            ++report.sections.back().sampled;
        }
    }
    // 평면: 꼭짓점 하나를 점으로 두고 point_to_plane 높이를 재서, 클릭한 평면 기준 기대값과 비교한다.
    // 같은 평면 위에 나뉜 면은 어느 것이든 값이 같으므로 값이 같으면 정답으로 친다.
    const auto planes = adapter.FindPlaneCandidates(source, "Datum_Plane", "");
    const auto vertices = adapter.FindPointCandidates(source, geometry::PointSubKind::Vertex, center, everywhere);
    const std::size_t planeSection = report.sections.size();
    report.sections.push_back(DiagnosticSection{"평면(높이)", planes.size()});
    if (!vertices.empty()) {
        report.sections.back().sampled = std::min(planes.size(), kMaxDiagnosticSamples);
    }
    for (auto& section : report.sections) {
        section.others.resize(others.size());
    }

    const std::size_t total = probes.size() + report.sections[planeSection].sampled;
    std::size_t done = 0;
    const auto tick = [&]() {
        ++done;
        if (progress && !progress(done, total)) {
            report.cancelled = true;
        }
        return !report.cancelled;
    };

    for (const auto& probe : probes) {
        DiagnosticSection& section = report.sections[probe.section];
        const Anchor anchor = MakeRefAnchor(adapter, source, probe.type, probe.position, box, probe.diameter);
        std::string error;
        const auto matched = RuleEngine::ResolveAnchorPosition(adapter, source, anchor, &error);
        if (!matched) {
            Count(section.self, error);
        } else if (ComputePointToPointDistance(*matched, anchor.ref->position) <= kSameFeatureToleranceMm) {
            ++section.self.correct;
        } else {
            ++section.self.wrong;
        }
        for (std::size_t i = 0; i < others.size(); ++i) {
            std::string crossError;
            if (RuleEngine::ResolveAnchorPosition(adapter, others[i], anchor, &crossError)) {
                ++section.others[i].correct;
            } else {
                Count(section.others[i], crossError);
            }
        }
        if (!tick()) break;
    }

    if (!report.cancelled && !vertices.empty()) {
        DiagnosticSection& section = report.sections[planeSection];
        const geometry::Vec3 tip = vertices.front().position;
        for (std::size_t p = 0; p < section.sampled; ++p) {
            const auto& plane = planes[p];
            Rule r;
            r.measurementType = MeasurementType::PointToPlane;
            r.anchors = {MakeRefAnchor(adapter, source, "Vertex", tip, box)};
            r.anchors[0].role = "single";
            PlaneRef ref{"Datum_Plane", ""};
            ref.ref = RefGeometry{plane.pointOnPlane, plane.normal, box};
            RuleEngine::FinalizePlaneRef(adapter, source, *ref.ref);
            r.referencePlane = ref;

            const auto selfResult = RuleEngine::Evaluate(adapter, {{0, source}}, r).front();
            const double expected = ComputePointToPlaneDistance(tip, plane.pointOnPlane, plane.normal);
            if (!selfResult.ok) {
                Count(section.self, selfResult.error);
            } else if (std::abs(selfResult.value - expected) <= 1e-6) {
                ++section.self.correct;
            } else {
                ++section.self.wrong;
            }
            for (std::size_t i = 0; i < others.size(); ++i) {
                const auto crossResult = RuleEngine::Evaluate(adapter, {{0, others[i]}}, r).front();
                if (crossResult.ok) {
                    ++section.others[i].correct;
                } else {
                    Count(section.others[i], crossResult.error);
                }
            }
            if (!tick()) break;
        }
    }

    report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return report;
}

} // namespace rule
