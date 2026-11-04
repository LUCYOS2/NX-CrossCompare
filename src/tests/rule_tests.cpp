#include "database/Database.h"
#include "rule/Measure.h"
#include "rule/Rule.h"
#include "rule/SyntheticFixture.h"

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

rule::Rule BuildBossScrewRule() {
    rule::Rule r;
    r.name = "Boss-Screw 체결 정렬";
    r.anchors = {
        rule::Anchor{"A", "Hole", "Bezel", std::optional<std::string>("diameter"), std::optional<double>(2.8)},
        rule::Anchor{"B", "Boss_Center", "Rear_Chassis", std::optional<std::string>("diameter"), std::optional<double>(2.6)},
    };
    r.referenceFrame = {"World_Origin", "Datum_CSYS"};
    r.selector = {"nearest_pair"};
    r.measurementType = rule::MeasurementType::PointToPoint;
    r.projection = "3D";
    r.tolerancePlusMm = 0.15;
    r.toleranceMinusMm = 0.15;
    return r;
}

void TestRuleRoundTrip() {
    database::Database db(":memory:");
    db.EnsureSchema();

    const int projectId = db.CreateProject("TestProject");
    Check(projectId > 0, "project id should be positive");

    const rule::Rule original = BuildBossScrewRule();
    const int ruleId = db.SaveRule(projectId, original);
    Check(ruleId > 0, "rule id should be positive");

    const rule::Rule loaded = db.LoadRule(ruleId);
    Check(loaded.name == original.name, "loaded rule name should match");
    Check(loaded.measurementType == original.measurementType, "loaded measurement type should match");
    Check(loaded.projection == original.projection, "loaded projection should match");
    Check(std::abs(loaded.tolerancePlusMm - original.tolerancePlusMm) < 1e-9, "tolerance_plus should match");
    Check(loaded.anchors.size() == 2, "loaded rule should have 2 anchors");
    Check(loaded.referenceFrame.size() == 2 && loaded.referenceFrame[0] == "World_Origin",
          "reference_frame priority order should be preserved (World_Origin first)");
    Check(loaded.selector.size() == 1 && loaded.selector[0] == "nearest_pair",
          "selector should be preserved");

    const std::vector<int> inches = {43, 50, 55, 65, 75, 85};
    const auto samples = rule::GenerateBossScrewFixture(inches);
    for (const auto& sample : samples) {
        db.SavePoint(ruleId, sample);
    }

    const auto loadedPoints = db.LoadPoints(ruleId);
    Check(loadedPoints.size() == inches.size() * 2, "should load 2 points per inch");

    // 인치별로 A/B를 묶어서 거리를 계산 — 인치가 달라도 값이 거의 동일해야 정상
    // (§8: anchor_B는 anchor_A 대비 고정 오프셋만 가지도록 생성했으므로)
    std::map<int, geometry::Vec3> pointA, pointB;
    for (const auto& p : loadedPoints) {
        geometry::Vec3 v{p.x, p.y, p.z};
        if (p.role == "A") {
            pointA[p.inch] = v;
        } else if (p.role == "B") {
            pointB[p.inch] = v;
        }
    }

    double firstDistance = -1.0;
    for (int inch : inches) {
        const double distance = rule::ComputePointToPointDistance(pointA[inch], pointB[inch]);
        Check(distance <= loaded.tolerancePlusMm, "distance should stay within rule tolerance");
        if (firstDistance < 0.0) {
            firstDistance = distance;
        } else {
            Check(std::abs(distance - firstDistance) < 1e-9,
                  "distance should stay fixed across inches, not scale with inch");
        }
    }
}

void TestFindOrCreateProjectAndLoadRulesForProject() {
    database::Database db(":memory:");
    db.EnsureSchema();

    const int projectId1 = db.FindOrCreateProject("Default");
    const int projectId2 = db.FindOrCreateProject("Default");
    Check(projectId1 == projectId2, "FindOrCreateProject should return the same id for the same name");

    const int otherProjectId = db.FindOrCreateProject("Other");
    Check(otherProjectId != projectId1, "different project name should get a different id");

    db.SaveRule(projectId1, BuildBossScrewRule());
    auto secondRule = BuildBossScrewRule();
    secondRule.name = "두 번째 규칙";
    db.SaveRule(projectId1, secondRule);
    db.SaveRule(otherProjectId, BuildBossScrewRule());

    const auto rulesForProject1 = db.LoadRulesForProject(projectId1);
    Check(rulesForProject1.size() == 2, "should load only the rules belonging to this project");

    const auto rulesForOther = db.LoadRulesForProject(otherProjectId);
    Check(rulesForOther.size() == 1, "other project should have its own single rule");
}

