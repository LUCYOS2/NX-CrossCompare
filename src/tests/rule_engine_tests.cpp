#include "geometry/MockGeometryAdapter.h"
#include "rule/BuiltInCatalog.h"
#include "rule/RuleEngine.h"
#include "rule/Selector.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

std::map<int, geometry::ModelHandle> LoadAllInches(geometry::MockGeometryAdapter& adapter) {
    std::map<int, geometry::ModelHandle> handles;
    for (int inch : {43, 50, 55, 65, 75, 85}) {
        handles[inch] = adapter.LoadModel(std::to_string(inch) + "inch.jt");
    }
    return handles;
}

// § 예시 카탈로그 정리(2026-09-11) - Boss-Screw/후크/Rib/살두께는 실제 대상 모델(Hole만
// 있고 Hook/Boss/Rib류 형상 없음)과 안 맞아 rule::BuiltInRules()에서 빠졌다(사용자
// 요청). 그래도 이 규칙들이 대표하는 측정 로직(nearest_pair, point_to_plane,
// axis_projection, nearest_face_pair, parallel_face_pair)은 계속 검증해야 하므로, 예전
// BuiltInCatalog.cpp에 있던 정의를 그대로 여기로 옮겨 테스트 전용 픽스처로 쓴다.
rule::Rule MakeBossScrewRule() {
    rule::Rule r;
    r.name = "001. Boss-Screw 체결 정렬";
    r.anchors = {
        rule::Anchor{"A", "Hole", "Bezel", std::optional<std::string>("diameter"), std::optional<double>(2.8)},
        rule::Anchor{
            "B", "Boss_Center", "Rear_Chassis", std::optional<std::string>("diameter"), std::optional<double>(2.6)},
    };
    r.referenceFrame = {"World_Origin", "Datum_CSYS"};
    r.selector = {"nearest_pair"};
    r.measurementType = rule::MeasurementType::PointToPoint;
    r.projection = "3D";
    r.tolerancePlusMm = 0.15;
    r.toleranceMinusMm = 0.15;
    return r;
}

rule::Rule MakeHookHeightRule() {
    rule::Rule r;
    r.name = "002. 후크 높이";
    r.anchors = {rule::Anchor{"single", "Hook_Tip_Edge", "Side_Frame", std::nullopt, std::nullopt}};
    r.referencePlane = rule::PlaneRef{"Datum_Plane", "Rear_Chassis"};
    r.referenceFrame = {"World_Origin"};
    r.selector = {"leftmost"};
    r.measurementType = rule::MeasurementType::PointToPlane;
    r.projection = "normal";
    r.tolerancePlusMm = 0.1;
    r.toleranceMinusMm = 0.1;
    return r;
}

rule::Rule MakeHookCatchRule() {
    rule::Rule r;
    r.name = "003. 후크 걸림량";
    r.anchors = {
        rule::Anchor{"A", "Hook_Catch_Edge", "Side_Frame", std::nullopt, std::nullopt},
        rule::Anchor{"B", "Wall_Inner_Edge", "Front_Bezel", std::nullopt, std::nullopt},
    };
    r.referenceFrame = {"World_Origin"};
    r.measurementType = rule::MeasurementType::AxisProjection;
    r.projection = "Z";
    r.tolerancePlusMm = 0.3;
    r.toleranceMinusMm = 0.1;
    return r;
}

rule::Rule MakeRibOpenCellGapRule() {
    rule::Rule r;
    r.name = "004. Rib-OpenCell Gap";
    r.anchors = {
        rule::Anchor{"A", "Rib_Top_Surface", "Rib", std::nullopt, std::nullopt},
        rule::Anchor{"B", "OpenCell_Edge", "OpenCell", std::nullopt, std::nullopt},
    };
    r.referenceFrame = {"World_Origin"};
    r.selector = {"nearest_face_pair"};
    r.measurementType = rule::MeasurementType::FaceToFaceGap;
    r.tolerancePlusMm = 0.2;
    r.toleranceMinusMm = 0.2;
    return r;
}

