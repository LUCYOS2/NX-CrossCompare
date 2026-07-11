#pragma once

#include "rule/RuleEngine.h"

#include <string>
#include <vector>

namespace report {

struct RuleReport {
    std::string ruleName;
    std::vector<rule::InchResult> results;
};

// 규칙별 평가 결과를 CSV로 저장한다. Excel 서식(색상, 셀 병합 등)은 Python
// (src/python/export_excel.py)이 이 CSV를 읽어 담당한다 — 개발계획_v2.md §2의
// "Python: Excel/리포트" 역할 분리를 따른 것. C++은 계산만, 서식은 Python.
void ExportComparisonCsv(const std::string& filePath, const std::vector<RuleReport>& reports);

} // namespace report
