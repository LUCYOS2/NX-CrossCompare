#pragma once

#include "geometry/IGeometryAdapter.h"

#include <unordered_map>

namespace geometry {

// 회사PC 전용 실제 어댑터의 "착지점" 스켈레톤. 개인PC에는 JT Open Toolkit/NX Open
// SDK가 없어 실제 로직을 구현할 수 없으므로, 인터페이스 껍데기만 만들어두고
// 메서드 본문은 TODO로 남긴다. 회사PC에서 이 두 파일만 채우면 Rule Engine/UI/DB는
// 전혀 손대지 않아도 그대로 재사용된다 — 개발계획_v2.md §9의 핵심 목표.
//
// 회사PC 작업 순서(권장):
//  1) third_party에 JT Open Toolkit SDK 헤더/라이브러리 추가,
//     CMakeLists.txt에 include/link 경로 연결 (find_package 또는 직접 경로 지정)
//  2) LoadModel: JtkOpen() 등으로 실제 .jt 파일 열기
//  3) GetBoundingBox/GetVertices: JT 테셀레이션 데이터에서 추출
//  4) FindAnchorCandidates/FindPlaneCandidates: NX Open API로 Hole/Boss/Face 등
//     형상 검색 (rule_catalog.md의 anchor_type 문자열 -> 실제 NX 질의 매핑 테이블 필요)
//  5) 실제 .jt 파일 1개로 LoadModel ~ FindAnchorCandidates 왕복 테스트
//  6) src/ui/MainWindow.cpp 생성자의 MockGeometryAdapter를 이 클래스로 교체
class NxJtGeometryAdapter : public IGeometryAdapter {
public:
    NxJtGeometryAdapter();
    ~NxJtGeometryAdapter() override;

    ModelHandle LoadModel(const std::string& filePath) override;
    BoundingBox GetBoundingBox(ModelHandle handle) const override;
    std::vector<Vec3> GetVertices(ModelHandle handle) const override;

    std::vector<AnchorCandidate> FindAnchorCandidates(
        ModelHandle handle, const std::string& anchorType, const std::string& partName) const override;
    std::vector<PlaneCandidate> FindPlaneCandidates(
        ModelHandle handle, const std::string& planeType, const std::string& partName) const override;
    std::vector<FaceCandidate> FindFaceCandidates(
        ModelHandle handle, const std::string& faceType, const std::string& partName) const override;

private:
    struct LoadedModel {
        std::string filePath;
        // TODO(회사PC): JT 세션/문서 핸들, 테셀레이션 캐시 등을 여기에 보관
    };

    std::unordered_map<ModelHandle, LoadedModel> models_;
    ModelHandle nextHandle_ = 0;
};

} // namespace geometry
