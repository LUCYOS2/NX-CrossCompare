#pragma once

#include "geometry/IGeometryAdapter.h"

#include <optional>
#include <utility>
#include <vector>

namespace rule {

// 개발계획_v2.md §7 Selector enum 중 현재 카탈로그(rule_catalog.md)가 실제로 쓰는
// 3종만 구현한다. nearest_to_point는 아직 쓰는 규칙이 없어 구현하지 않음(YAGNI) —
// 필요한 규칙이 생기면 그때 추가한다.

std::optional<std::pair<geometry::AnchorCandidate, geometry::AnchorCandidate>> SelectNearestPair(
    const std::vector<geometry::AnchorCandidate>& candidatesA,
    const std::vector<geometry::AnchorCandidate>& candidatesB);

std::optional<geometry::AnchorCandidate> SelectLeftmost(
    const std::vector<geometry::AnchorCandidate>& candidates);

std::optional<geometry::AnchorCandidate> SelectRightmost(
    const std::vector<geometry::AnchorCandidate>& candidates);

} // namespace rule
