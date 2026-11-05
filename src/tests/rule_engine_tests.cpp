#include "geometry/MockGeometryAdapter.h"
#include "rule/BuiltInCatalog.h"
#include "rule/Matching.h"
#include "rule/Measure.h"
#include "rule/PresetDimensions.h"
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

// § 인치 간 자동 매칭(2026-10-30) 테스트용 어댑터 - 인치마다 bbox 크기가 다른 모델을 손으로
// 구성한다. MockGeometryAdapter는 클릭 좌표/면 범위/점 후보 개념이 없어서 따로 둔다.
class FakeAdapter : public geometry::IGeometryAdapter {
public:
    struct Model {
        geometry::BoundingBox box;
        std::vector<geometry::AnchorCandidate> holes;
        std::vector<geometry::PlaneCandidate> planes;
        std::vector<geometry::Vec3> vertices;
        // § 주변 지문 검증 - (위치, 그 자리의 주변 지문). CaptureLocalPatch는 반경 안의 가장 가까운
        // 항목을 돌려주고, 없으면 "평평한 판" 지문을 돌려준다.
        std::vector<std::pair<geometry::Vec3, geometry::FeaturePatchDescriptor>> patches;
    };

    geometry::ModelHandle Add(Model model) {
        models_.push_back(std::move(model));
        return static_cast<geometry::ModelHandle>(models_.size() - 1);
    }

    geometry::ModelHandle LoadModel(const std::string&) override { return geometry::kInvalidModelHandle; }
    geometry::BoundingBox GetBoundingBox(geometry::ModelHandle h) const override { return models_[h].box; }
    std::vector<geometry::Vec3> GetVertices(geometry::ModelHandle) const override { return {}; }
    std::vector<geometry::AnchorCandidate> FindAnchorCandidates(
        geometry::ModelHandle h, const std::string& anchorType, const std::string&) const override {
        return anchorType == "Hole" ? models_[h].holes : std::vector<geometry::AnchorCandidate>{};
    }
    std::vector<geometry::PlaneCandidate> FindPlaneCandidates(
        geometry::ModelHandle h, const std::string&, const std::string&) const override {
        return models_[h].planes;
    }
    std::vector<geometry::FaceCandidate> FindFaceCandidates(
        geometry::ModelHandle, const std::string&, const std::string&) const override {
        return {};
    }
    std::vector<geometry::AnchorCandidate> FindPointCandidates(
        geometry::ModelHandle h, geometry::PointSubKind subKind, const geometry::Vec3& center,
        double radiusMm) const override {
        std::vector<geometry::AnchorCandidate> result;
        if (subKind != geometry::PointSubKind::Vertex) {
            return result;
        }
        for (const auto& v : models_[h].vertices) {
            if (rule::ComputePointToPointDistance(v, center) <= radiusMm) {
                geometry::AnchorCandidate c;
                c.position = v;
                result.push_back(c);
            }
        }
        return result;
    }
    geometry::FeaturePatchCapture CaptureLocalPatch(
        geometry::ModelHandle h, const geometry::Vec3& center, double radiusMm) const override {
        geometry::FeaturePatchCapture capture;
        capture.valid = true;
        capture.position = center;
        capture.descriptor = FlatPatch();
        double best = radiusMm;
        for (const auto& [position, descriptor] : models_[h].patches) {
            const double d = rule::ComputePointToPointDistance(position, center);
            if (d <= best) {
                best = d;
                capture.descriptor = descriptor;
            }
        }
        return capture;
    }

    static geometry::FeaturePatchDescriptor FlatPatch() {
        geometry::FeaturePatchDescriptor d;
        d.surfaceTypeCounts[0] = 1;
        d.faceCount = 1;
        d.bboxRatioZ = 0.0;
        return d;
    }

private:
    std::vector<Model> models_;
};

