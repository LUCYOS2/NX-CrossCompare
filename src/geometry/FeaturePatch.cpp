#include "geometry/FeaturePatch.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <GProp_GProps.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GeomLProp_SLProps.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopTools_MapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Cone.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Torus.hxx>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace geometry {

namespace {

// § 패치 확장 경계 판정(2026-09-18) - 공유 edge 양쪽 면의 법선 사이 각이 이 미만이면
// "매끄럽게 이어지는 같은 형상"으로 보고 계속 확장한다(예: 후크 내부의 완만한 굽힘,
// 필렛). 이 이상이면 뚜렷한 꺾임(다른 형상/모재와의 경계일 가능성)으로 보고 멈춘다.
// 초기값은 대략치 - 실제 STEP 표본(Hook/Flange)으로 튜닝 필요(FeaturePatch.h 주석 참고).
constexpr double kPatchContinueAngleDeg = 100.0;

gp_Pnt FaceCentroid(const TopoDS_Face& face) {
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.CentreOfMass();
}

// 면의 파라미터 중앙에서의 법선(면의 Orientation 반영, 바깥쪽 방향).
bool FaceNormalAtCenter(const TopoDS_Face& face, gp_Dir& outNormal) {
    BRepAdaptor_Surface surf(face, Standard_True);
    const double uMid = (surf.FirstUParameter() + surf.LastUParameter()) * 0.5;
    const double vMid = (surf.FirstVParameter() + surf.LastVParameter()) * 0.5;
    GeomLProp_SLProps props(surf.Surface().Surface(), uMid, vMid, 1, 1e-6);
    if (!props.IsNormalDefined()) {
        return false;
    }
    gp_Dir normal = props.Normal();
    if (face.Orientation() == TopAbs_REVERSED) {
        normal.Reverse();
    }
    outNormal = normal;
    return true;
}

double TotalBoundaryLength(const std::vector<TopoDS_Face>& patch,
                            const TopTools_MapOfShape& patchFaceSet) {
    // 패치 안의 각 면에서, 그 edge를 공유하는 상대 면이 patchFaceSet 밖에 있으면(또는
    // 자유 edge면) "경계"로 센다.
    double total = 0.0;
    TopTools_MapOfShape countedEdges;
    for (const auto& face : patch) {
        for (TopExp_Explorer edgeExp(face, TopAbs_EDGE); edgeExp.More(); edgeExp.Next()) {
            const TopoDS_Edge& edge = TopoDS::Edge(edgeExp.Current());
            if (countedEdges.Contains(edge)) {
                continue;
            }
            GProp_GProps lprops;
            BRepGProp::LinearProperties(edge, lprops);
            total += lprops.Mass();
            countedEdges.Add(edge);
        }
    }
    return total;
}

// 원통/원뿔/토러스 면이면 대표 반지름을, 아니면 -1을 반환.
double FaceRadiusIfCurved(const TopoDS_Face& face, GeomAbs_SurfaceType type) {
    BRepAdaptor_Surface surf(face, Standard_False);
    switch (type) {
        case GeomAbs_Cylinder:
            return surf.Cylinder().Radius();
        case GeomAbs_Cone:
            return surf.Cone().RefRadius();
        case GeomAbs_Torus:
            return surf.Torus().MajorRadius();
        default:
            return -1.0;
    }
}

} // namespace

Vec3 PatchCentroid(const std::vector<TopoDS_Face>& patch) {
    GProp_GProps props;
    for (const auto& face : patch) {
        GProp_GProps faceProps;
        BRepGProp::SurfaceProperties(face, faceProps);
        props.Add(faceProps);
    }
    const gp_Pnt center = props.CentreOfMass();
    return Vec3{center.X(), center.Y(), center.Z()};
}

