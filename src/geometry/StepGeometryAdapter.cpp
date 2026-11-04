// § 유니코드 경로 지원 - windows.h를 파일 맨 앞에서 먼저 통째로 포함시킨다. OCCT의
// Standard_Macro.hxx가 WIN32_LEAN_AND_MEAN을 정의한 뒤 windows.h를 포함해서, OCCT
// 헤더들보다 뒤에 windows.h를 include하면(#pragma once 가드 때문에) stringapiset.h가
// 필요로 하는 winnls.h 선언들이 빠진 채로 남는다 - 먼저 완전한 windows.h를 넣어서 이
// 문제를 피한다.
#ifdef _WIN32
#include <windows.h>
#include <stringapiset.h>
#endif

#include "geometry/StepGeometryAdapter.h"

#include "geometry/FeaturePatch.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
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
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
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
#include <fstream>
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

Vec3 ToVec3(const gp_Pnt& p) {
    return Vec3{p.X(), p.Y(), p.Z()};
}

Vec3 ToVec3(const gp_Dir& d) {
    return Vec3{d.X(), d.Y(), d.Z()};
}

// § 측정 재점검(2026-10-30) - 원통면의 대표점. gp_Cylinder의 V 파라미터는 축 방향 거리라서,
// 면의 V 구간 중앙을 축 위에 찍으면 "그 원통면이 실제로 차지하는 구간의 한가운데"가 된다.
// 예전엔 cylinder.Axis().Location()(STEP에 기록된 축 기준점 - 실제 구멍에서 수십 mm 떨어져
// 있을 수 있음)을 그대로 써서 구멍 사이 3D 거리에 축 방향 어긋남이 섞였다. 클릭(PickFace)과
// 엔진 검색(FindAnchorCandidates)이 이 함수를 같이 써야 같은 구멍이 같은 좌표로 나온다.
gp_Pnt CylinderFaceCenter(const TopoDS_Face& face, const gp_Cylinder& cylinder) {
    double uMin = 0.0;
    double uMax = 0.0;
    double vMin = 0.0;
    double vMax = 0.0;
    BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);
    const gp_Ax3& position = cylinder.Position();
    return position.Location().Translated(gp_Vec(position.Direction()) * ((vMin + vMax) * 0.5));
}

// 면 방향(TopAbs_REVERSED)과 좌표계 손잡이(왼손 Ax3)까지 반영한 평면의 바깥쪽 법선 - 판재
// 앞/뒷면처럼 같은 자리에서 마주보는 두 면을 부호로 구분하기 위해(rule::Matching 참고).
gp_Dir OrientedPlaneNormal(const TopoDS_Face& face, const gp_Pln& plane) {
    gp_Dir normal = plane.Axis().Direction();
    if (!plane.Direct()) {
        normal.Reverse();
    }
    if (face.Orientation() == TopAbs_REVERSED) {
        normal.Reverse();
    }
    return normal;
}

BoundingBox FaceBoundingBox(const TopoDS_Face& face) {
    Bnd_Box box;
    BRepBndLib::Add(face, box);
    if (box.IsVoid()) {
        return BoundingBox{};
    }
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    return BoundingBox{Vec3{xmin, ymin, zmin}, Vec3{xmax, ymax, zmax}};
}

// 면 bbox 중심을 평면 위로 내린 점 - plane.Location()(STEP 기준점, 면 밖일 수 있음) 대신
// 실제 면 근처의 대표점으로 쓴다.
gp_Pnt FaceCenterOnPlane(const BoundingBox& faceBox, const gp_Pln& plane) {
    const gp_Pnt boxCenter((faceBox.min.x + faceBox.max.x) * 0.5, (faceBox.min.y + faceBox.max.y) * 0.5,
                           (faceBox.min.z + faceBox.max.z) * 0.5);
    const gp_Vec normal(plane.Axis().Direction());
    const double offset = gp_Vec(plane.Location(), boxCenter).Dot(normal);
    return boxCenter.Translated(normal * -offset);
}