// 구멍 주변(원통 1 + 평면 2) / 보스 주변(원통 2 + 원뿔 1 + 평면 3) - 면 타입 구성이 달라 유사도가 낮다.
geometry::FeaturePatchDescriptor HolePatch() {
    geometry::FeaturePatchDescriptor d;
    d.surfaceTypeCounts[0] = 2;
    d.surfaceTypeCounts[1] = 1;
    d.faceCount = 3;
    d.bboxRatioZ = 0.3;
    d.dominantRadiusRatio = 0.1;
    return d;
}

geometry::FeaturePatchDescriptor BossPatch() {
    geometry::FeaturePatchDescriptor d;
    d.surfaceTypeCounts[1] = 2;
    d.surfaceTypeCounts[2] = 1;
    d.faceCount = 6;
    d.bboxRatioZ = 1.0;
    d.dominantRadiusRatio = 0.6;
    return d;
}

geometry::AnchorCandidate MakeHole(double x, double y, double z, double diameter) {
    geometry::AnchorCandidate c;
    c.anchorType = "Hole";
    c.position = geometry::Vec3{x, y, z};
    c.diameterMm = diameter;
    c.axis = geometry::Vec3{0.0, 0.0, 1.0};
    return c;
}

geometry::PlaneCandidate MakePlane(double z, double normalZ, double xMin, double xMax, double yHalf) {
    geometry::PlaneCandidate p;
    p.planeType = "Datum_Plane";
    p.pointOnPlane = geometry::Vec3{(xMin + xMax) * 0.5, 0.0, z};
    p.normal = geometry::Vec3{0.0, 0.0, normalZ};
    p.faceBox = geometry::BoundingBox{geometry::Vec3{xMin, -yHalf, z}, geometry::Vec3{xMax, yHalf, z}};
    p.hasFaceBox = true;
    return p;
}

// 55"/65" 두 모델 - 왼쪽 외곽에서 10mm 안쪽의 구멍(Ø3)과, 그 옆 100mm 떨어진 같은 지름의 구멍
// (오답 후보), 중앙 구멍 2개. 왼쪽 선반면(z=5, +Z)과 바닥면(z=0, -Z), 오른쪽 멀리 있는 다른
// 높이의 면(z=10, +Z). 후크 끝 꼭짓점은 왼쪽 외곽에서 10mm, 높이 30.
FakeAdapter::Model MakeChassis(double halfWidth, double halfHeight) {
    FakeAdapter::Model m;
    m.box = geometry::BoundingBox{geometry::Vec3{-halfWidth, -halfHeight, 0.0}, geometry::Vec3{halfWidth, halfHeight, 40.0}};
    m.holes = {MakeHole(-halfWidth + 10.0, 0.0, 20.0, 3.0), MakeHole(-halfWidth + 10.0, 100.0, 20.0, 3.0),
               MakeHole(0.0, 0.0, 20.0, 3.0), MakeHole(50.0, 0.0, 20.0, 3.0)};
    m.planes = {MakePlane(0.0, -1.0, -halfWidth, halfWidth, halfHeight),
                MakePlane(5.0, 1.0, -halfWidth, -halfWidth + 100.0, halfHeight),
                MakePlane(10.0, 1.0, 0.0, 100.0, halfHeight)};
    m.vertices = {geometry::Vec3{-halfWidth + 10.0, 0.0, 30.0}, geometry::Vec3{-halfWidth + 10.0, 3.0, 30.0}};
    return m;
}

rule::RefGeometry Ref(double x, double y, double z, const geometry::BoundingBox& box,
                      std::optional<geometry::Vec3> normal = std::nullopt) {
    return rule::RefGeometry{geometry::Vec3{x, y, z}, normal, box};
}