std::vector<TopoDS_Face> ExtractFeaturePatch(
    const TopoDS_Shape& wholeShape, const TopoDS_Face& seedFace, double maxRadius) {
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaceMap;
    TopExp::MapShapesAndAncestors(wholeShape, TopAbs_EDGE, TopAbs_FACE, edgeFaceMap);

    const gp_Pnt seedCentroid = FaceCentroid(seedFace);
    const double maxRadiusSq = maxRadius * maxRadius;

    std::vector<TopoDS_Face> patch;
    TopTools_MapOfShape visited;
    std::deque<TopoDS_Face> queue;
    queue.push_back(seedFace);
    visited.Add(seedFace);

    while (!queue.empty()) {
        const TopoDS_Face current = queue.front();
        queue.pop_front();
        patch.push_back(current);

        gp_Dir currentNormal;
        const bool hasCurrentNormal = FaceNormalAtCenter(current, currentNormal);

        for (TopExp_Explorer edgeExp(current, TopAbs_EDGE); edgeExp.More(); edgeExp.Next()) {
            const TopoDS_Edge& edge = TopoDS::Edge(edgeExp.Current());
            if (!edgeFaceMap.Contains(edge)) {
                continue;
            }
            const TopTools_ListOfShape& neighborFaces = edgeFaceMap.FindFromKey(edge);
            for (TopTools_ListOfShape::Iterator it(neighborFaces); it.More(); it.Next()) {
                const TopoDS_Face neighbor = TopoDS::Face(it.Value());
                if (visited.Contains(neighbor)) {
                    continue;
                }
                // 반경 상한 - 애매한 곡면(예: 이면각 판정이 불안정한 얇은 필렛)에서
                // 무한정 번지는 것을 막는 안전장치.
                const gp_Pnt neighborCentroid = FaceCentroid(neighbor);
                if (seedCentroid.SquareDistance(neighborCentroid) > maxRadiusSq) {
                    continue;
                }
                // 이면각(대표 법선 사이 각) 판정 - 매끄럽게 이어지면 계속 확장.
                gp_Dir neighborNormal;
                bool continueGrowth = true;
                if (hasCurrentNormal && FaceNormalAtCenter(neighbor, neighborNormal)) {
                    const double angleDeg = currentNormal.Angle(neighborNormal) * 180.0 / M_PI;
                    continueGrowth = angleDeg < kPatchContinueAngleDeg;
                }
                if (!continueGrowth) {
                    continue;
                }
                visited.Add(neighbor);
                queue.push_back(neighbor);
            }
        }
    }
    return patch;
}

FeaturePatchDescriptor ComputePatchDescriptor(const std::vector<TopoDS_Face>& patch) {
    FeaturePatchDescriptor descriptor;
    descriptor.faceCount = static_cast<int>(patch.size());
    if (patch.empty()) {
        return descriptor;
    }

    Bnd_Box bbox;
    double maxRadius = 0.0;
    for (const auto& face : patch) {
        BRepBndLib::Add(face, bbox);
        BRepAdaptor_Surface surf(face, Standard_False);
        const GeomAbs_SurfaceType type = surf.GetType();
        const int typeIndex = std::clamp(static_cast<int>(type), 0,
                                          static_cast<int>(descriptor.surfaceTypeCounts.size()) - 1);
        descriptor.surfaceTypeCounts[static_cast<size_t>(typeIndex)]++;
        const double radius = FaceRadiusIfCurved(face, type);
        if (radius > maxRadius) {
            maxRadius = radius;
        }
    }

    if (bbox.IsVoid()) {
        return descriptor;
    }
    double xmin, ymin, zmin, xmax, ymax, zmax;
    bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double dx = xmax - xmin;
    const double dy = ymax - ymin;
    const double dz = zmax - zmin;
    const double maxDim = std::max({dx, dy, dz, 1e-9});
    descriptor.bboxRatioX = dx / maxDim;
    descriptor.bboxRatioY = dy / maxDim;
    descriptor.bboxRatioZ = dz / maxDim;

    const double diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (diagonal > 1e-9) {
        TopTools_MapOfShape patchFaceSet;
        for (const auto& face : patch) {
            patchFaceSet.Add(face);
        }
        descriptor.boundaryLengthRatio = TotalBoundaryLength(patch, patchFaceSet) / diagonal;
        descriptor.dominantRadiusRatio = maxRadius / diagonal;
    }
    return descriptor;
}

double PatchSimilarity(const FeaturePatchDescriptor& a, const FeaturePatchDescriptor& b) {
    // 면 타입 히스토그램 - 코사인 유사도(둘 다 비어있으면 0으로 취급해 아래에서 걸러짐).
    double dot = 0.0, normA = 0.0, normB = 0.0;
    for (size_t i = 0; i < a.surfaceTypeCounts.size(); ++i) {
        dot += static_cast<double>(a.surfaceTypeCounts[i]) * b.surfaceTypeCounts[i];
        normA += static_cast<double>(a.surfaceTypeCounts[i]) * a.surfaceTypeCounts[i];
        normB += static_cast<double>(b.surfaceTypeCounts[i]) * b.surfaceTypeCounts[i];
    }
    const double histogramSim =
        (normA <= 0.0 || normB <= 0.0) ? 0.0 : dot / (std::sqrt(normA) * std::sqrt(normB));

    // bbox 비율 - 절대오차 기반 유사도(0~1).
    const double bboxErr = (std::abs(a.bboxRatioX - b.bboxRatioX) + std::abs(a.bboxRatioY - b.bboxRatioY) +
                             std::abs(a.bboxRatioZ - b.bboxRatioZ)) /
                            3.0;
    const double bboxSim = std::clamp(1.0 - bboxErr, 0.0, 1.0);

    const double boundaryErr = std::abs(a.boundaryLengthRatio - b.boundaryLengthRatio);
    const double boundarySim = std::clamp(1.0 - boundaryErr, 0.0, 1.0);

    const double radiusErr = std::abs(a.dominantRadiusRatio - b.dominantRadiusRatio);
    const double radiusSim = std::clamp(1.0 - radiusErr, 0.0, 1.0);

    const double faceCountErr =
        std::abs(a.faceCount - b.faceCount) / static_cast<double>(std::max(a.faceCount, b.faceCount) + 1);
    const double faceCountSim = std::clamp(1.0 - faceCountErr, 0.0, 1.0);

    // 가중합 - 면 타입 구성이 가장 결정적인 특징이라 가장 크게 반영.
    return histogramSim * 0.40 + bboxSim * 0.20 + boundarySim * 0.15 + radiusSim * 0.15 + faceCountSim * 0.10;
}

