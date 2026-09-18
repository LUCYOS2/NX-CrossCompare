#pragma once

#include "geometry/IGeometryAdapter.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <array>
#include <vector>

namespace geometry {

// § 형상 프리셋(2026-09-18) - Hook/Flange처럼 원통/평면 단일 판정으로는 못 잡는 복합
// 형상을 "생김새"로 식별하기 위한 국소 형상 패치 추출 + 지문(descriptor) 계산 + 유사도
// 검색. 배경: anchor_type이 Hole/Boss_Center(둘 다 단일 원통면) 뿐이라 Hook류는 자동
// 검색이 전혀 안 됐고, 부품 이름도 전부 UNITE되어 없어(사용자 확인, 2026-09-18)
// 이름 기반 식별도 불가능 - 남은 유일한 단서는 형상 자체의 기하학적 생김새뿐이다.
//
// 이 헤더는 TopoDS_Face/TopoDS_Shape(OCCT 타입)를 그대로 쓴다 - StepGeometryAdapter.cpp
// 내부에서만 include하는 전제(공개 인터페이스인 IGeometryAdapter.h는 이 헤더를 모른다).
// FeaturePatchDescriptor 자체는 OCCT 타입이 없어서 공개 인터페이스(IGeometryAdapter.h)에
// 정의돼 있다 - 여기서는 그걸 그대로 재사용한다.
//
// 지문은 회전/이동에 무관하고 스케일에는 관대(비율 기반)하게 설계했다 - 인치가 달라져도
// 절대 치수는 조금씩 다르지만 "형상 비율"은 유지된다는 전제. 완전 자동 확정이 아니라
// 유사도 점수로 후보를 추려 사람이 확인하거나(등록 시), RuleEngine의 자동 Selector가
// 최고점을 고르는(적용 시) 반자동 방식이다 - 100% 정확도는 기술적으로 보장 못 한다.

// 패치 확장 반경 상한 = 전체 모델 bbox 대각선의 이 비율 - 후크류 크기가 전체 어셈블리
// 대비 작다는 전제(§17 NX 경량화 목표 규모와 동일 전제). CaptureFeaturePatch(단일 패치
// 추출)와 FindPatchCandidates(전체 탐색) 양쪽에서 같은 값을 써야 지문이 서로 비교
// 가능하다.
constexpr double kPatchRadiusRatio = 0.08;

// 패치 하나(면 목록) + 대표 위치(면적 가중 중심) + 원본 지문과의 유사도(0~1, 1이 완전 일치).
struct FeaturePatchCandidate {
    std::vector<TopoDS_Face> faces;
    Vec3 position;
    double similarity = 0.0;
};

// 패치를 구성하는 면들의 면적 가중 중심(월드 좌표).
Vec3 PatchCentroid(const std::vector<TopoDS_Face>& patch);

// seedFace에서 시작해 인접 면으로 확장한다. 공유 edge 양쪽 면의 대표 법선 사이 각이
// kPatchContinueAngleDeg 미만이면 "같은 형상의 연속"으로 보고 계속 확장하고, 그 이상이면
// (뚜렷한 꺾임 = 다른 형상과의 경계일 가능성) 그 edge에서 멈춘다. 추가로 seedFace 중심에서
// maxRadius(보통 전체 bbox 대각선의 일정 배수)를 넘는 면은 애매한 곡면에서 무한정 번지는
// 것을 막기 위해 강제로 제외한다.
std::vector<TopoDS_Face> ExtractFeaturePatch(
    const TopoDS_Shape& wholeShape, const TopoDS_Face& seedFace, double maxRadius);

FeaturePatchDescriptor ComputePatchDescriptor(const std::vector<TopoDS_Face>& patch);

// 두 지문의 유사도(0~1, 1이 완전 일치) - 면 타입 히스토그램/bbox 비율/경계 비율/대표
// 반지름 비율의 가중 차이를 종합한다.
double PatchSimilarity(const FeaturePatchDescriptor& a, const FeaturePatchDescriptor& b);

// wholeShape 전체에서 descriptor와 비슷한 패치를 찾는다. excludeFaces에 있는 면은 시드
// 후보에서 제외한다(같은 모델 내 "나머지 찾기"에서 원본 패치를 다시 찾지 않도록). 지문의
// surfaceTypeCounts에서 가장 드문 면 타입을 우선 시드로 삼아 브루트포스 확장을 줄인다.
// similarityThreshold 이상인 것만 반환하며, 유사도 내림차순으로 정렬한다.
std::vector<FeaturePatchCandidate> FindPatchCandidates(
    const TopoDS_Shape& wholeShape, const FeaturePatchDescriptor& descriptor,
    const std::vector<TopoDS_Face>& excludeFaces, double similarityThreshold);

} // namespace geometry