// § 교차점 탐색 공용(2026-10-30) - PickFace(커서 근처)와 FindPointCandidates(측정 엔진)가
// 같은 근사로 교차점을 구해야 클릭한 좌표와 엔진이 다시 찾은 좌표가 일치한다.
struct NearbyEdge {
    TopoDS_Edge edge;
    double first = 0.0;
    double last = 0.0;
};

// 두 모서리를 촘촘히 샘플링해서 서로 가장 가까워지는 지점을 찾는다. 그 최소 거리가 아주
// 작으면(실제로 만나거나 맞닿음) 두 샘플의 중점을 교차점으로 삼는다 - 정확한 곡선-곡선 교차
// 계산(무거움) 대신 쓰는 실용적 근사.
bool ApproxEdgeIntersection(const NearbyEdge& a, const NearbyEdge& b, gp_Pnt& out) {
    constexpr int kIntersectionSampleCount = 24;
    constexpr double kIntersectionTouchToleranceMm = 0.5;
    BRepAdaptor_Curve curveA(a.edge);
    BRepAdaptor_Curve curveB(b.edge);
    double bestPairDist = std::numeric_limits<double>::max();
    gp_Pnt bestPairMid;
    for (int ia = 0; ia <= kIntersectionSampleCount; ++ia) {
        const double ta = a.first + (a.last - a.first) * ia / kIntersectionSampleCount;
        const gp_Pnt pa = curveA.Value(ta);
        for (int ib = 0; ib <= kIntersectionSampleCount; ++ib) {
            const double tb = b.first + (b.last - b.first) * ib / kIntersectionSampleCount;
            const gp_Pnt pb = curveB.Value(tb);
            const double d = pa.Distance(pb);
            if (d < bestPairDist) {
                bestPairDist = d;
                bestPairMid = gp_Pnt((pa.X() + pb.X()) * 0.5, (pa.Y() + pb.Y()) * 0.5, (pa.Z() + pb.Z()) * 0.5);
            }
        }
    }
    if (bestPairDist > kIntersectionTouchToleranceMm) {
        return false;
    }
    out = bestPairMid;
    return true;
}

// § 형상 프리셋 - CaptureFeaturePatch의 clickPoint(월드 좌표)에서 가장 가까운 면을 찾는다.
// 클릭 지점은 이미 뷰어 피킹(PickFace)으로 형상 표면 위에서 얻은 점이라 실제로는 거리가
// 0에 매우 가깝다 - 전체 면을 순회하는 O(n) 비용은 클릭 1회당 일회성이라 허용.
// § 하이라이트 공용 헬퍼(2026-09-18) - PickFace(면 하나)와 CaptureFeaturePatch(면 여러
// 개짜리 패치)가 똑같이 "이 면의 캐시된 테셀레이션을 월드좌표 삼각형으로 변환해서
// 하이라이트 목록에 이어붙이는" 로직을 쓴다 - PickFace에 원래 있던 코드를 재사용
// 가능하게 빼낸 것뿐, 동작은 그대로.
void AppendFaceTriangles(const TopoDS_Face& face, std::vector<Vec3>& out) {
    TopLoc_Location location;
    const Handle(Poly_Triangulation)& tri = BRep_Tool::Triangulation(face, location);
    if (tri.IsNull()) {
        return;
    }
    const gp_Trsf& trsf = location.Transformation();
    const bool reversed = face.Orientation() == TopAbs_REVERSED;
    out.reserve(out.size() + static_cast<size_t>(tri->NbTriangles()) * 3);
    for (int i = 1; i <= tri->NbTriangles(); ++i) {
        int i1, i2, i3;
        tri->Triangle(i).Get(i1, i2, i3);
        if (reversed) {
            std::swap(i1, i3);
        }
        const gp_Pnt p1 = tri->Node(i1).Transformed(trsf);
        const gp_Pnt p2 = tri->Node(i2).Transformed(trsf);
        const gp_Pnt p3 = tri->Node(i3).Transformed(trsf);
        out.push_back(Vec3{p1.X(), p1.Y(), p1.Z()});
        out.push_back(Vec3{p2.X(), p2.Y(), p2.Z()});
        out.push_back(Vec3{p3.X(), p3.Y(), p3.Z()});
    }
}