std::vector<FeaturePatchCandidate> FindPatchCandidates(
    const TopoDS_Shape& wholeShape, const FeaturePatchDescriptor& descriptor,
    const std::vector<TopoDS_Face>& excludeFaces, double similarityThreshold) {
    // 전체 bbox 대각선 - 패치 확장 반경 상한과 겹치는 후보 억제(같은 지점에서 여러 번
    // 시드되는 것 방지)에 쓴다.
    Bnd_Box wholeBox;
    BRepBndLib::Add(wholeShape, wholeBox);
    double diagonal = 1.0;
    if (!wholeBox.IsVoid()) {
        double xmin, ymin, zmin, xmax, ymax, zmax;
        wholeBox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        const double dx = xmax - xmin, dy = ymax - ymin, dz = zmax - zmin;
        diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    // 패치 자체는 국소적이라, 확장 반경 상한은 원본 patch의 대략적 크기(대표 반지름 또는
    // bbox 비율)에서 역산하기보다 전체 대각선의 작은 비율로 넉넉히 잡는다(kPatchRadiusRatio -
    // CaptureFeaturePatch와 같은 값을 써야 지문이 서로 비교 가능하다).
    const double maxPatchRadius = diagonal * kPatchRadiusRatio;

    TopTools_MapOfShape excludeSet;
    for (const auto& face : excludeFaces) {
        excludeSet.Add(face);
    }

    // 지문에서 가장 드문(개수가 적은, 0보다 큰) 면 타입을 시드 우선순위로 삼는다 - 흔한
    // Plane부터 전부 시드하면 느리다. 원본 지문에 있는 면 타입이 하나도 없으면(빈 패치)
    // 그냥 전체를 다 본다.
    int seedTypeIndex = -1;
    int seedTypeCount = std::numeric_limits<int>::max();
    for (size_t i = 0; i < descriptor.surfaceTypeCounts.size(); ++i) {
        const int count = descriptor.surfaceTypeCounts[i];
        if (count > 0 && count < seedTypeCount) {
            seedTypeCount = count;
            seedTypeIndex = static_cast<int>(i);
        }
    }

    std::vector<FeaturePatchCandidate> candidates;
    TopTools_MapOfShape alreadySeeded;
    for (TopExp_Explorer faceExp(wholeShape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
        const TopoDS_Face& seed = TopoDS::Face(faceExp.Current());
        if (excludeSet.Contains(seed) || alreadySeeded.Contains(seed)) {
            continue;
        }
        if (seedTypeIndex >= 0) {
            BRepAdaptor_Surface surf(seed, Standard_False);
            const int typeIndex = std::clamp(static_cast<int>(surf.GetType()), 0,
                                              static_cast<int>(descriptor.surfaceTypeCounts.size()) - 1);
            if (typeIndex != seedTypeIndex) {
                continue;
            }
        }

        const std::vector<TopoDS_Face> patch = ExtractFeaturePatch(wholeShape, seed, maxPatchRadius);
        const FeaturePatchDescriptor candidateDescriptor = ComputePatchDescriptor(patch);
        const double similarity = PatchSimilarity(descriptor, candidateDescriptor);

        // 이 패치를 구성한 면들은 전부 "이미 평가됨"으로 표시해서 같은 패치를 여러 시드로
        // 중복 평가하지 않게 한다.
        for (const auto& face : patch) {
            alreadySeeded.Add(face);
        }

        if (similarity < similarityThreshold) {
            continue;
        }

        FeaturePatchCandidate candidate;
        candidate.faces = patch;
        candidate.position = PatchCentroid(patch);
        candidate.similarity = similarity;
        candidates.push_back(std::move(candidate));
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const FeaturePatchCandidate& lhs, const FeaturePatchCandidate& rhs) {
                  return lhs.similarity > rhs.similarity;
              });
    return candidates;
}

} // namespace geometry
