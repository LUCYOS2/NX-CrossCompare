#include "geometry/NxJtGeometryAdapter.h"

#include <stdexcept>

namespace geometry {

namespace {
constexpr const char* kNotImplementedHint =
    " 미구현 — 회사PC에서 JT Open Toolkit/NX Open 연동 필요 (개발계획_v2.md §9, NxJtGeometryAdapter.h 참고)";
}

NxJtGeometryAdapter::NxJtGeometryAdapter() {
    // TODO(회사PC): JT Open Toolkit 세션 초기화 (예: JtkOpen)
}

NxJtGeometryAdapter::~NxJtGeometryAdapter() {
    // TODO(회사PC): 세션 정리 (예: JtkClose)
}

ModelHandle NxJtGeometryAdapter::LoadModel(const std::string& filePath) {
    (void)filePath;
    // TODO(회사PC): 실제 .jt 파일 열기, 테셀레이션/exact B-rep 로드
    throw std::runtime_error(std::string("NxJtGeometryAdapter::LoadModel") + kNotImplementedHint);
}

BoundingBox NxJtGeometryAdapter::GetBoundingBox(ModelHandle handle) const {
    (void)handle;
    throw std::runtime_error(std::string("NxJtGeometryAdapter::GetBoundingBox") + kNotImplementedHint);
}

std::vector<Vec3> NxJtGeometryAdapter::GetVertices(ModelHandle handle) const {
    (void)handle;
    throw std::runtime_error(std::string("NxJtGeometryAdapter::GetVertices") + kNotImplementedHint);
}

std::vector<AnchorCandidate> NxJtGeometryAdapter::FindAnchorCandidates(
    ModelHandle handle, const std::string& anchorType, const std::string& partName) const {
    (void)handle;
    (void)anchorType;
    (void)partName;
    throw std::runtime_error(std::string("NxJtGeometryAdapter::FindAnchorCandidates") + kNotImplementedHint);
}

std::vector<PlaneCandidate> NxJtGeometryAdapter::FindPlaneCandidates(
    ModelHandle handle, const std::string& planeType, const std::string& partName) const {
    (void)handle;
    (void)planeType;
    (void)partName;
    throw std::runtime_error(std::string("NxJtGeometryAdapter::FindPlaneCandidates") + kNotImplementedHint);
}

std::vector<FaceCandidate> NxJtGeometryAdapter::FindFaceCandidates(
    ModelHandle handle, const std::string& faceType, const std::string& partName) const {
    (void)handle;
    (void)faceType;
    (void)partName;
    // TODO(회사PC): face_to_face_gap은 실제 면 테셀레이션 기반 최단거리 탐색이 필요.
    // Mock의 중심점+법선 근사와 달리 실제 구현은 삼각형 메시 간 최단거리 알고리즘을 써야 함.
    throw std::runtime_error(std::string("NxJtGeometryAdapter::FindFaceCandidates") + kNotImplementedHint);
}

} // namespace geometry
