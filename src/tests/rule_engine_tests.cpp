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
    const auto rules = rule::BuiltInRules();
    const auto& bossScrewRule = rules[3];

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
    const auto rules = rule::BuiltInRules();
    const auto& hookHeightRule = rules[4];

    const auto results = rule::RuleEngine::Evaluate(adapter, handles, hookHeightRule);
    for (const auto& r : results) {
        Check(std::abs(r.value - 8.5) < 1e-6, "Hook height should stay fixed at 8.5mm regardless of inch");
        Check(r.withinTolerance, "Hook height should be within tolerance for every inch");
    }
}

void TestHookCatchRuleAcrossInches() {
    geometry::MockGeometryAdapter adapter;
    const auto handles = LoadAllInches(adapter);
    const auto rules = rule::BuiltInRules();
    const auto& hookCatchRule = rules[5];

    const auto results = rule::RuleEngine::Evaluate(adapter, handles, hookCatchRule);
    for (const auto& r : results) {
        Check(std::abs(r.value - 0.2) < 1e-6, "Hook catch axis_projection(Z) should stay fixed at 0.2mm");
        Check(r.withinTolerance, "Hook catch should be within tolerance for every inch");
    }
}

void TestRibOpenCellGapRuleAcrossInches() {
    geometry::MockGeometryAdapter adapter;
    const auto handles = LoadAllInches(adapter);
    const auto rules = rule::BuiltInRules();
    const auto& ribGapRule = rules[6];

    const auto results = rule::RuleEngine::Evaluate(adapter, handles, ribGapRule);
    for (const auto& r : results) {
        Check(std::abs(r.value - 0.15) < 1e-6, "Rib-OpenCell gap should stay fixed at 0.15mm regardless of inch");
        Check(r.withinTolerance, "Rib-OpenCell gap should be within tolerance for every inch");
    }
}

void TestWallThicknessRuleAcrossInches() {
    geometry::MockGeometryAdapter adapter;
    const auto handles = LoadAllInches(adapter);
    const auto rules = rule::BuiltInRules();
    const auto& wallThicknessRule = rules[7];

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
