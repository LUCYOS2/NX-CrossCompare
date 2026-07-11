#include "geometry/MockGeometryAdapter.h"
#include "rule/BuiltInCatalog.h"
#include "rule/RuleEngine.h"
#include "rule/Selector.h"

#include <cmath>
#include <cstdio>
#include <map>

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
    const auto rules = rule::BuiltInPointRules();
    const auto& bossScrewRule = rules[0];

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
    const auto rules = rule::BuiltInPointRules();
    const auto& hookHeightRule = rules[1];

    const auto results = rule::RuleEngine::Evaluate(adapter, handles, hookHeightRule);
    for (const auto& r : results) {
        Check(std::abs(r.value - 8.5) < 1e-6, "Hook height should stay fixed at 8.5mm regardless of inch");
        Check(r.withinTolerance, "Hook height should be within tolerance for every inch");
    }
}

void TestHookCatchRuleAcrossInches() {
    geometry::MockGeometryAdapter adapter;
    const auto handles = LoadAllInches(adapter);
    const auto rules = rule::BuiltInPointRules();
    const auto& hookCatchRule = rules[2];

    const auto results = rule::RuleEngine::Evaluate(adapter, handles, hookCatchRule);
    for (const auto& r : results) {
        Check(std::abs(r.value - 0.2) < 1e-6, "Hook catch axis_projection(Z) should stay fixed at 0.2mm");
        Check(r.withinTolerance, "Hook catch should be within tolerance for every inch");
    }
}

} // namespace

int main() {
    TestNearestPairPicksTrueMatchAmongDistractors();
    TestBossScrewRuleAcrossInches();
    TestHookHeightRuleAcrossInches();
    TestHookCatchRuleAcrossInches();

    if (g_failures == 0) {
        std::printf("All rule engine tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
