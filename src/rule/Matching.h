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

// § 주변 지문 검증(2026-11-04) - 위치로 고른 후보 주변 형상이 기준 인치와 같은지 확인한다.
constexpr double kLocalPatchRadiusMm = 10.0;        // 주변 지문을 뜨는 반경(절대값 - 국부 형상은 인치와 무관한 크기)
constexpr double kLocalPatchMinSimilarity = 0.75;   // 이 미만이면 "주변 형상이 다름"으로 실패

// § 국부 기준(2026-11-04, 측정면 원칙 3) - 두 번째 형상(기준 평면, 포인트 B)이 첫 번째 형상에서
// 기준 인치 대각선의 이 비율 안에 있으면, 외곽(bbox) 대신 다른 인치에서 매칭된 첫 형상 위치에서의
// 상대 거리로 예측한다. 높이/단차의 국부 기준면은 형상 바로 옆에 있어서, 인치가 커져도 첫 형상과의
// 거리는 그대로인데 외곽에서의 거리는 달라진다.
constexpr double kLocalReferenceMaxRatio = 0.10;

// § 인치 간 값 일관성(2026-11-04) - 매칭이 "유일"해도 엉뚱한 형상을 잡았을 수 있어서, 값이 튀면
// 경고한다(실패시키지는 않음 - 실제 설계 차이일 수도 있으므로 사람이 확인).
constexpr double kSuspectToleranceMultiple = 5.0;   // 공차 폭의 이 배수를 넘으면 의심
constexpr double kSuspectMinDeviationMm = 0.5;      // 공차가 아주 작아도 최소 이만큼은 넘어야 의심
constexpr double kSuspectTrendRatio = 0.05;         // 추세 예측값의 5%까지는 정상 범위로 봄

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

// 기준 인치에서 first → second 상대 위치가 국부 기준으로 볼 만큼 가까운지(kLocalReferenceMaxRatio).
bool IsLocalReference(const geometry::Vec3& first, const geometry::Vec3& second, const geometry::BoundingBox& sourceBox);

// 인치 간 값 일관성 - points는 (인치, 측정값). 각 점을 나머지 점들의 직선 추세(인치에 대한 1차 회귀)로
// 예측해서, 허용 범위(allowedMm와 예측값의 kSuspectTrendRatio 중 큰 값) 대비 가장 많이 벗어난 점
// 하나만 돌려준다(그 점도 범위 안이면 nullopt). 점이 kMinTrendPoints개 미만이면 판단하지 않는다 -
// 3개일 때는 튀는 점 하나가 나머지 두 점의 추세까지 기울여서, 정상인 점이 더 튀어 보일 수 있다.
constexpr std::size_t kMinTrendPoints = 4;
struct TrendOutlier {
    std::size_t index = 0;
    double predicted = 0.0;
};
std::optional<TrendOutlier> FindTrendOutlier(const std::vector<std::pair<int, double>>& points, double allowedMm);

} // namespace rule
