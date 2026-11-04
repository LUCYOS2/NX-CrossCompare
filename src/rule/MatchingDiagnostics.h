#pragma once

#include "geometry/IGeometryAdapter.h"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace rule {

// § 매칭 진단(2026-10-30 진단 도구 → 2026-11-04 앱 메뉴 "검증 → 매칭 진단"으로 공용화) - 기준 모델의
// 모든 구멍/꼭짓점/모서리 중간점/원 중심/평면을 "그 자리를 클릭했다"고 가정하고, 앱에서 포인트를
// 지정할 때와 똑같이 확정(RuleEngine::Finalize*)한 뒤 규칙 엔진으로 다시 찾게 해서 매칭 정확도를 센다.
//   - 기준 모델 자기 매칭: 정답 / 실패(값을 안 냄) / 오답(다른 형상 - 가장 위험, 0이어야 함)
//   - 다른 모델: 정답 세트가 없어서 "값을 냄 / 실패"와 실패 사유 분포만 센다
// 사람이 몇 개 클릭해 보는 것으로는 알 수 없는 "실패/오답 비율"을 숫자로 보고 Matching.h의 임계값을
// 조정하기 위한 것이다. 앱(MatchingDiagnosticsDialog)과 명령줄 도구(diag_measure_matching)가 같이 쓴다.

struct DiagnosticTally {
    int correct = 0;  // 자기 매칭: 정답 / 다른 모델: 값을 냄
    int failed = 0;
    int wrong = 0;    // 자기 매칭에서만 의미 있음
    std::map<std::string, int> reasons; // 숫자를 뗀 실패 사유 -> 개수
};

struct DiagnosticSection {
    std::string label;          // "구멍", "꼭짓점", ...
    std::size_t candidateCount = 0; // 기준 모델에서 찾은 전체 후보 수(검사는 kMaxDiagnosticSamples개까지)
    std::size_t sampled = 0;
    DiagnosticTally self;
    std::vector<DiagnosticTally> others; // RunMatchingDiagnostics의 others 순서
};

struct MatchingDiagnosticsReport {
    std::vector<DiagnosticSection> sections;
    double seconds = 0.0;
    bool cancelled = false;
};

// 큰 모델에서 진단이 너무 오래 걸리지 않게 종류별 검사 개수 상한.
constexpr std::size_t kMaxDiagnosticSamples = 400;

// progress(done, total)이 false를 돌려주면 그 자리에서 멈추고 cancelled=true로 돌려준다.
MatchingDiagnosticsReport RunMatchingDiagnostics(
    const geometry::IGeometryAdapter& adapter, geometry::ModelHandle source,
    const std::vector<geometry::ModelHandle>& others,
    const std::function<bool(std::size_t done, std::size_t total)>& progress = {});

// "포인트 A(Hole): 후보가 애매함 (0.00mm / 0.10mm)" -> "후보가 애매함" 처럼 대상/숫자를 뗀 사유.
std::string DiagnosticReasonKey(const std::string& error);

} // namespace rule
