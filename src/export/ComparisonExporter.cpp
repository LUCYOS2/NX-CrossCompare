#include "export/ComparisonExporter.h"

#include <fstream>
#include <stdexcept>

namespace report {

namespace {

// 이미지 경로/CTQ 코드처럼 사용자가 자유 입력하는 문자열 필드는 콤마/따옴표를 포함할 수
// 있으므로 항상 따옴표로 감싸고 내부 따옴표는 두 번 써서 이스케이프한다(표준 CSV 규칙).
// rule/inch/value 등 우리가 직접 만든 숫자/식별자 필드는 콤마가 없다고 보장되므로 그대로 둔다.
std::string CsvQuote(const std::string& field) {
    std::string escaped;
    escaped.reserve(field.size() + 2);
    escaped += '"';
    for (char c : field) {
        if (c == '"') {
            escaped += '"';
        }
        escaped += c;
    }
    escaped += '"';
    return escaped;
}

} // namespace

void ExportComparisonCsv(const std::string& filePath, const std::vector<RuleReport>& reports,
                          const std::map<int, std::string>& modelNameByInch) {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file for CSV export: " + filePath);
    }

    // § 세로형(long) 컬럼 재구성 - "규칙/모델/인치/포인트(부위)/관리항목 종류/도면 스펙/
    // 공차/측정방법"으로 정리해달라는 요청에 맞춰 헤더를 다시 짰다. "도면 스펙"은 목표
    // (nominal)값이 아니라 실측값(value_mm) 그대로(사용자 확인 완료) - spec_mm이라는
    // 이름은 Python 쪽 export_excel.py가 "도면 스펙" 헤더로 보여주기 위한 것뿐이다.
    // within_tolerance는 8개 컬럼에는 없지만 Python이 셀 색상(공차 통과/실패)을 입히는 데
    // 계속 필요해서 마지막에 덧붙였다.
    file << "rule,model,inch,ctq_code,check_point_category,spec_mm,tolerance_plus_mm,"
            "tolerance_minus_mm,measurement_type,image_path,within_tolerance\n";
    for (const auto& r : reports) {
        const std::string ctqCode = r.ctqCode.value_or("");
        const std::string category = r.checkPointCategory.value_or("");
        const std::string imagePath = r.imagePath.value_or("");
        for (const auto& result : r.results) {
            const auto modelIt = modelNameByInch.find(result.inch);
            const std::string modelName = modelIt != modelNameByInch.end() ? modelIt->second : "";
            file << CsvQuote(r.ruleName) << ","
                 << CsvQuote(modelName) << ","
                 << result.inch << ","
                 << CsvQuote(ctqCode) << ","
                 << CsvQuote(category) << ","
                 << result.value << ","
                 << r.tolerancePlusMm << ","
                 << r.toleranceMinusMm << ","
                 << CsvQuote(r.measurementType) << ","
                 << CsvQuote(imagePath) << ","
                 << (result.withinTolerance ? "TRUE" : "FALSE") << "\n";
        }
    }
}

} // namespace report