void TestPredictPositionKeepsOffsetFromNearestReference() {
    const geometry::BoundingBox small{geometry::Vec3{-600, -350, 0}, geometry::Vec3{600, 350, 40}};
    const geometry::BoundingBox large{geometry::Vec3{-700, -400, 0}, geometry::Vec3{700, 400, 40}};
    const auto nearLeft = rule::PredictPosition(geometry::Vec3{-590, 0, 20}, small, large);
    Check(std::abs(nearLeft.x - (-690)) < 1e-9, "point near min X should keep its offset from min X");
    Check(std::abs(nearLeft.y) < 1e-9, "point at center Y should stay at center Y");
    const auto nearRight = rule::PredictPosition(geometry::Vec3{580, 340, 20}, small, large);
    Check(std::abs(nearRight.x - 680) < 1e-9 && std::abs(nearRight.y - 390) < 1e-9,
          "point near max X/Y should keep its offset from max X/Y");
    const auto same = rule::PredictPosition(geometry::Vec3{123.4, -56.7, 8.9}, small, small);
    Check(std::abs(same.x - 123.4) < 1e-9 && std::abs(same.y + 56.7) < 1e-9 && std::abs(same.z - 8.9) < 1e-9,
          "same source and target box should return the clicked point unchanged");
}

void TestPickUniqueNearestRejectsAmbiguousAndFarCandidates() {
    const auto never = [](std::size_t, std::size_t) { return false; };
    const auto ok = rule::PickUniqueNearest({0.1, 8.0, 30.0}, never, 5.0);
    Check(ok.index == std::optional<std::size_t>(0), "clear nearest candidate should be picked");
    const auto ambiguous = rule::PickUniqueNearest({1.0, 1.5}, never, 5.0);
    Check(!ambiguous.index && !ambiguous.error.empty(), "two similarly close candidates should be rejected");
    const auto far = rule::PickUniqueNearest({7.0}, never, 5.0);
    Check(!far.index, "nearest candidate outside the radius should be rejected");
    const auto duplicates = rule::PickUniqueNearest(
        {0.0, 0.01, 9.0}, [](std::size_t i, std::size_t j) { return i + j == 1; }, 5.0);
    Check(duplicates.index.has_value(), "candidates marked as the same feature should not count as ambiguity");
}

void TestClickedHolesMatchAcrossInchesAndSpecJudgement() {
    FakeAdapter adapter;
    const auto h55 = adapter.Add(MakeChassis(600.0, 350.0));
    const auto h65 = adapter.Add(MakeChassis(700.0, 400.0));
    auto missing = MakeChassis(800.0, 450.0);
    missing.holes.erase(missing.holes.begin()); // 75": 왼쪽 구멍이 없음 -> 이 인치만 실패해야 함
    const auto h75 = adapter.Add(missing);
    const geometry::BoundingBox box55 = adapter.GetBoundingBox(h55);

    rule::Rule r;
    r.name = "구멍 간 거리";
    r.measurementType = rule::MeasurementType::PointToPoint;
    r.projection = "3D";
    rule::Anchor a{"A", "Hole", "", std::optional<std::string>("diameter"), std::optional<double>(3.0)};
    a.ref = Ref(-590.0, 0.0, 20.0, box55);
    rule::Anchor b{"B", "Hole", "", std::optional<std::string>("diameter"), std::optional<double>(3.0)};
    b.ref = Ref(0.0, 0.0, 20.0, box55);
    r.anchors = {a, b};
    r.tolerancePlusMm = 0.3;
    r.toleranceMinusMm = 0.3;
    r.specByInch = {{55, 590.0}, {65, 690.5}};

    const auto results = rule::RuleEngine::Evaluate(adapter, {{55, h55}, {65, h65}, {75, h75}}, r);
    Check(results.size() == 3, "should report every inch even when one fails");
    Check(results[0].ok && std::abs(results[0].value - 590.0) < 1e-9, "55\" should pick the clicked holes");
    Check(results[0].withinTolerance && results[0].spec.has_value(), "55\" should pass against its drawing spec");
    Check(results[1].ok && std::abs(results[1].value - 690.0) < 1e-9,
          "65\" should follow the edge-relative hole, not scale or jump to the distractor");
    Check(!results[1].withinTolerance && std::abs(results[1].deviation + 0.5) < 1e-9,
          "65\" should fail against its own drawing spec (690.5 +/-0.3)");
    Check(!results[2].ok && !results[2].error.empty(), "75\" with the hole missing should fail with a reason");
}

