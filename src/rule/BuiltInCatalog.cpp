#include "rule/BuiltInCatalog.h"

namespace rule {

std::vector<Rule> BuiltInRules() {
    std::vector<Rule> rules;

    // 000. 전장 사이즈 (X/Y/Z) - 기본 세팅 항목. anchor 탐색 없이 BoundingBox로 직접 계산.
    // 인치마다 값이 다른 게 정상이라 공차 판정은 하지 않는다 (RuleEngine::Evaluate 참고).
    for (const std::string& axis : {"X", "Y", "Z"}) {
        Rule r;
        r.name = "000. 전장 사이즈 (" + axis + ")";
        r.referenceFrame = {"World_Origin"};
        r.measurementType = MeasurementType::OverallSize;
        r.projection = axis;
        rules.push_back(r);
    }

    // 001. Boss-Screw 체결 정렬 (docs/rule_catalog.md)
    {
        Rule r;
        r.name = "001. Boss-Screw 체결 정렬";
        r.anchors = {
            Anchor{"A", "Hole", "Bezel", std::optional<std::string>("diameter"), std::optional<double>(2.8)},
            Anchor{"B", "Boss_Center", "Rear_Chassis", std::optional<std::string>("diameter"), std::optional<double>(2.6)},
        };
        r.referenceFrame = {"World_Origin", "Datum_CSYS"};
        r.selector = {"nearest_pair"};
        r.measurementType = MeasurementType::PointToPoint;
        r.projection = "3D";
        r.tolerancePlusMm = 0.15;
        r.toleranceMinusMm = 0.15;
        rules.push_back(r);
    }

    // 002. 후크 높이
    {
        Rule r;
        r.name = "002. 후크 높이";
        r.anchors = {
            Anchor{"single", "Hook_Tip_Edge", "Side_Frame", std::nullopt, std::nullopt},
        };
        r.referencePlane = PlaneRef{"Datum_Plane", "Rear_Chassis"};
        r.referenceFrame = {"World_Origin"};
        r.selector = {"leftmost"};
        r.measurementType = MeasurementType::PointToPlane;
        r.projection = "normal";
        r.tolerancePlusMm = 0.1;
        r.toleranceMinusMm = 0.1;
        rules.push_back(r);
    }

    // 003. 후크 걸림량 (전장길이류) - selector 없음: anchor당 단일 후보 전제
    {
        Rule r;
        r.name = "003. 후크 걸림량";
        r.anchors = {
            Anchor{"A", "Hook_Catch_Edge", "Side_Frame", std::nullopt, std::nullopt},
            Anchor{"B", "Wall_Inner_Edge", "Front_Bezel", std::nullopt, std::nullopt},
        };
        r.referenceFrame = {"World_Origin"};
        r.measurementType = MeasurementType::AxisProjection;
        r.projection = "Z";
        r.tolerancePlusMm = 0.3;
        r.toleranceMinusMm = 0.1;
        rules.push_back(r);
    }

    // 004. Rib-OpenCell Gap - selector: nearest_face_pair
    {
        Rule r;
        r.name = "004. Rib-OpenCell Gap";
        r.anchors = {
            Anchor{"A", "Rib_Top_Surface", "Rib", std::nullopt, std::nullopt},
            Anchor{"B", "OpenCell_Edge", "OpenCell", std::nullopt, std::nullopt},
        };
        r.referenceFrame = {"World_Origin"};
        r.selector = {"nearest_face_pair"};
        r.measurementType = MeasurementType::FaceToFaceGap;
        r.tolerancePlusMm = 0.2;
        r.toleranceMinusMm = 0.2;
        rules.push_back(r);
    }

    // 005. 살두께 (Boss Root Wall Thickness) - selector: parallel_face_pair
    {
        Rule r;
        r.name = "005. 살두께";
        r.anchors = {
            Anchor{"A", "Boss_Outer_Wall", "Boss", std::nullopt, std::nullopt},
            Anchor{"B", "Boss_Inner_Wall", "Boss", std::nullopt, std::nullopt},
        };
        r.referenceFrame = {"World_Origin"};
        r.selector = {"parallel_face_pair"};
        r.measurementType = MeasurementType::FaceToFaceGap;
        r.projection = "normal";
        r.tolerancePlusMm = 0.05;
        r.toleranceMinusMm = 0.05;
        rules.push_back(r);
    }

    // 006. 구멍 개수 - 지오메트리 인식 파이프라인 검증용(§ 화면설정 다음 작업, Pitch
    // 논의). anchor_type="Hole"은 StepGeometryAdapter가 이미 원통면 전부를 후보로
    // 반환하므로, diameter 필터 없이도 실제 STEP 샘플에서 바로 개수가 나온다.
    {
        Rule r;
        r.name = "006. 구멍 개수 (Hole)";
        r.anchors = {Anchor{"single", "Hole", "", std::nullopt, std::nullopt}};
        r.referenceFrame = {"World_Origin"};
        r.measurementType = MeasurementType::InstanceCount;
        rules.push_back(r);
    }

    // 007. 구멍 최소 간격 (Pitch) - X축 기준 정렬 후 인접 간격 중 최솟값. 후크 간격
    // 측정을 위해 설계했지만 Hook_Tip_Edge 인식이 아직 없어서(§ 논의), 이미 되는
    // Hole로 먼저 파이프라인 자체를 검증한다 - 나중에 후크 인식이 생기면 anchor_type만
    // 바꿔 끼우면 된다.
    {
        Rule r;
        r.name = "007. 구멍 최소 간격 (Pitch, X축)";
        r.anchors = {Anchor{"single", "Hole", "", std::nullopt, std::nullopt}};
        r.referenceFrame = {"World_Origin"};
        r.measurementType = MeasurementType::MinPitch;
        r.projection = "X";
        r.tolerancePlusMm = 0.1;
        r.toleranceMinusMm = 0.1;
        rules.push_back(r);
    }

    return rules;
}

} // namespace rule