rule::Rule MakeWallThicknessRule() {
    rule::Rule r;
    r.name = "005. 살두께";
    r.anchors = {
        rule::Anchor{"A", "Boss_Outer_Wall", "Boss", std::nullopt, std::nullopt},
        rule::Anchor{"B", "Boss_Inner_Wall", "Boss", std::nullopt, std::nullopt},
    };
    r.referenceFrame = {"World_Origin"};
    r.selector = {"parallel_face_pair"};
    r.measurementType = rule::MeasurementType::FaceToFaceGap;
    r.projection = "normal";
    r.tolerancePlusMm = 0.05;
    r.toleranceMinusMm = 0.05;
    return r;
}

void TestNearestPairPicksTrueMatchAmongDistractors() {
    geometry::MockGeometryAdapter adapter;
    const auto handle = adapter.LoadModel("55inch.jt");

    const auto holes = adapter.FindAnchorCandidates(handle, "Hole", "Bezel");
    const auto bosses = adapter.FindAnchorCandidates(handle, "Boss_Center", "Rear_Chassis");
    Check(holes.size() == 3, "should generate 3 hole candidates");
    Check(bosses.size() == 3, "should generate 3 boss candidates");

    const auto pair = rule::SelectNearestPair(holes, bosses);
    Check(pair.has_value(), "nearest_pair should find a pair");
    if (pair) {
        const double distance = std::sqrt(
            std::pow(pair->first.position.x - pair->second.position.x, 2) +
            std::pow(pair->first.position.y - pair->second.position.y, 2));
        Check(distance < 1.0, "nearest_pair should pick the true match (small offset), not a distractor");
    }
}

void TestBossScrewRuleAcrossInches() {
    geometry::MockGeometryAdapter adapter;
    const auto handles = LoadAllInches(adapter);
    const auto bossScrewRule = MakeBossScrewRule();

    const auto results = rule::RuleEngine::Evaluate(adapter, handles, bossScrewRule);
    Check(results.size() == handles.size(), "should have one result per inch");

    const double expected = std::sqrt(0.05 * 0.05 + 0.02 * 0.02);
    for (const auto& r : results) {
        Check(std::abs(r.value - expected) < 1e-6, "Boss-Screw distance should stay fixed regardless of inch");
        Check(r.withinTolerance, "Boss-Screw distance should be within tolerance for every inch");
    }
}

void TestHookHeightRuleAcrossInches() {
    geometry::MockGeometryAdapter adapter;
    const auto handles = LoadAllInches(adapter);
    const auto hookHeightRule = MakeHookHeightRule();

    const auto results = rule::RuleEngine::Evaluate(adapter, handles, hookHeightRule);
    for (const auto& r : results) {
        Check(std::abs(r.value - 8.5) < 1e-6, "Hook height should stay fixed at 8.5mm regardless of inch");
        Check(r.withinTolerance, "Hook height should be within tolerance for every inch");
    }
}

void TestHookCatchRuleAcrossInches() {
    geometry::MockGeometryAdapter adapter;
    const auto handles = LoadAllInches(adapter);
    const auto hookCatchRule = MakeHookCatchRule();

    const auto results = rule::RuleEngine::Evaluate(adapter, handles, hookCatchRule);
    for (const auto& r : results) {
        Check(std::abs(r.value - 0.2) < 1e-6, "Hook catch axis_projection(Z) should stay fixed at 0.2mm");
        Check(r.withinTolerance, "Hook catch should be within tolerance for every inch");
    }
}

void TestRibOpenCellGapRuleAcrossInches() {
    geometry::MockGeometryAdapter adapter;
    const auto handles = LoadAllInches(adapter);
    const auto ribGapRule = MakeRibOpenCellGapRule();

    const auto results = rule::RuleEngine::Evaluate(adapter, handles, ribGapRule);
    for (const auto& r : results) {
        Check(std::abs(r.value - 0.15) < 1e-6, "Rib-OpenCell gap should stay fixed at 0.15mm regardless of inch");
        Check(r.withinTolerance, "Rib-OpenCell gap should be within tolerance for every inch");
    }
}

