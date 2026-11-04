// § 측정 매칭 진단(2026-10-30) - 실제 STEP에서 클릭 좌표 기반 매칭(rule::Matching)이 얼마나
// 정확한지 전수 검사하는 일회성 도구(CTest에는 안 넣음 - 실제 파일 경로가 필요).
//
//   diag_measure_matching <기준.stp> [다른 인치.stp ...]
//
// 1) 기준 모델 자기 매칭: 모든 구멍/꼭짓점/모서리 중간점/원 중심/평면을 "그 자리를 클릭했다"고
//    가정하고 규칙 엔진으로 다시 찾게 해서, 정확히 같은 형상이 나오는지(정답), 애매/못 찾음
//    (실패 - 값을 안 냄), 다른 형상(오답 - 가장 위험)으로 나눠 센다. 오답은 0이어야 한다.
// 2) 다른 인치 모델이 주어지면 같은 클릭 좌표로 매칭해서 성공/실패 개수와 실패 사유를 보여준다
//    (정답 세트가 없어 정답/오답은 판정 못 함 - 실패율과 사유 분포 확인용).
//
// § 주변 지문 검증(2026-11-04) - 앱에서 포인트를 지정할 때와 똑같이 RuleEngine::FinalizeAnchorRef/
// FinalizePlaneRef로 기준 모델의 주변 지문을 붙인 뒤 검사한다. 다른 인치에서 "주변 형상이 기준
// 인치와 다름" 사유가 많으면 kLocalPatchRadiusMm/kLocalPatchMinSimilarity 튜닝이 필요하다는 뜻.

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "geometry/StepGeometryAdapter.h"
#include "rule/Matching.h"
#include "rule/Measure.h"
#include "rule/RuleEngine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

struct Tally {
    int correct = 0;
    int failed = 0;
    int wrong = 0;
    std::map<std::string, int> reasons;
};

// "포인트 A(Hole): 후보가 애매함 (0.00mm / 0.10mm)" -> "후보가 애매함" 처럼 숫자를 뗀 사유.
std::string ReasonKey(const std::string& error) {
    const auto colon = error.find(": ");
    std::string reason = colon == std::string::npos ? error : error.substr(colon + 2);
    const auto paren = reason.find(" (");
    return paren == std::string::npos ? reason : reason.substr(0, paren);
}

void Print(const char* label, const Tally& t) {
    std::printf("  %-14s 정답 %5d | 실패 %5d | 오답 %5d\n", label, t.correct, t.failed, t.wrong);
    for (const auto& [reason, count] : t.reasons) {
        std::printf("      - %s: %d\n", reason.c_str(), count);
    }
}

constexpr size_t kMaxSamplesPerKind = 400; // 큰 모델에서 진단이 너무 오래 걸리지 않게

void CheckAnchors(const geometry::IGeometryAdapter& adapter, geometry::ModelHandle source,
                  const std::vector<geometry::ModelHandle>& others, const std::vector<rule::Anchor>& anchors,
                  const char* label) {
    Tally self;
    std::vector<Tally> cross(others.size());
    for (const auto& anchor : anchors) {
        std::string error;
        const auto matched = rule::RuleEngine::ResolveAnchorPosition(adapter, source, anchor, &error);
        if (!matched) {
            ++self.failed;
            ++self.reasons[ReasonKey(error)];
        } else if (rule::ComputePointToPointDistance(*matched, anchor.ref->position) <= rule::kSameFeatureToleranceMm) {
            ++self.correct;
        } else {
            ++self.wrong;
        }
        for (size_t i = 0; i < others.size(); ++i) {
            std::string crossError;
            if (rule::RuleEngine::ResolveAnchorPosition(adapter, others[i], anchor, &crossError)) {
                ++cross[i].correct; // 정답 세트가 없으니 "값을 냄" 개수로만 센다
            } else {
                ++cross[i].failed;
                ++cross[i].reasons[ReasonKey(crossError)];
            }
        }
    }
    Print(label, self);
    for (size_t i = 0; i < others.size(); ++i) {
        std::printf("    -> 모델 #%zu: 값을 냄 %d | 실패 %d\n", i + 1, cross[i].correct, cross[i].failed);
        for (const auto& [reason, count] : cross[i].reasons) {
            std::printf("         - %s: %d\n", reason.c_str(), count);
        }
    }
}

// 앱에서 포인트를 지정할 때처럼 기준 모델에서 ref를 확정하고 주변 지문을 붙인다.
rule::Anchor MakeRefAnchor(const geometry::IGeometryAdapter& adapter, geometry::ModelHandle source,
                           const std::string& type, const geometry::Vec3& position, const geometry::BoundingBox& box,
                           double diameter = 0.0) {
    rule::Anchor a;
    a.role = "A";
    a.anchorType = type;
    if (diameter > 0.0) {
        a.paramKey = "diameter";
        a.paramValue = diameter;
    }
    a.ref = rule::RefGeometry{position, std::nullopt, box};
    rule::RuleEngine::FinalizeAnchorRef(adapter, source, a);
    return a;
}