void TestClickedPlaneAndPointHeightAndGap() {
    FakeAdapter adapter;
    const auto h55 = adapter.Add(MakeChassis(600.0, 350.0));
    const auto h65 = adapter.Add(MakeChassis(700.0, 400.0));
    const geometry::BoundingBox box55 = adapter.GetBoundingBox(h55);

    rule::Rule height;
    height.name = "후크 높이";
    height.measurementType = rule::MeasurementType::PointToPlane;
    height.projection = "normal";
    rule::Anchor tip{"single", "Vertex", "", std::nullopt, std::nullopt};
    tip.ref = Ref(-590.0, 0.0, 30.0, box55);
    height.anchors = {tip};
    rule::PlaneRef shelf{"Datum_Plane", ""};
    shelf.ref = Ref(-550.0, 0.0, 5.0, box55, geometry::Vec3{0.0, 0.0, 1.0});
    height.referencePlane = shelf;
    for (const auto& result : rule::RuleEngine::Evaluate(adapter, {{55, h55}, {65, h65}}, height)) {
        Check(result.ok && std::abs(result.value - 25.0) < 1e-9,
              "height should use the clicked shelf plane (z=5), not the first or a far plane");
    }

    rule::Rule gap;
    gap.name = "선반-바닥 간격";
    gap.measurementType = rule::MeasurementType::FaceToFaceGap;
    rule::Anchor faceA{"A", "Datum_Plane", "", std::nullopt, std::nullopt};
    faceA.ref = Ref(-550.0, 0.0, 5.0, box55, geometry::Vec3{0.0, 0.0, 1.0});
    rule::Anchor faceB{"B", "Datum_Plane", "", std::nullopt, std::nullopt};
    faceB.ref = Ref(-550.0, 0.0, 0.0, box55, geometry::Vec3{0.0, 0.0, -1.0});
    gap.anchors = {faceA, faceB};
    for (const auto& result : rule::RuleEngine::Evaluate(adapter, {{55, h55}, {65, h65}}, gap)) {
        Check(result.ok && std::abs(result.value - 5.0) < 1e-9, "gap between the two clicked parallel planes");
    }

    rule::Rule legacy = height;
    legacy.anchors[0].ref.reset();
    const auto legacyResults = rule::RuleEngine::Evaluate(adapter, {{55, h55}}, legacy);
    Check(!legacyResults[0].ok && legacyResults[0].error.find("다시 지정") != std::string::npos,
          "clicked-point rule without stored coordinates should ask to re-pick, not guess");
}

