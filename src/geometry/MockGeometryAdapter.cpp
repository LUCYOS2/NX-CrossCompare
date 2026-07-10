#include "geometry/MockGeometryAdapter.h"

#include <cctype>
#include <utility>

namespace geometry {

namespace {

// 기준 인치(55") 대비 스케일 비율 계산. 폭/높이는 인치 비율대로 스케일되고,
// 두께(Z)는 인치와 무관하게 고정 — §8 합성 데이터 규칙(개발계획_v2.md)과 동일한 패턴.
constexpr double kBaseInch = 55.0;
constexpr double kBaseWidth = 1230.0;  // mm, 55인치 기준 가로 폭
constexpr double kBaseHeight = 710.0;  // mm, 55인치 기준 세로 높이
constexpr double kFixedDepth = 45.0;   // mm, 인치 무관 고정 두께

int ExtractInchFromFilename(const std::string& filePath) {
    std::string digits;
    for (char c : filePath) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digits += c;
        } else if (!digits.empty()) {
            break;
        }
    }
    if (digits.empty()) {
        return static_cast<int>(kBaseInch);
    }
    return std::stoi(digits);
}

} // namespace

ModelHandle MockGeometryAdapter::LoadModel(const std::string& filePath) {
    const int inch = ExtractInchFromFilename(filePath);
    const double scale = static_cast<double>(inch) / kBaseInch;
    const double halfW = (kBaseWidth * scale) / 2.0;
    const double halfH = (kBaseHeight * scale) / 2.0;
    const double halfD = kFixedDepth / 2.0;

    MockModel model;
    model.bounds = BoundingBox{
        Vec3{-halfW, -halfH, -halfD},
        Vec3{halfW, halfH, halfD}
    };
    // 중심(원점) 기준 박스의 8개 꼭짓점 — §5 절대좌표 중심정렬 전제와 일치
    model.vertices = {
        {-halfW, -halfH, -halfD}, {halfW, -halfH, -halfD},
        {halfW, halfH, -halfD}, {-halfW, halfH, -halfD},
        {-halfW, -halfH, halfD}, {halfW, -halfH, halfD},
        {halfW, halfH, halfD}, {-halfW, halfH, halfD},
    };

    const ModelHandle handle = nextHandle_++;
    models_[handle] = std::move(model);
    return handle;
}

BoundingBox MockGeometryAdapter::GetBoundingBox(ModelHandle handle) const {
    auto it = models_.find(handle);
    if (it == models_.end()) {
        return BoundingBox{};
    }
    return it->second.bounds;
}

std::vector<Vec3> MockGeometryAdapter::GetVertices(ModelHandle handle) const {
    auto it = models_.find(handle);
    if (it == models_.end()) {
        return {};
    }
    return it->second.vertices;
}

} // namespace geometry
