#pragma once

#include "geometry/IGeometryAdapter.h"

#include <string>

namespace rule {

double ComputePointToPointDistance(const geometry::Vec3& a, const geometry::Vec3& b);

// axis는 "X" | "Y" | "Z" 중 하나.
double ComputeAxisProjectionDistance(const geometry::Vec3& a, const geometry::Vec3& b, const std::string& axis);

// normal은 단위벡터라고 가정하지 않고 내부에서 정규화한다.
double ComputePointToPlaneDistance(
    const geometry::Vec3& point, const geometry::Vec3& pointOnPlane, const geometry::Vec3& normal);

// Phase4b 초안: 실제 면 테셀레이션 기반 최단거리가 아니라, faceA 중심에서
// faceA 법선 방향으로 faceB 중심까지의 투영 거리로 근사한다 (FaceCandidate
// 주석 참고). 회사PC에서 실제 메시 데이터가 들어오면 진짜 최단거리 탐색으로
// 교체가 필요하다.
double ComputeFaceToFaceGap(const geometry::FaceCandidate& faceA, const geometry::FaceCandidate& faceB);

} // namespace rule