// § 주변 지문 검증(2026-11-04) - 저장할 때 기준 인치에서 ref를 실제 형상 위치로 맞추고 지문을 붙인다.
// 다른 인치에서 같은 자리에 "유일한" 구멍이 있어도, 주변 형상이 다르면(구멍 대신 보스) 실패해야 한다.
void TestLocalPatchRejectsDifferentFeatureAtPredictedSpot() {
    FakeAdapter adapter;
    auto m55 = MakeChassis(600.0, 350.0);
    m55.patches = {{geometry::Vec3{-590.0, 0.0, 20.0}, HolePatch()}};
    auto m65 = MakeChassis(700.0, 400.0);
    m65.patches = {{geometry::Vec3{-690.0, 0.0, 20.0}, HolePatch()}};
    auto m75 = MakeChassis(800.0, 450.0);
    m75.patches = {{geometry::Vec3{-790.0, 0.0, 20.0}, BossPatch()}};
    const auto h55 = adapter.Add(m55);
    const auto h65 = adapter.Add(m65);
    const auto h75 = adapter.Add(m75);

    rule::Anchor a{"A", "Hole", "", std::optional<std::string>("diameter"), std::optional<double>(3.0)};
    a.ref = Ref(-590.0, 0.4, 22.5, adapter.GetBoundingBox(h55)); // 원통 축 위 아무 데나 클릭한 셈
    rule::RuleEngine::FinalizeAnchorRef(adapter, h55, a);
    Check(a.ref->localPatch.has_value() && a.ref->localPatchRadiusMm > 0.0, "finalize should attach the local patch");
    Check(std::abs(a.ref->position.y) < 1e-9 && std::abs(a.ref->position.z - 20.0) < 1e-9,
          "finalize should snap the ref to the matched hole position");

    rule::Anchor b{"B", "Hole", "", std::optional<std::string>("diameter"), std::optional<double>(3.0)};
    b.ref = Ref(0.0, 0.0, 20.0, adapter.GetBoundingBox(h55));
    rule::RuleEngine::FinalizeAnchorRef(adapter, h55, b);

    rule::Rule r;
    r.name = "구멍 간 거리";
    r.measurementType = rule::MeasurementType::PointToPoint;
    r.projection = "3D";
    r.anchors = {a, b};
    const auto results = rule::RuleEngine::Evaluate(adapter, {{55, h55}, {65, h65}, {75, h75}}, r);
    Check(results[0].ok && results[1].ok, "same surroundings should pass the local patch check");
    Check(!results[2].ok && results[2].error.find("주변 형상") != std::string::npos,
          "different surroundings at the predicted spot should fail with a reason");
}

// § 국부 기준(측정면 원칙 3) - 후크 끝은 중앙 기준으로, 그 옆 선반면은 왼쪽 외곽 기준으로 예측되는
// 위치라서, 외곽 기준으로 선반면을 찾으면 65"에서 엉뚱한 평면(z=8)을 잡는다. 매칭된 후크 끝에서의
// 상대 위치로 찾으면 바로 옆 선반면(z=5)을 잡아야 한다.
void TestLocalReferencePlaneFollowsMatchedPoint() {
    const auto makeModel = [](double halfWidth, bool withDistractor) {
        FakeAdapter::Model m;
        m.box = geometry::BoundingBox{geometry::Vec3{-halfWidth, -350.0, 0.0}, geometry::Vec3{halfWidth, 350.0, 40.0}};
        m.vertices = {geometry::Vec3{-290.0, 0.0, 30.0}};
        m.planes = {MakePlane(5.0, 1.0, -320.0, -300.0, 10.0)};
        if (withDistractor) {
            // 외곽 기준 예측 위치(-halfWidth + 290) 바로 아래의 다른 높이 평면.
            m.planes.push_back(MakePlane(8.0, 1.0, -halfWidth + 280.0, -halfWidth + 300.0, 10.0));
        }
        return m;
    };
    FakeAdapter adapter;
    const auto h55 = adapter.Add(makeModel(600.0, false));
    const auto h65 = adapter.Add(makeModel(700.0, true));
    const auto box55 = adapter.GetBoundingBox(h55);

    rule::Rule height;
    height.name = "후크 높이";
    height.measurementType = rule::MeasurementType::PointToPlane;
    height.projection = "normal";
    rule::Anchor tip{"single", "Vertex", "", std::nullopt, std::nullopt};
    tip.ref = Ref(-290.0, 0.0, 30.0, box55);
    height.anchors = {tip};
    rule::PlaneRef shelf{"Datum_Plane", ""};
    shelf.ref = Ref(-310.0, 0.0, 5.0, box55, geometry::Vec3{0.0, 0.0, 1.0});
    height.referencePlane = shelf;
    const auto results = rule::RuleEngine::Evaluate(adapter, {{55, h55}, {65, h65}}, height);
    Check(results[0].ok && std::abs(results[0].value - 25.0) < 1e-9, "55\" height from the local shelf");
    Check(results[1].ok && std::abs(results[1].value - 25.0) < 1e-9,
          "65\" should find the shelf next to the matched tip, not the plane at the outline-predicted spot");
}

