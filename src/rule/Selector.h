#pragma once

#include "geometry/IGeometryAdapter.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rule {

// 개발계획_v2.md §7 Selector enum 중 현재 카탈로그(rule_catalog.md)가 실제로 쓰는
// 5종만 구현한다. nearest_to_point는 아직 쓰는 규칙이 없어 구현하지 않음(YAGNI) —
// 필요한 규칙이 생기면 그때 추가한다.

// anchor_type이 원통면(Hole/Boss_Center)이면 종류를 안 가리고 다 잡히는 문제(형상
// 인식은 되지만 진짜 체결 구멍인지 필렛/모서리인지 구분 못 함)를 완화하는 필터.
// paramKey=="diameter"이고 paramValue가 있을 때만 지름 ±kDiameterMatchToleranceMm
// 범위 밖의 후보를 제거한다. RuleEngine과 포인트 검색(AnchorSearchDialog)이 공유한다.
constexpr double kDiameterMatchToleranceMm = 0.3;

std::vector<geometry::AnchorCandidate> FilterByDiameter(
    std::vector<geometry::AnchorCandidate> candidates,
    const std::optional<std::string>& paramKey, const std::optional<double>& paramValue);

std::optional<std::pair<geometry::AnchorCandidate, geometry::AnchorCandidate>> SelectNearestPair(
    const std::vector<geometry::AnchorCandidate>& candidatesA,
    const std::vector<geometry::AnchorCandidate>& candidatesB);

std::optional<geometry::AnchorCandidate> SelectLeftmost(
    const std::vector<geometry::AnchorCandidate>& candidates);

std::optional<geometry::AnchorCandidate> SelectRightmost(
    const std::vector<geometry::AnchorCandidate>& candidates);

// 가장 가까운 면 쌍 (거리만 기준, 법선 방향은 보지 않음).
std::optional<std::pair<geometry::FaceCandidate, geometry::FaceCandidate>> SelectNearestFacePair(
    const std::vector<geometry::FaceCandidate>& candidatesA,
    const std::vector<geometry::FaceCandidate>& candidatesB);

// 법선이 서로 반대방향(내적 < -0.9)인 면 쌍 중 가장 가까운 것. 살두께처럼
// "마주보는 두 벽" 성격의 측정에 쓴다.
std::optional<std::pair<geometry::FaceCandidate, geometry::FaceCandidate>> SelectParallelFacePair(
    const std::vector<geometry::FaceCandidate>& candidatesA,
    const std::vector<geometry::FaceCandidate>& candidatesB);

} // namespace rule
