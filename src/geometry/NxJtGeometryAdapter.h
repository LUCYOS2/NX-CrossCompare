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
// 1단계는 이미 만들어져 있다: external/NxCadCore(git submodule, TV_BitSam과 공유)의
// CadImportModule이 정확히 "북마크 open -> Assembly Tree/요약 Geometry 읽기"를
// 담당한다 (NxConnector::Connect(bookmarkPath), NxAssemblyReader, NxGeometryReader).
// 회사PC에서는 이걸 새로 만들지 말고 그대로 갖다 쓸 것 — 자세한 경계는
// external/NxCadCore/README.md, CadImportModule/README.md 참고.
//
// 회사PC 작업 순서(권장):
//  1) external/NxCadCore가 최신인지 확인(git submodule update --init --remote),
//     CadImportModule/NxBackend가 필요로 하는 JT Open Toolkit/NX Open SDK 경로를
//     Shared/NxOpenSdk.props 관례대로 맞춰서 빌드 가능하게 함
//  2) LoadModel: NxCadCore의 NxConnector::Connect(bookmarkPath)로 북마크 열기 +
//     NxAssemblyReader로 구조 읽기 (1단계 - 새로 구현할 필요 없음, 연결만)
//  3) GetBoundingBox/GetVertices: NxGeometryReader의 요약 Geometry(BoundingBox 등)
//     활용, 필요하면 NxCadCore에 기능 추가 요청/기여
//  4) FindAnchorCandidates/FindPlaneCandidates/FindFaceCandidates: anchor_type ->
//     NX Open API 질의 매핑 테이블 구현 (2단계 - 여기가 NX CrossCompare만의 새
//     로직. NxCadCore의 NxConnector가 구현하는 Shared/NxContracts::INxSessionAccessor
//     를 통해 세션에 접근하고, TV_BitSam의 RoiModule::NxRoiResolver와 같은 패턴으로
//     NxConnector 구체 클래스에는 의존하지 않는다. rule_catalog.md의 anchor_type
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
