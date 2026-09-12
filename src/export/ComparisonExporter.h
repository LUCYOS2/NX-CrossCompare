#pragma once

#include "rule/RuleEngine.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace report {

// § 규칙 메타데이터 동봉(2026-09-12) - "치수 비교 테이블을 규칙/모델/인치/포인트(부위)/
// 관리항목 종류/도면 스펙/공차/측정방법 컬럼으로 정리하려고 해"라는 요청. 기존에는
// ruleName + 측정값만 담아서 엑셀에서 어떤 부위를 찍었는지, 공차가 얼마인지, 측정방법이
// 뭔지 전혀 알 수 없었다. rule::Rule 전체를 그대로 담기보다 CSV/엑셀에 실제로 필요한
// 필드만 뽑아뒀다(Rule 쪽 필드가 늘어도 이 구조체를 매번 바꾸지 않도록).
// "도면 스펙"은 사용자 확인 결과 별도 목표(nominal)값이 아니라 실측값(value_mm)을 그대로
// 표시하는 것으로 확정됨 - InchResult::value를 그대로 쓴다(results에 이미 있음).
struct RuleReport {
    std::string ruleName;
    std::vector<rule::InchResult> results;
    std::optional<std::string> imagePath;          // Rule::imagePath - 측정 부위 캡쳐 이미지
    std::optional<std::string> ctqCode;             // Rule::ctqCode - Point(부위) 이니셜
    std::optional<std::string> checkPointCategory;  // Rule::checkPointCategory - 종류(Dimension 등)
    std::string measurementType;                    // rule::ToString(Rule::measurementType)
    double tolerancePlusMm = 0.0;
    double toleranceMinusMm = 0.0;
};

// 규칙별 평가 결과를 CSV로 저장한다. Excel 서식(색상, 셀 병합, 이미지 삽입)은 Python
// (src/python/export_excel.py)이 이 CSV를 읽어 담당한다 — 개발계획_v2.md §2의
// "Python: Excel/리포트" 역할 분리를 따른 것. C++은 계산만, 서식은 Python.
//
// modelNameByInch: 화면 매트릭스(규칙×인치)와 달리 세로형(long) export는 "모델" 컬럼이
// 필요하다(사용자 확인: 화면은 매트릭스 유지, export만 세로형) - 인치별로 실제 로드된
// STEP 파일명을 보여준다(MainWindow::inchFiles_ 참고). 없는 인치는 빈 문자열로 남는다.
void ExportComparisonCsv(const std::string& filePath, const std::vector<RuleReport>& reports,
                          const std::map<int, std::string>& modelNameByInch = {});

} // namespace report
