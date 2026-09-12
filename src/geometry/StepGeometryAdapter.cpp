#include "geometry/StepGeometryAdapter.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <Geom_Curve.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IntCurvesFace_Intersector.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

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

// § NX 스타일 포인트 스냅(2026-09-11) - "포인트가 EDGE에 안 맞고 DATUM(면)으로 인식된다"는
// 피드백에 따라 PickFace가 면 판별 전에 꼭짓점/모서리 중간점을 먼저 찾는다. 위 deflection과
// 같은 이유(작은 부품/큰 어셈블리 모두 일관된 "클릭 여유")로 바운딩박스 대각선에 비례한
// 허용오차를 쓴다 - deflection보다 훨씬 커야(눈에 보이는 점선이 아니라 실제 클릭 여유라서)
// 0.6%로 잡았다.
constexpr double kSnapToleranceRatio = 0.006; // 대각선의 0.6%
constexpr double kMinSnapToleranceMm = 1.0;   // 아주 작은 부품에서도 최소 1mm는 보장

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

std::vector<Vec3> StepGeometryAdapter::GetRenderEdges(ModelHandle handle) const {
    const auto* model = impl_->Find(handle);
    if (!model) {
        return {};
    }
    // 면 테셀레이션과 같은 deflection - 시각적으로 어색한 굵기 차이 없이 일관되게 보인다.
    const double linearDeflection =
        std::max(BoundingDiagonal(model->bounds) * kLinearDeflectionRatio, 1e-3);

    std::vector<Vec3> segments;
    for (TopExp_Explorer edgeExp(model->shape, TopAbs_EDGE); edgeExp.More(); edgeExp.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(edgeExp.Current());
        BRepAdaptor_Curve curve(edge);
        GCPnts_QuasiUniformDeflection discretizer(curve, linearDeflection);
        if (!discretizer.IsDone() || discretizer.NbPoints() < 2) {
            continue; // 퇴화 엣지 등 - 건너뜀.
        }
        for (int i = 1; i < discretizer.NbPoints(); ++i) {
            const gp_Pnt p1 = discretizer.Value(i);
            const gp_Pnt p2 = discretizer.Value(i + 1);
            segments.push_back(Vec3{p1.X(), p1.Y(), p1.Z()});
            segments.push_back(Vec3{p2.X(), p2.Y(), p2.Z()});
        }
    }
    return segments;
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

    const gp_Pnt origin(rayOrigin.x, rayOrigin.y, rayOrigin.z);
    const gp_Dir dir(rayDir.x, rayDir.y, rayDir.z);
    const gp_Vec dirVec(dir);
    const gp_Lin line(origin, dir);

    // § NX 스타일 포인트 스냅 - 면 판별보다 먼저 광선 근처(모델 크기에 비례한 허용오차
    // 안)의 꼭짓점/모서리 중간점을 찾는다. 여러 후보가 허용오차 안에 있으면 광선에 가장
    // 가까운(=화면에서 커서에 가장 가까워 보일) 것을 고른다 - "여러 번 눌러야 겨우
    // 맞는다"는 피드백에 대응.
    const double snapToleranceMm =
        std::max(kMinSnapToleranceMm, BoundingDiagonal(model->bounds) * kSnapToleranceRatio);
    double bestPointDistToRay = std::numeric_limits<double>::max();
    gp_Pnt bestPoint;
    PointSubKind bestPointSubKind = PointSubKind::None;
    TopoDS_Edge bestPointEdge; // bestPointSubKind==EdgeMidpoint일 때만 유효 - 하이라이트용.

    auto considerPoint = [&](const gp_Pnt& p, PointSubKind subKind, const TopoDS_Edge& edge) {
        const double param = gp_Vec(origin, p).Dot(dirVec);
        if (param < 0.0) {
            return; // 카메라 뒤쪽 - 화면에 안 보이는 점은 후보에서 제외.
        }
        const double dist = line.Distance(p);
        if (dist > snapToleranceMm || dist >= bestPointDistToRay) {
            return;
        }
        bestPointDistToRay = dist;
        bestPoint = p;
        bestPointSubKind = subKind;
        bestPointEdge = edge;
    };

    // § NX 포인트 생성자 스타일 확장(2026-09-13) - "POINT도 교차점/끝점/시작점/중앙점으로
    // 하이라이트되게 해달라"는 요청. 시작점/끝점을 구분하고(예전엔 Vertex 하나로 합쳤음),
    // 원/호 모서리의 실제 중심(곡선 위 점이 아님)도 후보에 추가한다. "커서 근처에 있는
    // 모서리"만 따로 모아뒀다가 아래에서 교차점 탐색에 재사용한다(전체 모서리 쌍을
    // O(n²)로 비교하면 모델이 클 때 느려지므로, 커서 근방으로 후보를 좁힌다).
    constexpr double kNearbyEdgeToleranceRatio = 4.0; // 스냅 허용오차의 4배 - 교차점 탐색용 느슨한 반경
    struct NearbyEdge {
        TopoDS_Edge edge;
        double first = 0.0;
        double last = 0.0;
    };
    std::vector<NearbyEdge> nearbyEdges;

    for (TopExp_Explorer edgeExp(model->shape, TopAbs_EDGE); edgeExp.More(); edgeExp.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(edgeExp.Current());
        double first = 0.0;
        double last = 0.0;
        const Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
        if (curve.IsNull()) {
            continue; // 퇴화 엣지(degenerate, 예: 원뿔 꼭짓점) 등 실제 곡선이 없는 경우.
        }
        const gp_Pnt startPnt = curve->Value(first);
        const gp_Pnt endPnt = curve->Value(last);
        const gp_Pnt midPnt = curve->Value((first + last) * 0.5);
        considerPoint(startPnt, PointSubKind::StartPoint, edge);
        considerPoint(endPnt, PointSubKind::EndPoint, edge);
        considerPoint(midPnt, PointSubKind::EdgeMidpoint, edge);

        BRepAdaptor_Curve curveAdaptor(edge);
        if (curveAdaptor.GetType() == GeomAbs_Circle) {
            considerPoint(curveAdaptor.Circle().Location(), PointSubKind::Center, edge);
        }

        const double nearTol = snapToleranceMm * kNearbyEdgeToleranceRatio;
        if (line.Distance(startPnt) <= nearTol || line.Distance(endPnt) <= nearTol ||
            line.Distance(midPnt) <= nearTol) {
            nearbyEdges.push_back({edge, first, last});
        }
    }

    // § 교차점 탐색 - 커서 근처(nearbyEdges, 보통 0~수 개)의 모서리 쌍만 촘촘히 샘플링해서
    // 서로 가장 가까워지는 지점을 찾는다. 그 최소 거리가 아주 작으면(실제로 만나거나
    // 맞닿음) 두 샘플의 중점을 교차점 후보로 삼는다 - 정확한 곡선-곡선 교차 계산(무거움)
    // 대신 쓰는 실용적 근사. nearbyEdges로 후보를 미리 좁혀서 전체 모서리 쌍 비교
    // (O(전체 모서리 수²))를 피한다.
    constexpr int kIntersectionSampleCount = 24;
    constexpr double kIntersectionTouchToleranceMm = 0.5;
    for (size_t a = 0; a < nearbyEdges.size(); ++a) {
        BRepAdaptor_Curve curveA(nearbyEdges[a].edge);
        for (size_t b = a + 1; b < nearbyEdges.size(); ++b) {
            BRepAdaptor_Curve curveB(nearbyEdges[b].edge);
            double bestPairDist = std::numeric_limits<double>::max();
            gp_Pnt bestPairMid;
            for (int ia = 0; ia <= kIntersectionSampleCount; ++ia) {
                const double ta = nearbyEdges[a].first +
                    (nearbyEdges[a].last - nearbyEdges[a].first) * ia / kIntersectionSampleCount;
                const gp_Pnt pa = curveA.Value(ta);
                for (int ib = 0; ib <= kIntersectionSampleCount; ++ib) {
                    const double tb = nearbyEdges[b].first +
                        (nearbyEdges[b].last - nearbyEdges[b].first) * ib / kIntersectionSampleCount;
                    const gp_Pnt pb = curveB.Value(tb);
                    const double d = pa.Distance(pb);
                    if (d < bestPairDist) {
                        bestPairDist = d;
                        bestPairMid = gp_Pnt(
                            (pa.X() + pb.X()) * 0.5, (pa.Y() + pb.Y()) * 0.5, (pa.Z() + pb.Z()) * 0.5);
                    }
                }
            }
            if (bestPairDist <= kIntersectionTouchToleranceMm) {
                considerPoint(bestPairMid, PointSubKind::Intersection, nearbyEdges[a].edge);
            }
        }
    }

    if (bestPointSubKind != PointSubKind::None) {
        PickResult result;
        result.kind = PickedFaceKind::Point;
        result.pointSubKind = bestPointSubKind;
        result.point = Vec3{bestPoint.X(), bestPoint.Y(), bestPoint.Z()};
        // § 하이라이트 미리보기(2026-09-13 재조정) - "면만/선만/점만 각각 따로 표시되게"
        // 요청에 맞춰 세 범주를 배타적으로 나눴다: 시작점/끝점(=실제 꼭짓점)은 "점" 범주라
        // 점만 표시하고, 모서리 중간점만 "선" 범주로 그 모서리 전체를 선분으로 뽑아
        // ModelViewport가 점 대신 선만 보여주게 한다(중앙점/교차점도 "점" 범주 - 계속
        // 점으로만 표시).
        if (bestPointSubKind == PointSubKind::EdgeMidpoint) {
            BRepAdaptor_Curve edgeCurve(bestPointEdge);
            const double linearDeflection =
                std::max(BoundingDiagonal(model->bounds) * kLinearDeflectionRatio, 1e-3);
            GCPnts_QuasiUniformDeflection discretizer(edgeCurve, linearDeflection);
            if (discretizer.IsDone()) {
                for (int i = 1; i < discretizer.NbPoints(); ++i) {
                    const gp_Pnt p1 = discretizer.Value(i);
                    const gp_Pnt p2 = discretizer.Value(i + 1);
                    result.highlightEdgeSegments.push_back(Vec3{p1.X(), p1.Y(), p1.Z()});
                    result.highlightEdgeSegments.push_back(Vec3{p2.X(), p2.Y(), p2.Z()});
                }
            }
        }
        return result;
    }

    // 근처에 스냅할 점이 없으면 기존처럼 면 판별로 폴백.
    // 모델의 모든 면과 광선을 정확히 교차시켜(근사 없음), 광선을 따라 가장 먼저(=가장
    // 가까운) 맞는 면을 찾는다 - 여러 면이 겹쳐 보여도 화면에서 실제로 보이는 면이
    // 뽑히도록 하기 위함(뒤에 가려진 면이 아니라).
    constexpr double kIntersectionTolerance = 1e-4;
    double bestParam = std::numeric_limits<double>::max();
    TopoDS_Face bestFace;
    gp_Pnt bestFacePoint;
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
                bestFacePoint = intersector.Pnt(i);
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
        const gp_Dir normal = plane.Axis().Direction();
        result.kind = PickedFaceKind::Plane;
        // § 라이브 호버 미리보기(2026-09-11) - 예전엔 plane.Location()(평면의 임의
        // 기준점 - 실제로 클릭한 위치와 무관하게 멀리 떨어져 있을 수 있음)을 썼는데,
        // 마커/호버 프리뷰를 실제 커서 위치에 그리려면 광선이 실제로 맞은 지점이어야
        // 한다. 점+법선만 있으면 되는 point_to_plane 거리 계산(Measure)에는 어느 점을
        // 써도 결과가 같아 이 변경으로 깨지는 계산은 없다.
        result.point = Vec3{bestFacePoint.X(), bestFacePoint.Y(), bestFacePoint.Z()};
        result.normal = Vec3{normal.X(), normal.Y(), normal.Z()};
    }
    // 그 외 면 타입(원뿔/자유곡면 등)은 지금 단계에서 지원 안 함 - kind는 None으로 남는다.

    // § 하이라이트 미리보기 - 면 전체(구멍 원통/평면)를 주황색으로 보여주려고 그 면의
    // 캐시된 테셀레이션(로드 시 BRepMesh_IncrementalMesh로 이미 계산됨, GetRenderTriangles와
    // 같은 방식)을 그대로 재사용한다 - 다시 미소분할하지 않아 매 마우스 이동마다 호출돼도
    // 가볍다.
    if (result.kind != PickedFaceKind::None) {
        TopLoc_Location location;
        const Handle(Poly_Triangulation)& tri = BRep_Tool::Triangulation(bestFace, location);
        if (!tri.IsNull()) {
            const gp_Trsf& trsf = location.Transformation();
            const bool reversed = bestFace.Orientation() == TopAbs_REVERSED;
            result.highlightTriangles.reserve(static_cast<size_t>(tri->NbTriangles()) * 3);
            for (int i = 1; i <= tri->NbTriangles(); ++i) {
                int i1, i2, i3;
                tri->Triangle(i).Get(i1, i2, i3);
                if (reversed) {
                    std::swap(i1, i3);
                }
                const gp_Pnt p1 = tri->Node(i1).Transformed(trsf);
                const gp_Pnt p2 = tri->Node(i2).Transformed(trsf);
                const gp_Pnt p3 = tri->Node(i3).Transformed(trsf);
                result.highlightTriangles.push_back(Vec3{p1.X(), p1.Y(), p1.Z()});
                result.highlightTriangles.push_back(Vec3{p2.X(), p2.Y(), p2.Z()});
                result.highlightTriangles.push_back(Vec3{p3.X(), p3.Y(), p3.Z()});
            }
        }
    }

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
