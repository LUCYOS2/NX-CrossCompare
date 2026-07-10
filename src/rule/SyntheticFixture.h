#pragma once

#include <vector>

#include "rule/Rule.h"

namespace rule {

// 개발계획_v2.md §8: 이미지 소싱 없이 좌표를 직접 정의하는 합성 Ground Truth 생성 규칙.
// Boss-Screw 정렬 규칙(anchor_A: Hole, anchor_B: Boss_Center) 검증용 데이터.
//
// - anchor_A/B의 절대 위치(X, Y)는 인치 비율로 스케일 (같은 섀시 위에서 함께 이동)
// - anchor_B는 anchor_A 대비 고정된 미세 오프셋만 유지 — 설계 의도상 두 부품은
//   인치와 무관하게 항상 거의 정렬되어야 하므로, 둘 사이 거리는 인치가 달라져도
//   거의 동일한 값(공차 이내)이 나오는 것이 정답이다.
// - Z(두께 방향 위치)는 인치 무관 고정.
struct SyntheticFixtureConfig {
    double baseInch = 55.0;
    double baseX = 400.0;
    double baseY = 200.0;
    double baseZ = 22.5;
    double fixedOffsetX = 0.05; // anchor_B.x - anchor_A.x, 인치 무관 고정
    double fixedOffsetY = 0.02; // anchor_B.y - anchor_A.y, 인치 무관 고정
};

std::vector<PointSample> GenerateBossScrewFixture(
    const std::vector<int>& inches, const SyntheticFixtureConfig& config = {});

} // namespace rule
