#pragma once

#include "geometry/IGeometryAdapter.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rule {

// § 인치 간 자동 매칭(2026-10-30) - 클릭 좌표(RefGeometry)를 다른 인치 모델로 옮겨서, 그
// 근처에서 "확실히 하나뿐인" 후보만 채택한다. 애매하면 값을 내지 않고 실패로 돌려준다 -
// 틀린 값을 조용히 내놓는 게 가장 위험하다는 판단(사용자와 합의한 설계, 메모리
// cross-inch-matching-design 참고). 인치별 모델은 축 방향이 모두 같다는 전제(사용자 확인) -
// 원점만 달라도 되고, 회전 보정은 하지 않는다.
//
// 아래 상수는 초기값이다 - 실제 인치별 샘플로 정답률/오답률을 재서 튜닝해야 한다.
constexpr double kMatchMinRadiusMm = 5.0;           // 예측 위치에서 후보를 찾는 최소 반경
constexpr double kMatchRadiusDiagonalRatio = 0.01;  // 큰 모델은 대각선의 1%까지 허용
constexpr double kMatchMinSeparationMm = 0.2;       // 1등과 2등 후보 거리의 최소 차이
constexpr double kSameFeatureToleranceMm = 0.05;    // 이 안의 후보는 같은 형상으로 취급
constexpr double kNormalMatchToleranceDeg = 5.0;    // 평면 법선(부호 포함) 일치 허용 각도
constexpr double kParallelFaceToleranceDeg = 1.0;   // face_to_face_gap에서 "평행한 두 면"으로 보는 각도

// 축마다 bbox의 {min, center, max} 중 클릭 좌표에 가장 가까운 기준을 골라, 그 기준에서의
// 거리를 유지한 채 target bbox로 옮긴다. 모서리 근처 형상(후크, 코너 보스)은 가까운 외곽에서의
// 거리가, 중앙 형상(VESA 구멍)은 중심에서의 거리가 인치와 무관하게 거의 그대로라는 관찰에
// 근거한다(크기에 비례해서 움직이지 않음). source와 target이 같은 모델이면 그대로 돌아온다.
geometry::Vec3 PredictPosition(
    const geometry::Vec3& refPosition, const geometry::BoundingBox& sourceBox, const geometry::BoundingBox& targetBox);

double MatchRadiusMm(const geometry::BoundingBox& targetBox);

// 점에서 축정렬 박스까지의 거리(박스 안이면 0).
double DistanceToBox(const geometry::Vec3& point, const geometry::BoundingBox& box);

struct MatchResult {
    std::optional<std::size_t> index; // 채택한 후보 - 없으면 error에 사유
    std::string error;
    double distanceMm = 0.0;           // 예측 위치에서 채택 후보까지 거리
};

// distances[i]는 예측 위치에서 후보 i까지의 거리. sameFeature(i, j)가 true인 후보끼리는 하나로
// 취급한다(반원통 2개, 같은 평면 위에 나뉜 면 등 - 어느 쪽을 골라도 측정값이 같음). 채택 조건:
// 1등이 반경 안에 있고, 1등과 다른 형상인 2등이 max(2×1등, 1등+kMatchMinSeparationMm)보다 멀 것.
MatchResult PickUniqueNearest(const std::vector<double>& distances,
                              const std::function<bool(std::size_t, std::size_t)>& sameFeature, double radiusMm);

} // namespace rule