void TestTrendOutlierNeedsFourPointsAndPicksWorst() {
    const auto outlier = rule::FindTrendOutlier({{43, 10.0}, {50, 10.0}, {55, 10.05}, {65, 10.0}, {75, 13.0}}, 0.5);
    Check(outlier && outlier->index == 4, "the jumping inch should be the outlier");
    const auto scaled = rule::FindTrendOutlier({{43, 100.0}, {55, 127.9}, {65, 151.2}, {75, 174.4}}, 0.5);
    Check(!scaled, "values that grow linearly with inch are consistent");
    const auto three = rule::FindTrendOutlier({{43, 10.0}, {55, 10.0}, {65, 30.0}}, 0.5);
    Check(!three, "three points are too few to tell which one jumps");
}

void TestConsistencyWarningsMarkSuspectValues() {
    rule::Rule r;
    r.measurementType = rule::MeasurementType::PointToPoint;
    r.tolerancePlusMm = 0.1;
    r.toleranceMinusMm = 0.1;
    std::vector<rule::InchResult> results(5);
    const int inches[] = {43, 50, 55, 65, 75};
    const double values[] = {10.0, 10.0, 10.02, 10.0, 12.0};
    for (int i = 0; i < 5; ++i) {
        results[i].inch = inches[i];
        results[i].value = values[i];
    }
    results[1].spec = 8.0; // 도면 스펙 대비 +2mm - 공차(±0.1)의 5배(0.5mm)를 훨씬 넘음
    results[1].deviation = 2.0;
    results[3].ok = false;  // 실패한 인치는 추세 계산에서 빠진다
    rule::RuleEngine::AddConsistencyWarnings(r, results);
    Check(results[1].warning.find("도면 스펙") != std::string::npos, "far-off value against its spec should warn");
    Check(results[4].warning.find("추세") != std::string::npos, "the inch that breaks the trend should warn");
    Check(results[0].warning.empty() && results[2].warning.empty(), "consistent inches should not warn");

    rule::Rule size = r;
    size.measurementType = rule::MeasurementType::OverallSize;
    std::vector<rule::InchResult> sizeResults = results;
    for (auto& result : sizeResults) {
        result.warning.clear();
    }
    rule::RuleEngine::AddConsistencyWarnings(size, sizeResults);
    Check(sizeResults[1].warning.empty() && sizeResults[4].warning.empty(), "overall size needs no matching check");
}

// § 프리셋 자동 치수(2026-11-05) - 축 수직 평면 하나(faceBox는 그 평면 위의 범위).
geometry::PlaneCandidate AxisFace(int axis, double sign, const geometry::Vec3& lo, const geometry::Vec3& hi) {
    geometry::PlaneCandidate p;
    p.planeType = "Datum_Plane";
    p.normal = geometry::Vec3{axis == 0 ? sign : 0.0, axis == 1 ? sign : 0.0, axis == 2 ? sign : 0.0};
    p.faceBox = geometry::BoundingBox{lo, hi};
    p.hasFaceBox = true;
    p.pointOnPlane = geometry::Vec3{(lo.x + hi.x) * 0.5, (lo.y + hi.y) * 0.5, (lo.z + hi.z) * 0.5};
    return p;
}

