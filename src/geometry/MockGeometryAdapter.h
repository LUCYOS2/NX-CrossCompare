#pragma once

#include "geometry/IGeometryAdapter.h"

#include <unordered_map>

namespace geometry {

// 개인PC 개발/테스트용 어댑터. 실제 JT/NX 파일 없이, 파일명에서 인치 숫자를 추출해
// 인치 비율로 폭/높이는 스케일되고 두께는 고정된 더미 박스 형상을 반환한다.
// (실물 TV 섀시가 인치별로 폭/높이는 커지지만 두께는 거의 고정인 패턴을 모사)
class MockGeometryAdapter : public IGeometryAdapter {
public:
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
    struct MockModel {
        BoundingBox bounds;
        std::vector<Vec3> vertices;
        std::vector<AnchorCandidate> anchorCandidates;
        std::vector<PlaneCandidate> planeCandidates;
        std::vector<FaceCandidate> faceCandidates;
    };

    std::unordered_map<ModelHandle, MockModel> models_;
    ModelHandle nextHandle_ = 0;
};

} // namespace geometry
