#pragma once

#include "geometry/IGeometryAdapter.h"
#include "rule/Rule.h"

#include <string>
#include <vector>

namespace rule {

// § 프리셋 자동 치수(2026-11-05) - "후크 같은 형상 프리셋을 추가하면 전장 사이즈처럼 측정할 포인트와
// 치수선을 자동으로 만들어 달라"는 요청(1단계). 프리셋으로 찾은 형상 하나(패치 범위 + 그 형상의 평면들)와
// 모델 외곽에서 잴 만한 치수 후보를 뽑는다. 측정면 원칙에 따라 "버니어가 실제로 닿는 면"만 쓴다:
//   - 형상 크기: 축 방향으로 바깥을 향해 반대쪽에 있는 평면 두 개 중, 축 방향에서 봐서 겹치는(버니어 두 날이
//     동시에 물리는) 가장 먼 쌍 사이. 그런 평면 쌍이 없으면(끝이 곡면 등) 후보를 만들지 않는다.
//   - 외곽에서의 위치: 제품 외곽 끝면(전장 사이즈와 같은 끝면)에서 형상의 가까운 끝면까지.
// 후보는 전부 축에 수직인 평행 평면 두 개라서 face_to_face_gap 규칙(클릭한 두 평면과 같은 형식)으로
// 저장되고, 다른 인치에는 기존 평면 매칭(외곽 기준 예측 + 국부 기준 + 주변 지문)이 그대로 적용된다.
// 후크 높이/걸림량처럼 면의 역할(걸림면 등)을 알아야 하는 치수는 다음 단계.

enum class PresetDimensionKind {
    ShapeSize,   // 형상에서 바깥을 향해 마주 보는 두 평면 사이
    FromOutline, // 제품 외곽 끝면 -> 형상의 가까운 끝면
};

struct PresetDimensionCandidate {
    PresetDimensionKind kind = PresetDimensionKind::ShapeSize;
    int axis = 0;               // 0=X, 1=Y, 2=Z
    bool outlineAtMax = false;  // FromOutline: +쪽 외곽 끝면 기준이면 true
    geometry::PlaneCandidate faceA; // ShapeSize: -쪽 끝면 / FromOutline: 외곽 끝면
    geometry::PlaneCandidate faceB; // ShapeSize: +쪽 끝면 / FromOutline: 형상에서 외곽 쪽 끝면
    geometry::Vec3 refA;        // faceA 위에서 형상에 가장 가까운 점 - 규칙의 기준 좌표로 저장된다
    geometry::Vec3 refB;
    double valueMm = 0.0;
};

// 면 법선이 축과 이 각도 안이어야 "축에 수직인 끝면"으로 본다 - face_to_face_gap 평가가 두 면이
// 1도 안으로 평행해야 값을 내므로(kParallelFaceToleranceDeg) 그보다 좁게 잡는다.
constexpr double kPresetFaceAxisToleranceDeg = 0.5;
// 이보다 짧은 치수는 후보로 내지 않는다(형상이 외곽에 붙어 있는 경우 등).
constexpr double kPresetMinDimensionMm = 0.5;

std::string AxisName(int axis);

// 형상 하나(shape.hasBox && planes)에 대한 후보 - 순서: 형상 크기 X/Y/Z, 외곽에서의 위치 X/Y/Z.
std::vector<PresetDimensionCandidate> ComputePresetDimensionCandidates(
    const geometry::PatchCandidate& shape, const geometry::BoundingBox& modelBox,
    const std::vector<geometry::PlaneCandidate>& modelPlanes);

// 화면에 보여줄 기본 이름("<프리셋> 크기 X")과 설명.
std::string DefaultPresetDimensionName(const std::string& presetName, const PresetDimensionCandidate& candidate);
std::string DescribePresetDimension(const PresetDimensionCandidate& candidate);

// 후보 -> 저장할 규칙(face_to_face_gap, 평면 A/B의 기준 좌표 = refA/refB, sourceBox = 기준 인치 모델 범위).
// 주변 지문은 비어 있다 - 저장 전에 RuleEngine::FinalizePlaneRef로 기준 인치에서 채운다.
Rule MakePresetDimensionRule(const PresetDimensionCandidate& candidate, const std::string& name,
                             const geometry::BoundingBox& sourceBox);

} // namespace rule
