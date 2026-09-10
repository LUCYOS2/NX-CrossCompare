#include "geometry/StepGeometryAdapter.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IntCurvesFace_Intersector.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace geometry {

namespace {

// NX 경량화 표시 수준을 목표로 한 테셀레이션 정밀도(개발계획_v2.md §17).
// 바운딩박스 대각선에 비례한 값을 써서 작은 부품/큰 어셈블리 모두에서 일관된
// 시각적 해상도를 낸다. 절대값 deflection을 그냥 쓰면 큰 모델에서 너무 성겨진다.
constexpr double kLinearDeflectionRatio = 0.0008;  // 대각선의 0.08%
constexpr double kAngularDeflectionRad = 0.15;      // 라디안, 곡면 분할 각도

BoundingBox ComputeBoundingBox(const TopoDS_Shape& shape) {
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) {
        return BoundingBox{};
    }
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    return BoundingBox{Vec3{xmin, ymin, zmin}, Vec3{xmax, ymax, zmax}};
}

double BoundingDiagonal(const BoundingBox& box) {
    const double dx = box.max.x - box.min.x;
    const double dy = box.max.y - box.min.y;
    const double dz = box.max.z - box.min.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

struct StepGeometryAdapter::Impl {
    struct LoadedModel {
        std::string filePath;
        TopoDS_Shape shape;
        BoundingBox bounds;
    };

    std::unordered_map<ModelHandle, LoadedModel> models;
    ModelHandle nextHandle = 0;

    const LoadedModel* Find(ModelHandle handle) const {
        auto it = models.find(handle);
        return it == models.end() ? nullptr : &it->second;
    }
};

StepGeometryAdapter::StepGeometryAdapter() : impl_(std::make_unique<Impl>()) {}
StepGeometryAdapter::~StepGeometryAdapter() = default;

ModelHandle StepGeometryAdapter::LoadModel(const std::string& filePath) {
    STEPControl_Reader reader;
    const IFSelect_ReturnStatus status = reader.ReadFile(filePath.c_str());
    if (status != IFSelect_RetDone) {
        throw std::runtime_error("StepGeometryAdapter::LoadModel - STEP 파일을 읽을 수 없습니다: " + filePath);
    }

    reader.TransferRoots();
    const TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull()) {
        throw std::runtime_error("StepGeometryAdapter::LoadModel - 형상이 비어 있습니다: " + filePath);
    }

    Impl::LoadedModel model;
    model.filePath = filePath;
    model.shape = shape;
    model.bounds = ComputeBoundingBox(shape);

    // 뷰어 표시용 테셀레이션을 로드 시점에 미리 계산해 캐시해둔다 (매 프레임 재계산 방지).
    const double diagonal = BoundingDiagonal(model.bounds);
    const double linearDeflection = std::max(diagonal * kLinearDeflectionRatio, 1e-3);
    BRepMesh_IncrementalMesh(model.shape, linearDeflection, /*isRelative=*/false, kAngularDeflectionRad);

    const ModelHandle handle = impl_->nextHandle++;
    impl_->models[handle] = std::move(model);
    return handle;
}

BoundingBox StepGeometryAdapter::GetBoundingBox(ModelHandle handle) const {
    const auto* model = impl_->Find(handle);
    return model ? model->bounds : BoundingBox{};
}

