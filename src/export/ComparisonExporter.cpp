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
    // 공차/측정방법"으로 정리해달라는 요청에 맞춰 헤더를 다시 짰다.
    // § 도면 스펙 = nominal(2026-10-30, 사용자 확인) - 예전엔 규칙에 도면 치수가 없어서 spec_mm에
    // 실측값을 넣었지만, 이제 규칙의 인치별 도면 스펙(Rule::specByInch)을 넣고 실측값은 value_mm로
    // 따로 둔다. 도면 스펙이 없는 인치는 spec_mm이 비고, 측정 실패한 인치는 value_mm/deviation_mm이
    // 빈다. status: PASS/FAIL(도면 스펙 판정), COMPARE_PASS/COMPARE_FAIL(도면 스펙 없음 - 인치 간
    // 비교), ERROR(측정 실패, 사유는 error). within_tolerance는 예전 CSV를 읽는 쪽 호환용.
    file << "rule,model,inch,ctq_code,check_point_category,spec_mm,tolerance_plus_mm,"
            "tolerance_minus_mm,measurement_type,image_path,within_tolerance,"
            "value_mm,deviation_mm,status,error\n";
    for (const auto& r : reports) {
        const std::string ctqCode = r.ctqCode.value_or("");
        const std::string category = r.checkPointCategory.value_or("");
        const std::string imagePath = r.imagePath.value_or("");
        for (const auto& result : r.results) {
            const auto modelIt = modelNameByInch.find(result.inch);
            const std::string modelName = modelIt != modelNameByInch.end() ? modelIt->second : "";
            const char* status = !result.ok              ? "ERROR"
                                 : result.spec.has_value() ? (result.withinTolerance ? "PASS" : "FAIL")
                                                              : (result.withinTolerance ? "COMPARE_PASS" : "COMPARE_FAIL");
            file << CsvQuote(r.ruleName) << ","
                 << CsvQuote(modelName) << ","
                 << result.inch << ","
                 << CsvQuote(ctqCode) << ","
                 << CsvQuote(category) << ",";
            if (result.spec.has_value()) {
                file << *result.spec;
            }
            file << ","
                 << r.tolerancePlusMm << ","
                 << r.toleranceMinusMm << ","
                 << CsvQuote(r.measurementType) << ","
                 << CsvQuote(imagePath) << ","
                 << (result.withinTolerance ? "TRUE" : "FALSE") << ",";
            if (result.ok) {
                file << result.value;
            }
            file << ",";
            if (result.ok) {
                file << result.deviation;
            }
            file << "," << status << "," << CsvQuote(result.error) << "\n";
        }
    }
}

} // namespace report
