#pragma once

#include "geometry/IGeometryAdapter.h"
#include "rule/Rule.h"

#include <map>
#include <vector>

namespace rule {

struct InchResult {
    int inch = 0;
    double value = 0.0;
    double deltaFromBaseline = 0.0; // 기준 인치(목록 중 가장 작은 인치) 대비 편차
    bool withinTolerance = false;
};

// Reference Frame(§5, 실제로는 절대좌표 그대로 사용) + Selector + Measurement를 묶어
// 규칙 하나를 여러 인치에 대해 평가한다.
//
// 공차 판정 방식: Rule 스키마에 목표(nominal) 값이 없으므로, handlesByInch에서 가장
// 작은 인치의 측정값을 기준선으로 삼고 나머지 인치가 거기서 얼마나 벗어났는지로
// tolerance_plus_mm/tolerance_minus_mm를 판정한다. 이 툴의 목적 자체가 "인치 간
// 비교"이므로 채택한 해석이며, 실제 회사 데이터 연동 시 조정될 수 있다.
class RuleEngine {
public:
    static std::vector<InchResult> Evaluate(
        const geometry::IGeometryAdapter& adapter,
        const std::map<int, geometry::ModelHandle>& handlesByInch,
        const Rule& rule);
};

} // namespace rule