// 왼쪽 외곽에서 10mm 안쪽에 선 10x10x10 후크(바닥은 판에 붙어 있어 -Z 끝면 없음) + 제품 외곽 6면 + 판 윗면.
struct HookModel {
    FakeAdapter::Model model;
    geometry::PatchCandidate hook;
};
HookModel MakeHookModel(double w, double h) {
    using geometry::Vec3;
    HookModel m;
    m.model.box = geometry::BoundingBox{Vec3{-w, -h, 0.0}, Vec3{w, h, 40.0}};
    const double x0 = -w + 10.0;
    const double x1 = -w + 20.0;
    m.hook.hasBox = true;
    m.hook.box = geometry::BoundingBox{Vec3{x0, -5.0, 5.0}, Vec3{x1, 5.0, 15.0}};
    m.hook.planes = {AxisFace(0, -1.0, Vec3{x0, -5, 5}, Vec3{x0, 5, 15}), AxisFace(0, 1.0, Vec3{x1, -5, 5}, Vec3{x1, 5, 15}),
                     AxisFace(1, -1.0, Vec3{x0, -5, 5}, Vec3{x1, -5, 15}), AxisFace(1, 1.0, Vec3{x0, 5, 5}, Vec3{x1, 5, 15}),
                     AxisFace(2, 1.0, Vec3{x0, -5, 15}, Vec3{x1, 5, 15})};
    m.model.planes = m.hook.planes;
    const std::vector<geometry::PlaneCandidate> outline = {
        AxisFace(0, -1.0, Vec3{-w, -h, 0}, Vec3{-w, h, 40}), AxisFace(0, 1.0, Vec3{w, -h, 0}, Vec3{w, h, 40}),
        AxisFace(1, -1.0, Vec3{-w, -h, 0}, Vec3{w, -h, 40}), AxisFace(1, 1.0, Vec3{-w, h, 0}, Vec3{w, h, 40}),
        AxisFace(2, -1.0, Vec3{-w, -h, 0}, Vec3{w, h, 0}),   AxisFace(2, 1.0, Vec3{-w, -h, 40}, Vec3{w, h, 40}),
        AxisFace(2, 1.0, Vec3{-w, -h, 5}, Vec3{w, h, 5})};
    m.model.planes.insert(m.model.planes.end(), outline.begin(), outline.end());
    return m;
}

void TestPresetDimensionCandidatesAndRulesAcrossInches() {
    const HookModel m55 = MakeHookModel(600.0, 350.0);
    const HookModel m65 = MakeHookModel(700.0, 400.0);
    const auto candidates = rule::ComputePresetDimensionCandidates(m55.hook, m55.model.box, m55.model.planes);
    // -Z 끝면이 없어(판에 붙음) 크기 Z는 없고, 위치 Z는 가까운 -Z 외곽 대신 +Z 외곽(윗면 -> 후크 윗면)에서 잰다.
    Check(candidates.size() == 5, "hook should give size X/Y and position X/Y/Z");
    if (candidates.size() != 5) {
        return;
    }
    Check(candidates[0].kind == rule::PresetDimensionKind::ShapeSize && candidates[0].axis == 0 &&
              std::abs(candidates[0].valueMm - 10.0) < 1e-9,
          "size X between the hook's two X end faces");
    Check(candidates[1].kind == rule::PresetDimensionKind::ShapeSize && candidates[1].axis == 1,
          "size Y between the hook's two Y end faces");
    Check(candidates[2].kind == rule::PresetDimensionKind::FromOutline && candidates[2].axis == 0 &&
              !candidates[2].outlineAtMax && std::abs(candidates[2].valueMm - 10.0) < 1e-9,
          "position X from the nearer (-X) outline face");
    Check(candidates[3].kind == rule::PresetDimensionKind::FromOutline && candidates[3].axis == 1 &&
              std::abs(candidates[3].valueMm - 345.0) < 1e-9,
          "position Y from the -Y outline face");
    Check(candidates[4].kind == rule::PresetDimensionKind::FromOutline && candidates[4].axis == 2 &&
              candidates[4].outlineAtMax && std::abs(candidates[4].valueMm - 25.0) < 1e-9,
          "position Z falls back to the +Z outline face when the near side has no shape end face");

    FakeAdapter adapter;
    const auto h55 = adapter.Add(m55.model);
    const auto h65 = adapter.Add(m65.model);
    const double expected65[5] = {10.0, 10.0, 10.0, 395.0, 25.0};
    for (size_t i = 0; i < candidates.size(); ++i) {
        rule::Rule r = rule::MakePresetDimensionRule(candidates[i], "hook", adapter.GetBoundingBox(h55));
        rule::RuleEngine::FinalizePlaneRef(adapter, h55, *r.anchors[0].ref);
        rule::RuleEngine::FinalizePlaneRef(adapter, h55, *r.anchors[1].ref);
        const auto results = rule::RuleEngine::Evaluate(adapter, {{55, h55}, {65, h65}}, r);
        Check(results[0].ok && std::abs(results[0].value - candidates[i].valueMm) < 1e-9,
              "the saved rule should measure the previewed value on the source inch");
        Check(results[1].ok && std::abs(results[1].value - expected65[i]) < 1e-9,
              "the saved rule should follow the hook on the larger inch");
    }
}

} // namespace

