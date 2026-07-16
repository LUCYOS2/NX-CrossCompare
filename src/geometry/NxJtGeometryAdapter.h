#pragma once

#include "geometry/IGeometryAdapter.h"

#include <unordered_map>

namespace geometry {

// 회사PC 전용 실제 어댑터의 "착지점" 스켈레톤. 개인PC에는 JT Open Toolkit/NX Open
// SDK가 없어 실제 로직을 구현할 수 없으므로, 인터페이스 껍데기만 만들어두고
// 메서드 본문은 TODO로 남긴다. 회사PC에서 이 두 파일만 채우면 Rule Engine/UI/DB는
// 전혀 손대지 않아도 그대로 재사용된다 — 개발계획_v2.md §9의 핵심 목표.
//
// 실제 데이터 접근은 2단계로 나뉜다 (다른 사내 프로젝트에서 NX 북마크+NX Open API로
// 도면을 가져온 경험 기반):
//  1단계 - 구조 진입: 북마크 파일은 실제 부품/어셈블리의 참조(경로 또는 GUID)만
//          가리키는 포인터다. NX Open API로 그 참조를 열면 NX 구조에는 들어가지만,
//          아직 홀/보스/후크 같은 구체적 형상을 얻은 건 아니다. -> LoadModel 담당
//  2단계 - 규칙 기반 형상 탐색: 열린 구조 안에서 anchor_type 문자열(Hole,
//          Boss_Center, Hook_Tip_Edge, Face 등)을 실제 NX Open API 질의로 변환해야
//          형상 후보를 얻는다 (예: "Hole" -> 원통형 면 중 지름이 anchor_param과
//          일치하는 것 검색). -> FindAnchorCandidates/FindPlaneCandidates/
//          FindFaceCandidates 담당. 이 anchor_type -> NX 질의 매핑 테이블 설계가
//          회사PC 작업에서 가장 어렵고 중요한 부분이다.
//
// 회사PC 작업 순서(권장):
//  1) third_party에 JT Open Toolkit/NX Open SDK 헤더·라이브러리 추가,
//     CMakeLists.txt에 include/link 경로 연결 (find_package 또는 직접 경로 지정)
//  2) LoadModel: 북마크 파일 파싱 -> 참조(경로/GUID) 추출 -> NX Open API로 실제
//     파트/어셈블리 열기 (1단계)
//  3) GetBoundingBox/GetVertices: 열린 구조의 테셀레이션 데이터에서 추출
//  4) FindAnchorCandidates/FindPlaneCandidates/FindFaceCandidates: anchor_type ->
//     NX Open API 질의 매핑 테이블 구현 (2단계, rule_catalog.md의 anchor_type
//     목록 기준으로 하나씩 채워나가면 됨)
//  5) 실제 북마크 1개로 LoadModel ~ FindAnchorCandidates 왕복 테스트
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
