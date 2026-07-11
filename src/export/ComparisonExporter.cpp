#include "export/ComparisonExporter.h"

#include <fstream>
#include <stdexcept>

namespace report {

void ExportComparisonCsv(const std::string& filePath, const std::vector<RuleReport>& reports) {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file for CSV export: " + filePath);
    }

    // 초안 수준: 필드에 콤마가 없다는 전제로 단순 CSV 작성 (규칙 이름에 콤마 없음 확인됨).
    file << "rule,inch,value_mm,delta_from_baseline_mm,within_tolerance\n";
    for (const auto& r : reports) {
        for (const auto& result : r.results) {
            file << r.ruleName << ","
                 << result.inch << ","
                 << result.value << ","
                 << result.deltaFromBaseline << ","
                 << (result.withinTolerance ? "TRUE" : "FALSE") << "\n";
        }
    }
}

} // namespace report
