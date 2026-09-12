#include "rule/BuiltInCatalog.h"

#include <algorithm>

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

    // § 예시 카탈로그 정리(2026-09-11) - "Boss-Screw/후크/Rib 항목은 삭제하고 전장
    // 사이즈만 남겨달라"는 요청. 실제 대상 모델에는 Hole만 있고 Hook/Boss/Rib류 형상이
    // 없어서(사용자 확인) 이 문서 예시(docs/rule_catalog.md) 기반 규칙들은 실제로는 항상
    // 매칭 실패만 나고 "치수 비교 테이블"만 어지럽혔다. 이런 형상별 규칙은 이제 사용자가
    // "규칙 관리 > + 새 규칙 추가"(RuleEditorDialog, 라이브 3D 피킹)로 직접 만든다 -
    // 앱이 미리 하드코딩해서 보여줄 이유가 없다. Boss-Screw/후크/Rib/살두께 규칙
    // 정의였던 코드(PointToPoint/PointToPlane/AxisProjection/FaceToFaceGap x2 측정
    // 로직 테스트용)는 rule_engine_tests.cpp로 옮겨서 엔진 자체 테스트 커버리지는
    // 그대로 유지한다.

    return rules;
}

std::vector<Rule> MergeWithBuiltIns(const std::vector<Rule>& userRules) {
    std::vector<Rule> merged = BuiltInRules();
    for (const auto& userRule : userRules) {
        auto it = std::find_if(merged.begin(), merged.end(),
                                [&](const Rule& r) { return r.name == userRule.name; });
        if (it != merged.end()) {
            *it = userRule; // 이름이 같은 사용자 저장본이 내장 기본값을 덮어씀
        } else {
            merged.push_back(userRule);
        }
    }
    return merged;
}

} // namespace rule