std::vector<Vec3> StepGeometryAdapter::GetVertices(ModelHandle handle) const {
    // AlignmentCheck(§5 정렬 QC)는 바운딩박스만 보므로, 기존 8코너 규약을 유지한다.
    const auto* model = impl_->Find(handle);
    if (!model) {
        return {};
    }
    const Vec3& lo = model->bounds.min;
    const Vec3& hi = model->bounds.max;
    return {
        {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
        {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
    };
}

std::vector<Vec3> StepGeometryAdapter::GetRenderTriangles(ModelHandle handle) const {
    const auto* model = impl_->Find(handle);
    if (!model) {
        return {};
    }

    std::vector<Vec3> triangles;
    for (TopExp_Explorer faceExp(model->shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
        const TopoDS_Face& face = TopoDS::Face(faceExp.Current());
        TopLoc_Location location;
        const Handle(Poly_Triangulation)& tri = BRep_Tool::Triangulation(face, location);
        if (tri.IsNull()) {
            continue;
        }
        const gp_Trsf& trsf = location.Transformation();
        const bool reversed = face.Orientation() == TopAbs_REVERSED;

        for (int i = 1; i <= tri->NbTriangles(); ++i) {
            int i1, i2, i3;
            tri->Triangle(i).Get(i1, i2, i3);
            if (reversed) {
                std::swap(i1, i3);
            }
            const gp_Pnt p1 = tri->Node(i1).Transformed(trsf);
            const gp_Pnt p2 = tri->Node(i2).Transformed(trsf);
            const gp_Pnt p3 = tri->Node(i3).Transformed(trsf);
            triangles.push_back({p1.X(), p1.Y(), p1.Z()});
            triangles.push_back({p2.X(), p2.Y(), p2.Z()});
            triangles.push_back({p3.X(), p3.Y(), p3.Z()});
        }
    }
    return triangles;
}

std::vector<AnchorCandidate> StepGeometryAdapter::FindAnchorCandidates(
    ModelHandle handle, const std::string& anchorType, const std::string& partName) const {
    const auto* model = impl_->Find(handle);
    if (!model) {
        return {};
    }

    // anchor_type -> 지오메트리 질의 매핑 (NX Open API 질의를 대체, §17).
    // 현재는 "Hole"/"Boss_Center" 계열 = 원통면 검색만 구현. rule_catalog.md의
    // 나머지 anchor_type(Hook_Tip_Edge 등)은 실제 STEP 표본으로 튜닝하며 추가한다.
    std::vector<AnchorCandidate> candidates;
    if (anchorType != "Hole" && anchorType != "Boss_Center") {
        return candidates;
    }

    for (TopExp_Explorer faceExp(model->shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
        const TopoDS_Face& face = TopoDS::Face(faceExp.Current());
        BRepAdaptor_Surface surface(face, /*restrictTriangulation=*/false);
        if (surface.GetType() != GeomAbs_Cylinder) {
            continue;
        }
        const gp_Cylinder cylinder = surface.Cylinder();
        const gp_Ax1 axis = cylinder.Axis();
        const gp_Pnt center = axis.Location();
        const gp_Dir dir = axis.Direction();
        AnchorCandidate candidate{
            anchorType, partName, Vec3{center.X(), center.Y(), center.Z()}, cylinder.Radius() * 2.0};
        candidate.axis = Vec3{dir.X(), dir.Y(), dir.Z()};
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

std::vector<PlaneCandidate> StepGeometryAdapter::FindPlaneCandidates(
    ModelHandle handle, const std::string& planeType, const std::string& partName) const {
    const auto* model = impl_->Find(handle);
    if (!model) {
        return {};
    }

    std::vector<PlaneCandidate> candidates;
    for (TopExp_Explorer faceExp(model->shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
        const TopoDS_Face& face = TopoDS::Face(faceExp.Current());
        BRepAdaptor_Surface surface(face, /*restrictTriangulation=*/false);
        if (surface.GetType() != GeomAbs_Plane) {
            continue;
        }
        const gp_Pln plane = surface.Plane();
        const gp_Pnt point = plane.Location();
        const gp_Dir normal = plane.Axis().Direction();
        candidates.push_back(PlaneCandidate{
            planeType, partName,
            Vec3{point.X(), point.Y(), point.Z()},
            Vec3{normal.X(), normal.Y(), normal.Z()}});
    }
    return candidates;
}

PickResult StepGeometryAdapter::PickFace(
    ModelHandle handle, const Vec3& rayOrigin, const Vec3& rayDir) const {
    const auto* model = impl_->Find(handle);
    if (!model) {
        return PickResult{};
    }

    const gp_Lin line(gp_Pnt(rayOrigin.x, rayOrigin.y, rayOrigin.z),
                       gp_Dir(rayDir.x, rayDir.y, rayDir.z));

    // 모델의 모든 면과 광선을 정확히 교차시켜(근사 없음), 광선을 따라 가장 먼저(=가장
    // 가까운) 맞는 면을 찾는다 - 여러 면이 겹쳐 보여도 화면에서 실제로 보이는 면이
    // 뽑히도록 하기 위함(뒤에 가려진 면이 아니라).
    constexpr double kIntersectionTolerance = 1e-4;
    double bestParam = std::numeric_limits<double>::max();
    TopoDS_Face bestFace;
    bool found = false;

    for (TopExp_Explorer faceExp(model->shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
        const TopoDS_Face& face = TopoDS::Face(faceExp.Current());
        IntCurvesFace_Intersector intersector(face, kIntersectionTolerance);
        intersector.Perform(line, 0.0, std::numeric_limits<double>::max());
        if (!intersector.IsDone()) {
            continue;
        }
        for (int i = 1; i <= intersector.NbPnt(); ++i) {
            const double w = intersector.WParameter(i);
            if (w >= 0.0 && w < bestParam) {
                bestParam = w;
                bestFace = face;
                found = true;
            }
        }
    }

    if (!found) {
        return PickResult{};
    }

    // 판별 기준은 FindAnchorCandidates/FindPlaneCandidates와 동일(GeomAbs_Cylinder/Plane) -
    // 클릭으로 얻은 결과와 텍스트 검색으로 얻은 결과가 같은 형상이면 같은 값이 나오게.
    BRepAdaptor_Surface surface(bestFace, /*restrictTriangulation=*/false);
    PickResult result;
    if (surface.GetType() == GeomAbs_Cylinder) {
        const gp_Cylinder cylinder = surface.Cylinder();
        const gp_Pnt center = cylinder.Axis().Location();
        result.kind = PickedFaceKind::Cylinder;
        result.point = Vec3{center.X(), center.Y(), center.Z()};
        result.diameterMm = cylinder.Radius() * 2.0;
    } else if (surface.GetType() == GeomAbs_Plane) {
        const gp_Pln plane = surface.Plane();
        const gp_Pnt point = plane.Location();
        const gp_Dir normal = plane.Axis().Direction();
        result.kind = PickedFaceKind::Plane;
        result.point = Vec3{point.X(), point.Y(), point.Z()};
        result.normal = Vec3{normal.X(), normal.Y(), normal.Z()};
    }
    // 그 외 면 타입(원뿔/자유곡면 등)은 지금 단계에서 지원 안 함 - kind는 None으로 남는다.

    return result;
}

std::vector<FaceCandidate> StepGeometryAdapter::FindFaceCandidates(
    ModelHandle handle, const std::string& faceType, const std::string& partName) const {
    const auto* model = impl_->Find(handle);
    if (!model) {
        return {};
    }

    // 대표점(center)+법선만 뽑아둔다 - 실제 거리 계산은 기존 Measure::ComputeFaceToFaceGap
    // (center+normal 투영 근사, Phase4b 초안)이 그대로 쓰인다. OCCT의
    // BRepExtrema_DistShapeShape로 exact 최단거리를 계산하는 건 TODO(§17) - 이 함수가
    // TopoDS_Face 자체가 아니라 FaceCandidate(요약값)만 반환하는 현재 인터페이스로는
    // 바로 못 붙이고, IGeometryAdapter/FaceCandidate 확장이 먼저 필요하다.
    std::vector<FaceCandidate> candidates;
    for (TopExp_Explorer faceExp(model->shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
        const TopoDS_Face& face = TopoDS::Face(faceExp.Current());
        BRepAdaptor_Surface surface(face, /*restrictTriangulation=*/false);

        Vec3 center;
        Vec3 normal;
        if (surface.GetType() == GeomAbs_Plane) {
            const gp_Pln plane = surface.Plane();
            const gp_Pnt point = plane.Location();
            const gp_Dir dir = plane.Axis().Direction();
            center = {point.X(), point.Y(), point.Z()};
            normal = {dir.X(), dir.Y(), dir.Z()};
        } else if (surface.GetType() == GeomAbs_Cylinder) {
            const gp_Cylinder cylinder = surface.Cylinder();
            const gp_Pnt point = cylinder.Location();
            const gp_Dir dir = cylinder.Axis().Direction();
            center = {point.X(), point.Y(), point.Z()};
            normal = {dir.X(), dir.Y(), dir.Z()};
        } else {
            continue;
        }
        candidates.push_back(FaceCandidate{faceType, partName, center, normal});
    }
    return candidates;
}

} // namespace geometry