int Run(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::fprintf(stderr, "사용법: %s <기준.stp> [다른 인치.stp ...]\n", args.empty() ? "diag" : args[0].c_str());
        return 1;
    }
    geometry::StepGeometryAdapter adapter;
    std::vector<geometry::ModelHandle> handles;
    for (size_t i = 1; i < args.size(); ++i) {
        try {
            handles.push_back(adapter.LoadModel(args[i]));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "LoadModel failed: %s\n", e.what());
            return 1;
        }
    }
    const geometry::ModelHandle source = handles.front();
    const std::vector<geometry::ModelHandle> others(handles.begin() + 1, handles.end());
    const geometry::BoundingBox box = adapter.GetBoundingBox(source);
    const geometry::Vec3 center{(box.min.x + box.max.x) * 0.5, (box.min.y + box.max.y) * 0.5,
                                (box.min.z + box.max.z) * 0.5};
    const double everywhere = rule::ComputePointToPointDistance(box.min, box.max) + 1.0;

    const auto started = std::chrono::steady_clock::now();
    std::printf("기준 모델: %s\n", args[1].c_str());
    std::printf("bbox: (%.2f, %.2f, %.2f) ~ (%.2f, %.2f, %.2f), 매칭 반경 %.2fmm\n", box.min.x, box.min.y, box.min.z,
                box.max.x, box.max.y, box.max.z, rule::MatchRadiusMm(box));

    std::vector<rule::Anchor> holeAnchors;
    for (const auto& hole : adapter.FindAnchorCandidates(source, "Hole", "")) {
        if (holeAnchors.size() >= kMaxSamplesPerKind) break;
        holeAnchors.push_back(MakeRefAnchor(adapter, source, "Hole", hole.position, box, hole.diameterMm));
    }
    std::printf("\n원통(구멍) 후보 %zu개 (반원통 합친 뒤)\n", adapter.FindAnchorCandidates(source, "Hole", "").size());
    CheckAnchors(adapter, source, others, holeAnchors, "구멍");

    const std::pair<geometry::PointSubKind, const char*> pointKinds[] = {
        {geometry::PointSubKind::Vertex, "Vertex"},
        {geometry::PointSubKind::EdgeMidpoint, "Edge_Midpoint"},
        {geometry::PointSubKind::Center, "Center_Point"},
    };
    for (const auto& [kind, type] : pointKinds) {
        std::vector<rule::Anchor> anchors;
        for (const auto& p : adapter.FindPointCandidates(source, kind, center, everywhere)) {
            if (anchors.size() >= kMaxSamplesPerKind) break;
            anchors.push_back(MakeRefAnchor(adapter, source, type, p.position, box));
        }
        CheckAnchors(adapter, source, others, anchors, type);
    }

    // 평면: 꼭짓점 하나를 점으로 두고 point_to_plane 높이를 재서, 클릭한 평면 기준 기대값과 비교.
    // 같은 평면 위에 나뉜 면은 어느 것이든 값이 같으므로 값이 같으면 정답으로 친다.
    const auto planes = adapter.FindPlaneCandidates(source, "Datum_Plane", "");
    const auto vertices = adapter.FindPointCandidates(source, geometry::PointSubKind::Vertex, center, everywhere);
    if (!vertices.empty()) {
        Tally planeTally;
        const geometry::Vec3 tip = vertices.front().position;
        size_t count = 0;
        for (const auto& plane : planes) {
            if (count++ >= kMaxSamplesPerKind) break;
            rule::Rule r;
            r.measurementType = rule::MeasurementType::PointToPlane;
            r.anchors = {MakeRefAnchor(adapter, source, "Vertex", tip, box)};
            r.anchors[0].role = "single";
            rule::PlaneRef ref{"Datum_Plane", ""};
            ref.ref = rule::RefGeometry{plane.pointOnPlane, plane.normal, box};
            rule::RuleEngine::FinalizePlaneRef(adapter, source, *ref.ref);
            r.referencePlane = ref;
            const auto result = rule::RuleEngine::Evaluate(adapter, {{0, source}}, r).front();
            const double expected = rule::ComputePointToPlaneDistance(tip, plane.pointOnPlane, plane.normal);
            if (!result.ok) {
                ++planeTally.failed;
                ++planeTally.reasons[ReasonKey(result.error)];
            } else if (std::abs(result.value - expected) <= 1e-6) {
                ++planeTally.correct;
            } else {
                ++planeTally.wrong;
            }
        }
        std::printf("\n평면 후보 %zu개\n", planes.size());
        Print("평면(높이)", planeTally);
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("\n소요 시간 %.1f초\n", seconds);
    return 0;
}

} // namespace

// § 유니코드 경로(2026-11-04) - main(char**)의 인수는 Windows 기본 코드페이지(한국어 Windows는 CP949)로
// 들어와서, "●"는 깨지고 한글 경로는 UTF-8이 아니게 된다. StepGeometryAdapter::LoadModel은 UTF-8 경로를
// 기대하므로(앱은 Qt가 UTF-8로 넘김) wmain으로 UTF-16 인수를 받아 UTF-8로 바꿔 넘긴다.
#ifdef _WIN32
int wmain(int argc, wchar_t** wargv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(size > 0 ? static_cast<size_t>(size - 1) : 0, '\0');
        if (size > 1) {
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, utf8.data(), size, nullptr, nullptr);
        }
        args.push_back(std::move(utf8));
    }
    return Run(args);
}
#else
int main(int argc, char** argv) {
    return Run(std::vector<std::string>(argv, argv + argc));
}
#endif
