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

// 카탈로그 001(Boss-Screw)/002(후크 높이)/003(후크 걸림량) 검증용 후보 생성.
// §8과 동일한 패턴: 절대 위치(X/Y)는 인치 비율로 스케일, 정답 pair의 오프셋과
// 두께 방향(Z) 위치는 인치 무관 고정값. 오답 후보는 정답과 충분히 멀리 배치해
// nearest_pair/leftmost가 인치에 관계없이 항상 정답을 고르는지 검증한다.
std::vector<AnchorCandidate> GenerateAnchorCandidates(double scale, double halfD) {
    std::vector<AnchorCandidate> candidates;

    // 001. Boss-Screw: Hole(Bezel) 3개 후보, 그중 H0에 대응하는 Boss_Center 1개만
    // 고정 오프셋(0.05, 0.02)으로 근접 배치, 나머지는 스케일된 먼 거리 오답.
    const double h0x = -150.0 * scale, h0y = 100.0 * scale;
    candidates.push_back({"Hole", "Bezel", {h0x, h0y, halfD}});
    candidates.push_back({"Hole", "Bezel", {0.0 * scale, 100.0 * scale, halfD}});
    candidates.push_back({"Hole", "Bezel", {180.0 * scale, 100.0 * scale, halfD}});

    candidates.push_back({"Boss_Center", "Rear_Chassis", {h0x + 0.05, h0y + 0.02, halfD}});
    candidates.push_back({"Boss_Center", "Rear_Chassis", {250.0 * scale, -50.0 * scale, halfD}});
    candidates.push_back({"Boss_Center", "Rear_Chassis", {-300.0 * scale, 80.0 * scale, halfD}});

    // 002. 후크 높이: Hook_Tip_Edge(Side_Frame) 3개 후보, leftmost(가장 작은 x)가 정답.
    // Z는 인치 무관 고정(-halfD + 8.5) — 두께가 이미 고정이므로 높이도 자동으로 고정된다.
    const double hookHeightZ = -halfD + 8.5;
    candidates.push_back({"Hook_Tip_Edge", "Side_Frame", {-200.0 * scale, 50.0 * scale, hookHeightZ}});
    candidates.push_back({"Hook_Tip_Edge", "Side_Frame", {100.0 * scale, 50.0 * scale, hookHeightZ}});
    candidates.push_back({"Hook_Tip_Edge", "Side_Frame", {300.0 * scale, 50.0 * scale, hookHeightZ}});

    // 003. 후크 걸림량: selector 없음(단일 후보) 전제. Z만 인치 무관 고정.
    candidates.push_back({"Hook_Catch_Edge", "Side_Frame", {0.0, 0.0, -halfD + 5.0}});
    candidates.push_back({"Wall_Inner_Edge", "Front_Bezel", {0.0, 0.0, -halfD + 5.2}});

    return candidates;
}

std::vector<PlaneCandidate> GeneratePlaneCandidates(double halfD) {
    return {
        {"Datum_Plane", "Rear_Chassis", {0.0, 0.0, -halfD}, {0.0, 0.0, 1.0}},
    };
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
    model.anchorCandidates = GenerateAnchorCandidates(scale, halfD);
    model.planeCandidates = GeneratePlaneCandidates(halfD);

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

std::vector<AnchorCandidate> MockGeometryAdapter::FindAnchorCandidates(
    ModelHandle handle, const std::string& anchorType, const std::string& partName) const {
    auto it = models_.find(handle);
    if (it == models_.end()) {
        return {};
    }
    std::vector<AnchorCandidate> result;
    for (const auto& candidate : it->second.anchorCandidates) {
        if (candidate.anchorType == anchorType && candidate.partName == partName) {
            result.push_back(candidate);
        }
    }
    return result;
}

std::vector<PlaneCandidate> MockGeometryAdapter::FindPlaneCandidates(
    ModelHandle handle, const std::string& planeType, const std::string& partName) const {
    auto it = models_.find(handle);
    if (it == models_.end()) {
        return {};
    }
    std::vector<PlaneCandidate> result;
    for (const auto& candidate : it->second.planeCandidates) {
        if (candidate.planeType == planeType && candidate.partName == partName) {
            result.push_back(candidate);
        }
    }
    return result;
}

} // namespace geometry
