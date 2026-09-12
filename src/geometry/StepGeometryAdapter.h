#pragma once

#include "geometry/IGeometryAdapter.h"

#include <memory>

namespace geometry {

// STEP(.stp/.step) 파일을 OpenCASCADE(OCCT)로 직접 읽어 형상을 취득하는 어댑터.
// 개발계획_v2.md §17 참고 - NX Open API 세션 연결(§18, 보류)의 대안으로 채택된 경로.
// NX 세션/라이선스가 전혀 필요 없어 개인PC에서도 동작한다.
//
// - LoadModel: STEPControl_Reader로 .stp -> TopoDS_Shape.
// - GetVertices/GetBoundingBox: Bnd_Box 기반 요약 정보.
// - GetRenderTriangles: BRepMesh_IncrementalMesh로 뷰어 표시용 테셀레이션
//   (deflection을 bbox 대각선 기준으로 타이트하게 설정 - NX 경량화 수준 목표, §17).
// - FindAnchorCandidates/FindPlaneCandidates: 면 순회 + 표면 타입 분류(원통/평면)로
//   anchor_type을 지오메트리 속성 매칭으로 치환 (NX Open 질의 대신).
// - FindFaceCandidates: 대표점+법선만 추출 (기존 Measure::ComputeFaceToFaceGap의
//   center+normal 투영 근사를 그대로 사용). OCCT BRepExtrema_DistShapeShape 기반
//   exact 최단거리 계산은 TODO(§17) - FaceCandidate 확장이 먼저 필요.
//
// OCCT 타입(TopoDS_Shape 등)은 pImpl로 감춰서, 이 헤더를 include하는 쪽(ui 등)이
// OCCT include 경로/헤더를 몰라도 되게 한다 - StepGeometryAdapter.cpp만 OCCT에 의존.
class StepGeometryAdapter : public IGeometryAdapter {
public:
    StepGeometryAdapter();
    ~StepGeometryAdapter() override;

    ModelHandle LoadModel(const std::string& filePath) override;
    BoundingBox GetBoundingBox(ModelHandle handle) const override;
    std::vector<Vec3> GetVertices(ModelHandle handle) const override;
    std::vector<Vec3> GetRenderTriangles(ModelHandle handle) const override;
    // § 불필요한 선 정리(2026-09-11) - 삼각형 대각선이 아니라 실제 B-rep 엣지만 반환한다
    // (IGeometryAdapter.h 주석 참고). GCPnts_QuasiUniformDeflection으로 각 엣지를
    // 면 테셀레이션과 같은 deflection으로 따로 샘플링한다.
    std::vector<Vec3> GetRenderEdges(ModelHandle handle) const override;

    std::vector<AnchorCandidate> FindAnchorCandidates(
        ModelHandle handle, const std::string& anchorType, const std::string& partName) const override;
    std::vector<PlaneCandidate> FindPlaneCandidates(
        ModelHandle handle, const std::string& planeType, const std::string& partName) const override;
    std::vector<FaceCandidate> FindFaceCandidates(
        ModelHandle handle, const std::string& faceType, const std::string& partName) const override;

    // § 3D 클릭 피킹 - 광선과 모델의 모든 면을 정확히 교차시켜(OCCT
    // IntCurvesFace_Intersector) 가장 가까운 히트를 찾고, 그 면을 원통/평면으로 판별한다.
    // FindAnchorCandidates/FindPlaneCandidates와 같은 판별 기준(GeomAbs_Cylinder/Plane)을
    // 재사용하므로, 여기서 나온 결과를 anchor_type/지름으로 그대로 옮겨써도 일관된다.
    PickResult PickFace(ModelHandle handle, const Vec3& rayOrigin, const Vec3& rayDir) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace geometry