int main() {
    // § 예외 = 조용한 무한 행(2026-09-18) - 이 환경에서는 테스트 함수가 던진 예외를 못
    // 잡으면(std::terminate) 콘솔에 아무 메시지도 없이 프로세스가 멈춰버린다(Windows
    // 오류 보고 다이얼로그가 헤드리스 쉘에서 응답을 못 받는 것으로 추정) - 실제로
    // MockGeometryAdapter의 Boss-Screw 픽스처에 diameterMm 누락 버그가 있었을 때 이렇게
    // 재현됐다(버그 자체는 수정됨). 앞으로 같은 상황에서 "테스트가 멈췄다"가 아니라
    // 원인이 콘솔에 바로 보이도록 각 테스트를 try/catch로 감싼다.
    struct NamedTest {
        const char* name;
        void (*fn)();
    };
    const NamedTest tests[] = {
        {"TestNearestPairPicksTrueMatchAmongDistractors", TestNearestPairPicksTrueMatchAmongDistractors},
        {"TestBossScrewRuleAcrossInches", TestBossScrewRuleAcrossInches},
        {"TestHookHeightRuleAcrossInches", TestHookHeightRuleAcrossInches},
        {"TestHookCatchRuleAcrossInches", TestHookCatchRuleAcrossInches},
        {"TestRibOpenCellGapRuleAcrossInches", TestRibOpenCellGapRuleAcrossInches},
        {"TestWallThicknessRuleAcrossInches", TestWallThicknessRuleAcrossInches},
        {"TestOverallSizeRulesScaleWithInchExceptDepth", TestOverallSizeRulesScaleWithInchExceptDepth},
        {"TestParallelFacePairRejectsNonAntiParallelCandidates", TestParallelFacePairRejectsNonAntiParallelCandidates},
        {"TestPredictPositionKeepsOffsetFromNearestReference", TestPredictPositionKeepsOffsetFromNearestReference},
        {"TestPickUniqueNearestRejectsAmbiguousAndFarCandidates", TestPickUniqueNearestRejectsAmbiguousAndFarCandidates},
        {"TestClickedHolesMatchAcrossInchesAndSpecJudgement", TestClickedHolesMatchAcrossInchesAndSpecJudgement},
        {"TestClickedPlaneAndPointHeightAndGap", TestClickedPlaneAndPointHeightAndGap},
        {"TestLocalPatchRejectsDifferentFeatureAtPredictedSpot", TestLocalPatchRejectsDifferentFeatureAtPredictedSpot},
        {"TestLocalReferencePlaneFollowsMatchedPoint", TestLocalReferencePlaneFollowsMatchedPoint},
        {"TestTrendOutlierNeedsFourPointsAndPicksWorst", TestTrendOutlierNeedsFourPointsAndPicksWorst},
        {"TestConsistencyWarningsMarkSuspectValues", TestConsistencyWarningsMarkSuspectValues},
        {"TestPresetDimensionCandidatesAndRulesAcrossInches", TestPresetDimensionCandidatesAndRulesAcrossInches},
    };
    for (const auto& test : tests) {
        try {
            test.fn();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "FAIL: %s threw: %s\n", test.name, e.what());
            ++g_failures;
        }
    }

    if (g_failures == 0) {
        std::printf("All rule engine tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