void TestWallThicknessRuleAcrossInches() {
    geometry::MockGeometryAdapter adapter;
    const auto handles = LoadAllInches(adapter);
    const auto wallThicknessRule = MakeWallThicknessRule();

    const auto results = rule::RuleEngine::Evaluate(adapter, handles, wallThicknessRule);
    for (const auto& r : results) {
        Check(std::abs(r.value - 1.2) < 1e-6, "Wall thickness should stay fixed at 1.2mm regardless of inch");
        Check(r.withinTolerance, "Wall thickness should be within tolerance for every inch");
    }
}

void TestOverallSizeRulesScaleWithInchExceptDepth() {
    geometry::MockGeometryAdapter adapter;
    const auto handles = LoadAllInches(adapter);
    const auto rules = rule::BuiltInRules();
    const auto& sizeX = rules[0];
    const auto& sizeY = rules[1];
    const auto& sizeZ = rules[2];

    Check(sizeX.name.find("X") != std::string::npos, "rules[0] should be overall size X");
    Check(sizeZ.name.find("Z") != std::string::npos, "rules[2] should be overall size Z");

    const auto resultsX = rule::RuleEngine::Evaluate(adapter, handles, sizeX);
    const auto resultsY = rule::RuleEngine::Evaluate(adapter, handles, sizeY);
    const auto resultsZ = rule::RuleEngine::Evaluate(adapter, handles, sizeZ);

    // MockGeometryAdapter: kBaseWidth=1230, kBaseHeight=710, kFixedDepth=45, kBaseInch=55
    for (size_t i = 0; i < resultsX.size(); ++i) {
        const double scale = static_cast<double>(resultsX[i].inch) / 55.0;
        Check(std::abs(resultsX[i].value - 1230.0 * scale) < 1e-6, "overall size X should scale with inch ratio");
        Check(std::abs(resultsY[i].value - 710.0 * scale) < 1e-6, "overall size Y should scale with inch ratio");
        Check(std::abs(resultsZ[i].value - 45.0) < 1e-6, "overall size Z(depth) should stay fixed regardless of inch");
        Check(resultsX[i].withinTolerance, "overall size should never be flagged as tolerance failure");
    }
}

void TestParallelFacePairRejectsNonAntiParallelCandidates() {
    geometry::MockGeometryAdapter adapter;
    const auto handle = adapter.LoadModel("55inch.jt");

    const auto outerWalls = adapter.FindAnchorCandidates(handle, "Boss_Outer_Wall", "Boss");
    Check(outerWalls.empty(), "Boss_Outer_Wall is a face, not an anchor - sanity check on FindAnchorCandidates");

    const auto facesA = adapter.FindFaceCandidates(handle, "Boss_Outer_Wall", "Boss");
    const auto facesB = adapter.FindFaceCandidates(handle, "Boss_Inner_Wall", "Boss");
    Check(facesA.size() == 2 && facesB.size() == 2, "should generate 2 outer + 2 inner wall face candidates");

    const auto pair = rule::SelectParallelFacePair(facesA, facesB);
    Check(pair.has_value(), "parallel_face_pair should find the one anti-parallel pair");
    if (pair) {
        Check(std::abs(pair->first.normal.x - 1.0) < 1e-6, "should pick the outer wall facing +X, not the +Y distractor");
    }
}

} // namespace

int main() {
    TestNearestPairPicksTrueMatchAmongDistractors();
    TestBossScrewRuleAcrossInches();
    TestHookHeightRuleAcrossInches();
    TestHookCatchRuleAcrossInches();
    TestRibOpenCellGapRuleAcrossInches();
    TestWallThicknessRuleAcrossInches();
    TestOverallSizeRulesScaleWithInchExceptDepth();
    TestParallelFacePairRejectsNonAntiParallelCandidates();

    if (g_failures == 0) {
        std::printf("All rule engine tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
