#include "geometry/NxJtGeometryAdapter.h"

#include <stdexcept>

#ifdef NX_CROSSCOMPARE_HAS_NX_BACKEND
#include "Core/Logging/ConsoleLogger.h"
#include "NxBackend/NxAssemblyReader.h"
#include "NxBackend/NxConnector.h"
#include "NxBackend/NxGeometryReader.h"

#include <algorithm>
#endif

namespace geometry {

namespace {
constexpr const char* kNotImplementedHint =
    " 미구현 — 회사PC에서 JT Open Toolkit/NX Open 연동 필요 (개발계획_v2.md §9, NxJtGeometryAdapter.h 참고)";
}

#ifdef NX_CROSSCOMPARE_HAS_NX_BACKEND

namespace {

// ComponentInfo 트리를 재귀적으로 순회하며 각 named component의 GeometryInfo(요약,
// BoundingBox3D만 - 실제 메쉬 아님)를 모아 전체 어셈블리의 BoundingBox를 계산한다.
// 1차 검증(북마크 열기) 목표는 "실제 조립체 크기가 화면에 정확히 뜨는가"이지,
// 실제 솔리드 렌더링은 아니라서 요약 데이터로 충분하다 - 그건 §B(Solid/Section View)
// 작업에서 FacetFace 기반으로 확장 예정.
void AccumulateBounds(CadImport::NxBackend::NxGeometryReader& reader,
                      const CadImport::Core::ComponentInfo& node,
                      BoundingBox& outBounds, bool& anyBody) {
    const auto result = reader.ReadGeometry(node.name);
    if (result.success) {
        for (const auto& body : result.value.bodies) {
            const auto& bb = body.boundingBox;
            if (!bb.valid) {
                continue;
            }
            if (!anyBody) {
                outBounds.min = {bb.min.x, bb.min.y, bb.min.z};
                outBounds.max = {bb.max.x, bb.max.y, bb.max.z};
                anyBody = true;
            } else {
                outBounds.min.x = std::min(outBounds.min.x, bb.min.x);
                outBounds.min.y = std::min(outBounds.min.y, bb.min.y);
                outBounds.min.z = std::min(outBounds.min.z, bb.min.z);
                outBounds.max.x = std::max(outBounds.max.x, bb.max.x);
                outBounds.max.y = std::max(outBounds.max.y, bb.max.y);
                outBounds.max.z = std::max(outBounds.max.z, bb.max.z);
            }
        }
    }
    for (const auto& child : node.children) {
        AccumulateBounds(reader, child, outBounds, anyBody);
    }
}

} // namespace

#endif // NX_CROSSCOMPARE_HAS_NX_BACKEND

NxJtGeometryAdapter::NxJtGeometryAdapter() {
#ifdef NX_CROSSCOMPARE_HAS_NX_BACKEND
    logger_ = std::make_unique<CadImport::Core::ConsoleLogger>();
    connector_ = std::make_unique<CadImport::NxBackend::NxConnector>(logger_.get());
#endif
}

NxJtGeometryAdapter::~NxJtGeometryAdapter() = default;

ModelHandle NxJtGeometryAdapter::LoadModel(const std::string& filePath) {
#ifdef NX_CROSSCOMPARE_HAS_NX_BACKEND
    const auto connectResult = connector_->Connect(filePath);
    if (!connectResult.success) {
        throw std::runtime_error(
            "NxJtGeometryAdapter::LoadModel - 북마크 열기 실패: " + connectResult.errorMessage);
    }

    CadImport::NxBackend::NxAssemblyReader assemblyReader(connector_.get(), logger_.get());
    const auto treeResult = assemblyReader.ReadTree();
    if (!treeResult.success) {
        throw std::runtime_error(
            "NxJtGeometryAdapter::LoadModel - Assembly Tree 읽기 실패: " + treeResult.errorMessage);
    }

    CadImport::NxBackend::NxGeometryReader geometryReader(connector_.get(), logger_.get());
    BoundingBox bounds;
    bool anyBody = false;
    AccumulateBounds(geometryReader, treeResult.value, bounds, anyBody);
    if (!anyBody) {
        throw std::runtime_error(
            "NxJtGeometryAdapter::LoadModel - 어셈블리에서 Body를 하나도 못 찾음 "
            "(빈 북마크이거나, BodiesOfComponent 관련 TODO(office-PC verify) 재확인 필요)");
    }

    const ModelHandle handle = nextHandle_++;
    models_[handle] = LoadedModel{filePath, bounds};
    return handle;
#else
    (void)filePath;
    throw std::runtime_error(std::string("NxJtGeometryAdapter::LoadModel") + kNotImplementedHint);
#endif
}

BoundingBox NxJtGeometryAdapter::GetBoundingBox(ModelHandle handle) const {
    auto it = models_.find(handle);
    if (it == models_.end()) {
        throw std::runtime_error("NxJtGeometryAdapter::GetBoundingBox - 잘못된 handle");
    }
    return it->second.bounds;
}

std::vector<Vec3> NxJtGeometryAdapter::GetVertices(ModelHandle handle) const {
    const auto box = GetBoundingBox(handle);
    // 아직 실제 페싯/메쉬가 없어 전체 어셈블리 BoundingBox의 8개 꼭짓점만 반환한다 -
    // Mock과 동일한 와이어프레임 렌더링 경로(ModelViewport::kEdges, 8개 꼭짓점 전제)를
    // 그대로 재사용한다. 실제 면 데이터는 §B(Solid/Section View) 작업에서 FacetFace
    // 기반으로 교체 예정.
    return {
        {box.min.x, box.min.y, box.min.z}, {box.max.x, box.min.y, box.min.z},
        {box.max.x, box.max.y, box.min.z}, {box.min.x, box.max.y, box.min.z},
        {box.min.x, box.min.y, box.max.z}, {box.max.x, box.min.y, box.max.z},
        {box.max.x, box.max.y, box.max.z}, {box.min.x, box.max.y, box.max.z},
    };
}

std::vector<AnchorCandidate> NxJtGeometryAdapter::FindAnchorCandidates(
    ModelHandle handle, const std::string& anchorType, const std::string& partName) const {
    (void)handle;
    (void)anchorType;
    (void)partName;
    // 1차 검증 범위 밖 - 북마크 Import(연결/트리/BoundingBox) 확인이 우선이고,
    // anchor 후보 탐색(2단계, NxJtGeometryAdapter.h 상단 주석 참고)은 다음 작업.
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
    throw std::runtime_error(std::string("NxJtGeometryAdapter::FindFaceCandidates") + kNotImplementedHint);
}

} // namespace geometry
