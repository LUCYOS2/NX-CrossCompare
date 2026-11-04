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
    // § 인치 간 값 일관성(2026-11-04) - 값은 냈지만 도면 스펙과 차이가 지나치게 크거나 다른 인치들의
    // 추세에서 튀면 사유가 담긴다(비어 있으면 이상 없음). 실패가 아니라 "사람이 확인할 것" 표시다.
    std::string warning;
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

    // § 주변 지문 검증(2026-11-04) - 클릭으로 포인트를 지정한 직후(기준 인치 모델 handle로) 부른다.
    // 평가와 같은 방법으로 기준 모델에서 매칭해서, ref.position을 실제로 잡힌 형상 위치(구멍이면 원통
    // 구간 중앙)로 맞추고 그 자리의 주변 지문을 ref.localPatch에 저장한다. 기준 모델에서도 못 찾으면
    // 아무것도 바꾸지 않는다.
    static void FinalizeAnchorRef(const geometry::IGeometryAdapter& adapter, geometry::ModelHandle handle,
                                  Anchor& anchor);
    // 평면 클릭(기준 평면, 면 간격의 면) - 위치는 클릭 점 그대로 두고 주변 지문만 저장한다.
    static void FinalizePlaneRef(const geometry::IGeometryAdapter& adapter, geometry::ModelHandle handle,
                                 RefGeometry& ref);

    // § 인치 간 값 일관성(2026-11-04) - Evaluate가 마지막에 부른다(테스트용으로 공개). 도면 스펙과
    // 차이가 공차 폭의 kSuspectToleranceMultiple배(최소 kSuspectMinDeviationMm)를 넘거나, 도면 스펙이
    // 없는 인치가 다른 인치들의 직선 추세에서 튀면 InchResult::warning을 채운다.
    static void AddConsistencyWarnings(const Rule& rule, std::vector<InchResult>& results);
};

} // namespace rule