// § 형상 프리셋 수동 선택(2026-09-18) - 인덱스는 TopExp_Explorer(shape, TopAbs_FACE) 순회
// 순서 기준. 모델(shape)이 바뀌지 않는 한 항상 같은 순서라 안정적인 키로 쓸 수 있다.
bool GetFaceByIndex(const TopoDS_Shape& shape, int index, TopoDS_Face& outFace) {
    if (index < 0) {
        return false;
    }
    int i = 0;
    for (TopExp_Explorer faceExp(shape, TopAbs_FACE); faceExp.More(); faceExp.Next(), ++i) {
        if (i == index) {
            outFace = TopoDS::Face(faceExp.Current());
            return true;
        }
    }
    return false;
}

bool FindNearestFace(const TopoDS_Shape& shape, const gp_Pnt& point, TopoDS_Face& outFace) {
    const TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(point);
    double bestDist = std::numeric_limits<double>::max();
    bool found = false;
    for (TopExp_Explorer faceExp(shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
        const TopoDS_Face& face = TopoDS::Face(faceExp.Current());
        BRepExtrema_DistShapeShape extrema(vertex, face);
        if (!extrema.IsDone() || extrema.NbSolution() == 0) {
            continue;
        }
        const double dist = extrema.Value();
        if (dist < bestDist) {
            bestDist = dist;
            outFace = face;
            found = true;
        }
    }
    return found;
}

#ifdef _WIN32
// § 유니코드 경로 지원(2026-09-18) - "●" 같은 특수문자가 든 경로에서 STEP 로드가
// 실패한다는 리포트. STEPControl_Reader::ReadFile(const char*)는 OS의 narrow 문자열
// API(현재 ANSI 코드페이지 기준)를 타는데, filePath는 Qt QString::toStdString()이 만든
// UTF-8이라 시스템 코드페이지에 없는 문자(●는 물론 실사용에서 흔한 한글 경로도 포함될
// 가능성)가 있으면 깨진다. UTF-8 -> UTF-16으로 직접 바꿔서 std::ifstream을 열면
// 코드페이지와 무관하게 항상 정확한 파일을 찾는다(MSVC STL의 wchar_t* ifstream 생성자
// 확장 기능 사용 - 이 프로젝트는 MSVC 전용이라 안전).
std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return std::wstring();
    }
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wideLen <= 0) {
        return std::wstring();
    }
    std::wstring wide(static_cast<size_t>(wideLen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), wideLen);
    return wide;
}
#endif

} // namespace

// 점에서 축정렬 박스까지의 거리 제곱(박스 안이면 0).
double SquaredDistanceToBox(const gp_Pnt& p, const BoundingBox& box) {
    const double dx = std::max({box.min.x - p.X(), 0.0, p.X() - box.max.x});
    const double dy = std::max({box.min.y - p.Y(), 0.0, p.Y() - box.max.y});
    const double dz = std::max({box.min.z - p.Z(), 0.0, p.Z() - box.max.z});
    return dx * dx + dy * dy + dz * dz;
}

struct StepGeometryAdapter::Impl {
    struct LoadedModel {
        std::string filePath;
        TopoDS_Shape shape;
        BoundingBox bounds;
        // § 주변 지문 검증(2026-11-04) - 비교 테이블을 갱신할 때마다 규칙×인치×포인트 수만큼
        // CaptureLocalPatch가 불리므로, 모델이 바뀌지 않는 한 같은 값인 면 목록/면 bbox/모서리-면
        // 맵은 처음 쓸 때 한 번만 만든다(LoadedModel은 const로 다뤄지므로 mutable).
        mutable std::vector<std::pair<TopoDS_Face, BoundingBox>> faceBoxes;
        mutable std::unique_ptr<TopTools_IndexedDataMapOfShapeListOfShape> edgeFaceMap;