// § 클릭 기준 좌표/도면 스펙(2026-10-30) - 저장 후 다시 불러와도 클릭 좌표(점/평면),
// 평면 법선, 원본 bbox, 인치별 도면 스펙이 그대로 남아야 한다. UpdateRule 경로도 확인한다
// (예전엔 UPDATE에서 check_point_category가 빠져 있었다).
void TestRefGeometryAndSpecRoundTrip() {
    database::Database db(":memory:");
    db.EnsureSchema();
    const int projectId = db.FindOrCreateProject("Default");

    const geometry::BoundingBox box{geometry::Vec3{-600.5, -350.25, 0.0}, geometry::Vec3{600.5, 350.25, 45.125}};
    rule::Rule r;
    r.name = "후크 높이";
    r.measurementType = rule::MeasurementType::PointToPlane;
    r.projection = "normal";
    r.referenceFrame = {"World_Origin"};
    rule::Anchor point{"single", "Edge_Midpoint", "", std::nullopt, std::nullopt};
    point.ref = rule::RefGeometry{geometry::Vec3{-590.123456789, 12.5, 30.0}, std::nullopt, box};
    r.anchors = {point};
    rule::PlaneRef plane{"Datum_Plane", ""};
    plane.ref = rule::RefGeometry{geometry::Vec3{-580.0, 0.0, 0.0}, geometry::Vec3{0.0, 0.0, -1.0}, box};
    r.referencePlane = plane;
    r.specByInch = {{55, 8.5}, {65, 8.75}};
    r.checkPointCategory = "Height";

    const int ruleId = db.SaveRule(projectId, r);
    const auto loaded = db.LoadRule(ruleId);
    Check(loaded.anchors.size() == 1 && loaded.anchors[0].ref.has_value(), "anchor ref should round-trip");
    if (!loaded.anchors.empty() && loaded.anchors[0].ref) {
        const auto& ref = *loaded.anchors[0].ref;
        Check(std::abs(ref.position.x - (-590.123456789)) < 1e-9, "anchor ref position should keep full precision");
        Check(!ref.normal.has_value(), "point ref should have no normal");
        Check(std::abs(ref.sourceBox.max.z - 45.125) < 1e-9, "anchor ref source box should round-trip");
    }
    Check(loaded.referencePlane && loaded.referencePlane->ref && loaded.referencePlane->ref->normal,
          "plane ref with normal should round-trip");
    if (loaded.referencePlane && loaded.referencePlane->ref && loaded.referencePlane->ref->normal) {
        Check(std::abs(loaded.referencePlane->ref->normal->z + 1.0) < 1e-12, "plane normal sign should be kept");
    }
    Check(loaded.specByInch.size() == 2 && std::abs(loaded.specByInch.at(65) - 8.75) < 1e-12,
          "per-inch drawing specs should round-trip");

    auto updated = loaded;
    updated.specByInch = {{75, 9.0}};
    updated.checkPointCategory = "Gap";
    db.UpdateRule(ruleId, updated);
    const auto reloaded = db.LoadRule(ruleId);
    Check(reloaded.specByInch.size() == 1 && reloaded.specByInch.count(75) == 1,
          "UpdateRule should replace drawing specs, not append");
    Check(reloaded.checkPointCategory == std::optional<std::string>("Gap"), "UpdateRule should save check point category");
    Check(reloaded.anchors.size() == 1 && reloaded.anchors[0].ref.has_value(), "UpdateRule should keep anchor ref");
}

} // namespace

int main() {
    TestRuleRoundTrip();
    TestFindOrCreateProjectAndLoadRulesForProject();
    TestRefGeometryAndSpecRoundTrip();

    if (g_failures == 0) {
        std::printf("All rule/database tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
