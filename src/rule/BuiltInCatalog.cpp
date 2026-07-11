#include "rule/BuiltInCatalog.h"

namespace rule {

std::vector<Rule> BuiltInPointRules() {
    std::vector<Rule> rules;

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

    return rules;
}

} // namespace rule