        const std::vector<std::pair<TopoDS_Face, BoundingBox>>& FaceBoxes() const {
            if (faceBoxes.empty()) {
                for (TopExp_Explorer faceExp(shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
                    const TopoDS_Face& face = TopoDS::Face(faceExp.Current());
                    faceBoxes.emplace_back(face, FaceBoundingBox(face));
                }
            }
            return faceBoxes;
        }

        const TopTools_IndexedDataMapOfShapeListOfShape& EdgeFaceMap() const {
            if (!edgeFaceMap) {
                edgeFaceMap = std::make_unique<TopTools_IndexedDataMapOfShapeListOfShape>();
                TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, *edgeFaceMap);
            }
            return *edgeFaceMap;
        }

        // FindNearestFace와 같은 결과를 내되, 면 bbox까지의 거리가 지금까지의 최솟값보다 먼 면은
        // 정확한 거리 계산(BRepExtrema)을 건너뛴다 - bbox가 가까운 면부터 본다.
        bool FindNearestFaceFast(const gp_Pnt& point, TopoDS_Face& outFace) const {
            const auto& boxes = FaceBoxes();
            std::vector<std::pair<double, std::size_t>> order;
            order.reserve(boxes.size());
            for (std::size_t i = 0; i < boxes.size(); ++i) {
                order.emplace_back(SquaredDistanceToBox(point, boxes[i].second), i);
            }
            std::sort(order.begin(), order.end());
            const TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(point);
            double bestDist = std::numeric_limits<double>::max();
            bool found = false;
            for (const auto& [boxDistSq, index] : order) {
                if (found && boxDistSq > bestDist * bestDist) {
                    break;
                }
                BRepExtrema_DistShapeShape extrema(vertex, boxes[index].first);
                if (!extrema.IsDone() || extrema.NbSolution() == 0) {
                    continue;
                }
                if (extrema.Value() < bestDist) {
                    bestDist = extrema.Value();
                    outFace = boxes[index].first;
                    found = true;
                }
            }
            return found;
        }
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
#ifdef _WIN32
    // 유니코드 경로 지원 - 위 Utf8ToWide() 주석 참고. ReadStream의 첫 인자(theName)는
    // 진단 로그용 라벨일 뿐이라 실제 경로일 필요는 없다.
    std::ifstream fileStream(Utf8ToWide(filePath), std::ios::binary);
    if (!fileStream.is_open()) {
        throw std::runtime_error("StepGeometryAdapter::LoadModel - STEP 파일을 열 수 없습니다: " + filePath);
    }
    const IFSelect_ReturnStatus status = reader.ReadStream("step_input", fileStream);
#else
    const IFSelect_ReturnStatus status = reader.ReadFile(filePath.c_str());
#endif
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
    BRepMesh_IncrementalMesh meshAlgo(model.shape, linearDeflection, /*isRelative=*/false, kAngularDeflectionRad);
    (void)meshAlgo;

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

