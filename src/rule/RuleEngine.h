#pragma once

#include "geometry/IGeometryAdapter.h"
#include "rule/Rule.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rule {

struct InchResult {
    int inch = 0;
    // § 인치별 오류 분리(2026-10-30) - false면 이 인치는 값을 못 냈다(매칭 실패 등). value/
    // 편차는 무의미하고 error에 사유가 담긴다. 예전엔 인치 하나만 실패해도 예외가 규칙 전체로
    // 번져서 성공한 인치의 값까지 화면에서 사라졌다.
    bool ok = true;
    std::string error;
    double value = 0.0;
    // 이 인치의 도면 치수(Rule::specByInch). 없으면 비교 모드(아래 deltaFromBaseline으로 판정).
    std::optional<double> spec;
    // spec이 있으면 value - spec, 없으면 deltaFromBaseline과 같다 - 화면/CSV가 "판정에
    // 실제로 쓴 편차"를 하나의 필드로 보여주기 위해.
    double deviation = 0.0;
    double deltaFromBaseline = 0.0; // 기준 인치(값을 낸 인치 중 가장 작은 인치) 대비 편차
    bool withinTolerance = false;
};

// Reference Frame(§5, 실제로는 절대좌표 그대로 사용) + Selector + Measurement를 묶어
// 규칙 하나를 여러 인치에 대해 평가한다.
//
// 후보 선택: anchor/기준 평면에 클릭 좌표(RefGeometry)가 있으면 그 자리를 인치별 모델로 옮겨
// 가장 가까운 "유일한" 후보를 쓴다(rule::Matching). 없으면(예전 규칙) selector 규칙으로 고른다.
//
// 공차 판정(§ 도면 스펙, 2026-10-30): 인치별 도면 스펙(= nominal)이 있으면 value - spec이
// [-toleranceMinusMm, +tolerancePlusMm] 안인지로 판정한다. 없으면 예전처럼 가장 작은 인치
// 대비 편차로 비교만 한다(비교 모드 - 도면 합격 판정이 아니라 인치 간 일관성 확인).
class RuleEngine {
public:
    static std::vector<InchResult> Evaluate(
        const geometry::IGeometryAdapter& adapter,
        const std::map<int, geometry::ModelHandle>& handlesByInch,
        const Rule& rule);

    // 규칙 관리 화면 미리보기용 - anchor가 handle 모델의 어디로 매칭되는지(평가와 같은 로직).
    // 매칭에 실패하면 nullopt이고, error가 주어지면 사유를 채운다.
    static std::optional<geometry::Vec3> ResolveAnchorPosition(
        const geometry::IGeometryAdapter& adapter, geometry::ModelHandle handle, const Anchor& anchor,
        std::string* error = nullptr);
};

} // namespace rule