    // § 측정 재점검(2026-10-30) - STEP의 원통은 보통 반원통 면 2개로 나뉘어 있어 구멍 하나가
    // 후보 2개(같은 중앙점/지름)로 잡혔다 - 개수(instance_count)가 2배로 나오고 같은 자리
    // 후보가 겹쳐 "ambiguous"가 됐다. 중앙점과 지름이 같은 면은 하나로 합친다.
    constexpr double kSameHoleToleranceMm = 0.01;
    for (TopExp_Explorer faceExp(model->shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
        const TopoDS_Face& face = TopoDS::Face(faceExp.Current());
        BRepAdaptor_Surface surface(face, /*restrictTriangulation=*/false);
        if (surface.GetType() != GeomAbs_Cylinder) {
            continue;
        }
        const gp_Cylinder cylinder = surface.Cylinder();
        AnchorCandidate candidate{
            anchorType, partName, ToVec3(CylinderFaceCenter(face, cylinder)), cylinder.Radius() * 2.0};
        candidate.axis = ToVec3(cylinder.Axis().Direction());
        candidates.push_back(std::move(candidate));
    }
    // TV 어셈블리는 원통 면이 수만 개라 전체 쌍 비교(O(n²))는 느리다 - x로 정렬해 두고 x 차이가
    // 허용오차 안인 바로 앞 후보들만 비교한다.
    std::sort(candidates.begin(), candidates.end(),
              [](const AnchorCandidate& a, const AnchorCandidate& b) { return a.position.x < b.position.x; });
    std::vector<AnchorCandidate> unique;
    unique.reserve(candidates.size());
    for (auto& c : candidates) {
        bool duplicate = false;
        for (auto it = unique.rbegin(); it != unique.rend() && c.position.x - it->position.x <= kSameHoleToleranceMm;
             ++it) {
            const double dy = c.position.y - it->position.y;
            const double dz = c.position.z - it->position.z;
            const double dx = c.position.x - it->position.x;
            if (std::abs(c.diameterMm - it->diameterMm) <= kSameHoleToleranceMm &&
                std::sqrt(dx * dx + dy * dy + dz * dz) <= kSameHoleToleranceMm) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            unique.push_back(std::move(c));
        }
    }
    return unique;
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
        PlaneCandidate candidate;
        candidate.planeType = planeType;
        candidate.partName = partName;
        candidate.faceBox = FaceBoundingBox(face);
        candidate.hasFaceBox = true;
        candidate.pointOnPlane = ToVec3(FaceCenterOnPlane(candidate.faceBox, plane));
        candidate.normal = ToVec3(OrientedPlaneNormal(face, plane));
        candidates.push_back(std::move(candidate));
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

    // § 교차점 탐색 - 커서 근처(nearbyEdges, 보통 0~수 개)의 모서리 쌍만 비교한다
    // (ApproxEdgeIntersection 참고). nearbyEdges로 후보를 미리 좁혀서 전체 모서리 쌍 비교
    // (O(전체 모서리 수²))를 피한다.
    for (size_t a = 0; a < nearbyEdges.size(); ++a) {
        for (size_t b = a + 1; b < nearbyEdges.size(); ++b) {
            gp_Pnt intersection;
            if (ApproxEdgeIntersection(nearbyEdges[a], nearbyEdges[b], intersection)) {
                considerPoint(intersection, PointSubKind::Intersection, nearbyEdges[a].edge);
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
        result.kind = PickedFaceKind::Cylinder;
        result.point = ToVec3(CylinderFaceCenter(bestFace, cylinder));
        result.diameterMm = cylinder.Radius() * 2.0;
    } else if (surface.GetType() == GeomAbs_Plane) {
        const gp_Pln plane = surface.Plane();
        const gp_Dir normal = OrientedPlaneNormal(bestFace, plane);
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
        AppendFaceTriangles(bestFace, result.highlightTriangles);
    }

    return result;
}

std::vector<AnchorCandidate> StepGeometryAdapter::FindPointCandidates(
    ModelHandle handle, PointSubKind subKind, const Vec3& searchCenter, double radiusMm) const {
    const auto* model = impl_->Find(handle);
    if (!model) {
        return {};
    }
    const gp_Pnt nearPnt(searchCenter.x, searchCenter.y, searchCenter.z);
    std::vector<AnchorCandidate> candidates;
    // 꼭짓점은 여러 모서리가 공유해서 같은 점이 반복해 나온다 - 아주 가까운 점은 하나로 합친다.
    constexpr double kSamePointToleranceMm = 1e-3;
    auto add = [&](const gp_Pnt& p) {
        if (p.Distance(nearPnt) > radiusMm) {
            return;
        }
        const bool duplicate = std::any_of(candidates.begin(), candidates.end(), [&](const AnchorCandidate& c) {
            return p.Distance(gp_Pnt(c.position.x, c.position.y, c.position.z)) <= kSamePointToleranceMm;
        });
        if (!duplicate) {
            AnchorCandidate candidate;
            candidate.position = ToVec3(p);
            candidates.push_back(std::move(candidate));
        }
    };

    if (subKind == PointSubKind::Vertex || subKind == PointSubKind::StartPoint || subKind == PointSubKind::EndPoint) {
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(model->shape, TopAbs_VERTEX, vertices);
        for (int i = 1; i <= vertices.Extent(); ++i) {
            add(BRep_Tool::Pnt(TopoDS::Vertex(vertices(i))));
        }
        return candidates;
    }

    // TopExp_Explorer는 공유 모서리를 면마다 반복해서 돌려주므로 고유 모서리 맵으로 한 번씩만 본다.
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(model->shape, TopAbs_EDGE, edges);
    std::vector<NearbyEdge> nearbyEdges;
    for (int i = 1; i <= edges.Extent(); ++i) {
        const TopoDS_Edge& edge = TopoDS::Edge(edges(i));
        double first = 0.0;
        double last = 0.0;
        const Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
        if (curve.IsNull()) {
            continue;
        }
        if (subKind == PointSubKind::EdgeMidpoint) {
            add(curve->Value((first + last) * 0.5));
        } else if (subKind == PointSubKind::Center) {
            BRepAdaptor_Curve curveAdaptor(edge);
            if (curveAdaptor.GetType() == GeomAbs_Circle) {
                add(curveAdaptor.Circle().Location());
            }
        } else if (subKind == PointSubKind::Intersection) {
            // PickFace와 같은 기준 - 모서리의 시작/끝/중간점 중 하나라도 탐색 반경 안이면 후보.
            if (curve->Value(first).Distance(nearPnt) <= radiusMm || curve->Value(last).Distance(nearPnt) <= radiusMm ||
                curve->Value((first + last) * 0.5).Distance(nearPnt) <= radiusMm) {
                nearbyEdges.push_back({edge, first, last});
            }
        }
    }
    for (size_t a = 0; a < nearbyEdges.size(); ++a) {
        for (size_t b = a + 1; b < nearbyEdges.size(); ++b) {
            gp_Pnt intersection;
            if (ApproxEdgeIntersection(nearbyEdges[a], nearbyEdges[b], intersection)) {
                add(intersection);
            }
        }
    }
    return candidates;
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
            center = ToVec3(FaceCenterOnPlane(FaceBoundingBox(face), plane));
            normal = ToVec3(OrientedPlaneNormal(face, plane));
        } else if (surface.GetType() == GeomAbs_Cylinder) {
            const gp_Cylinder cylinder = surface.Cylinder();
            center = ToVec3(CylinderFaceCenter(face, cylinder));
            normal = ToVec3(cylinder.Axis().Direction());
        } else {
            continue;
        }
        candidates.push_back(FaceCandidate{faceType, partName, center, normal});
    }
    return candidates;
}

FeaturePatchCapture StepGeometryAdapter::CaptureFeaturePatch(ModelHandle handle, const Vec3& clickPoint) const {
    const auto* model = impl_->Find(handle);
    if (!model) {
        return FeaturePatchCapture{};
    }

    TopoDS_Face seedFace;
    if (!FindNearestFace(model->shape, gp_Pnt(clickPoint.x, clickPoint.y, clickPoint.z), seedFace)) {
        return FeaturePatchCapture{};
    }

    const double maxRadius = BoundingDiagonal(model->bounds) * kPatchRadiusRatio;
    const std::vector<TopoDS_Face> patch = ExtractFeaturePatch(model->shape, seedFace, maxRadius);
    if (patch.empty()) {
        return FeaturePatchCapture{};
    }

    FeaturePatchCapture capture;
    capture.descriptor = ComputePatchDescriptor(patch);
    capture.position = PatchCentroid(patch);
    capture.valid = true;
    for (const auto& face : patch) {
        AppendFaceTriangles(face, capture.highlightTriangles);
    }
    return capture;
}

FeaturePatchCapture StepGeometryAdapter::CaptureLocalPatch(
    ModelHandle handle, const Vec3& center, double radiusMm) const {
    const auto* model = impl_->Find(handle);
    if (!model) {
        return FeaturePatchCapture{};
    }
    const gp_Pnt centerPnt(center.x, center.y, center.z);
    TopoDS_Face seedFace;
    if (!model->FindNearestFaceFast(centerPnt, seedFace)) {
        return FeaturePatchCapture{};
    }
    const std::vector<TopoDS_Face> patch = ExtractLocalPatch(model->EdgeFaceMap(), seedFace, centerPnt, radiusMm);
    if (patch.empty()) {
        return FeaturePatchCapture{};
    }
    FeaturePatchCapture capture;
    capture.descriptor = ComputePatchDescriptor(patch);
    capture.position = center;
    capture.valid = true;
    return capture;
}

std::vector<PatchCandidate> StepGeometryAdapter::FindPatchCandidates(
    ModelHandle handle, const FeaturePatchDescriptor& descriptor, double similarityThreshold) const {
    const auto* model = impl_->Find(handle);
    if (!model) {
        return {};
    }

    const std::vector<FeaturePatchCandidate> rawCandidates =
        geometry::FindPatchCandidates(model->shape, descriptor, /*excludeFaces=*/{}, similarityThreshold);

    std::vector<PatchCandidate> result;
    result.reserve(rawCandidates.size());
    for (const auto& candidate : rawCandidates) {
        result.push_back(PatchCandidate{candidate.position, candidate.similarity});
    }
    return result;
}

int StepGeometryAdapter::PickFaceIndex(ModelHandle handle, const Vec3& rayOrigin, const Vec3& rayDir) const {
    const auto* model = impl_->Find(handle);
    if (!model) {
        return -1;
    }

    const gp_Pnt origin(rayOrigin.x, rayOrigin.y, rayOrigin.z);
    const gp_Dir dir(rayDir.x, rayDir.y, rayDir.z);
    const gp_Lin line(origin, dir);

    // PickFace의 면 폴백 구간과 같은 방식(정확한 광선-면 교차, 화면에서 실제로 보이는
    // 가장 가까운 면) - 다만 여기서는 원통/평면 분류를 안 하고 인덱스만 필요하다 -
    // 자유곡면(NURBS 등) 형상도 다뤄야 해서(스냅/후크류 실사용 형상 확인, 2026-09-18)
    // PickFace처럼 Cylinder/Plane으로 걸러내지 않는다.
    constexpr double kIntersectionTolerance = 1e-4;
    double bestParam = std::numeric_limits<double>::max();
    int bestIndex = -1;
    int index = 0;
    for (TopExp_Explorer faceExp(model->shape, TopAbs_FACE); faceExp.More(); faceExp.Next(), ++index) {
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
                bestIndex = index;
            }
        }
    }
    return bestIndex;
}

std::vector<Vec3> StepGeometryAdapter::GetFaceHighlightTriangles(ModelHandle handle, int faceIndex) const {
    const auto* model = impl_->Find(handle);
    if (!model) {
        return {};
    }
    TopoDS_Face face;
    if (!GetFaceByIndex(model->shape, faceIndex, face)) {
        return {};
    }
    std::vector<Vec3> triangles;
    AppendFaceTriangles(face, triangles);
    return triangles;
}

int StepGeometryAdapter::GetFaceCount(ModelHandle handle) const {
    const auto* model = impl_->Find(handle);
    if (!model) {
        return 0;
    }
    int count = 0;
    for (TopExp_Explorer faceExp(model->shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
        ++count;
    }
    return count;
}

FeaturePatchCapture StepGeometryAdapter::CaptureManualPatch(
    ModelHandle handle, const std::vector<int>& faceIndices) const {
    const auto* model = impl_->Find(handle);
    if (!model || faceIndices.empty()) {
        return FeaturePatchCapture{};
    }

    std::vector<TopoDS_Face> patch;
    patch.reserve(faceIndices.size());
    for (int idx : faceIndices) {
        TopoDS_Face face;
        if (GetFaceByIndex(model->shape, idx, face)) {
            patch.push_back(face);
        }
    }
    if (patch.empty()) {
        return FeaturePatchCapture{};
    }

    FeaturePatchCapture capture;
    capture.descriptor = ComputePatchDescriptor(patch);
    capture.position = PatchCentroid(patch);
    capture.valid = true;
    for (const auto& face : patch) {
        AppendFaceTriangles(face, capture.highlightTriangles);
    }
    return capture;
}

} // namespace geometry
