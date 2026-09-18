#pragma once

#include <array>
#include <string>
#include <vector>

namespace geometry {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct BoundingBox {
    Vec3 min;
    Vec3 max;
};

// Rule의 Anchor(§7)에 대응하는 실제 지오메트리 후보 하나. Selector가 여러 후보 중
// 하나(또는 pair)를 고르는 대상이 된다.
struct AnchorCandidate {
    std::string anchorType; // Hole, Boss_Center, Hook_Tip_Edge, ...
    std::string partName;   // Bezel, Rear_Chassis, ...
    Vec3 position;
    // 원통면(Hole/Boss_Center) 후보의 지름(mm). 평면/edge 기반 등 원통이 아닌 타입은 0.
    // Rule의 Anchor.paramKey=="diameter"일 때 후보를 걸러내는 데 쓰인다(RuleEngine 참고) -
    // "비슷한 지름의 구멍만 다 찾기" 요청에 대응하는 필드.
    double diameterMm = 0.0;
    // § 축 방향 필터(2026-09-09) - 원통의 축 방향(단위벡터 아님, 크기 무시하고 방향만
    // 씀). "지름은 같은데 축 방향이 달라 실제로는 다른 형상"인 후보를 구분하는 데 쓰인다
    // (as1_pe.stp 실측: Ø254mm 원통 58개가 지름만으로는 전혀 안 걸러졌던 문제).
    Vec3 axis;
    // § 형상 프리셋(2026-09-18) - FindPatchCandidates(PatchCandidate) 결과를
    // AnchorCandidate로 변환할 때만 0보다 큰 값이 채워진다(RuleEngine::ResolveAnchorCandidates
    // 참고). 일반 Hole/Boss_Center 등 기존 경로에서는 항상 0 - rule::SelectBestPatchMatch가
    // 이 값이 채워진 후보만 자동으로 최고점을 고른다.
    double patchSimilarity = 0.0;
};

// point_to_plane 측정의 기준 평면 후보. 평면 위 한 점 + 법선벡터로 표현.
struct PlaneCandidate {
    std::string planeType; // Datum_Plane 등
    std::string partName;
    Vec3 pointOnPlane;
    Vec3 normal;
};

// face_to_face_gap 측정용 면 후보. Phase4b 초안: 실제 면 테셀레이션이 아니라
// 중심점 + 법선벡터로 근사한다 (面 형상 전체가 아닌 대표점 기준 gap 계산).
// 회사PC에서 실제 JT 테셀레이션 데이터가 들어오면 최단거리 탐색으로 교체 필요.
struct FaceCandidate {
    std::string faceType; // Rib_Top_Surface, Boss_Outer_Wall, ...
    std::string partName;
    Vec3 center;
    Vec3 normal;
};

// § 3D 클릭 피킹(2026-09-09) - 텍스트로 anchor_type/지름을 타이핑하는 대신, 뷰어에서
// 마우스로 실제 면을 클릭해 그 자리에서 형상을 판별한다. kind는 원통("Cylinder")/
// 평면("Plane")과, § NX 스타일 포인트 스냅(2026-09-11)으로 추가된 "Point"(꼭짓점/모서리
// 중간점) - 후크처럼 여러 면이 합쳐진 복합 형상 인식(§ 대화 기록, "①단계"로 범위를 좁힌
// 부분)은 원격PC에서 실제 후크 샘플이 생기면 다음 단계로 확장.
enum class PickedFaceKind {
    None,     // 아무것도 안 맞음(광선이 형상을 비껴감)
    Cylinder, // FindAnchorCandidates의 Hole/Boss_Center와 같은 판별 기준
    Plane,    // FindPlaneCandidates의 Datum_Plane과 같은 판별 기준
    // § NX 스타일 포인트 스냅 - "치수 측정 포인트가 EDGE에 안 맞고 POINT가 아니라
    // DATUM(면)으로 인식된다"는 피드백에 따라 추가. 광선이 면에 닿기 전에 꼭짓점/모서리
    // 중간점이 (모델 크기에 비례한) 근접 허용오차 안에 있으면 이 kind로 스냅한다 - NX
    // Point 생성자의 "끝점/교차점" 계열 중 "끝점(=꼭짓점)"과 "중간점"에 해당. "교차점"(두
    // 모서리의 실제 교차)은 계산이 훨씬 무거워 이번 범위에서는 제외했다.
    Point,
};

// PickedFaceKind::Point일 때만 의미 있음 - 어떤 종류의 점인지(UI 라벨/§7 anchorType
// 문자열에 씀). § NX 포인트 생성자 스타일 확장(2026-09-13) - "POINT도 교차점/끝점/
// 시작점/중앙점으로 하이라이트되게 해달라"는 요청에 따라 NX Point Constructor의 주요
// 스냅 종류를 따로 구분했다(예전엔 edge의 양 끝을 구분 없이 Vertex 하나로 합쳤었음).
enum class PointSubKind {
    None,
    Vertex,       // (레거시 호환용) 예전 저장 데이터의 "시작/끝 구분 없는 꼭짓점" 표기
    StartPoint,   // 모서리의 시작점 - curve->Value(first)
    EndPoint,     // 모서리의 끝점 - curve->Value(last)
    EdgeMidpoint, // 모서리 위의 중간점(파라미터 50%)
    Center,       // 원/호 모서리의 실제 중심(곡선 위의 점이 아니라 원의 중심 자체)
    Intersection, // 두 모서리가 실제로 만나거나 매우 가깝게 교차하는 점(근사 탐색)
};

struct PickResult {
    PickedFaceKind kind = PickedFaceKind::None;
    Vec3 point;           // 광선이 면과 만난 지점(원통이면 축 위 가장 가까운 점 - AnchorCandidate.position과 동일 기준)
    Vec3 normal;          // Plane일 때만 의미 있음
    double diameterMm = 0.0; // Cylinder일 때만 의미 있음
    PointSubKind pointSubKind = PointSubKind::None; // Point일 때만 의미 있음

    // § 하이라이트 미리보기(2026-09-13) - "면/포인트/EDGE 근처에 가면 그 패턴을 통째로
    // 주황색 하이라이트해달라"는 요청(예전엔 뭘 가리키든 작은 점 하나만 찍혔음). kind가
    // Cylinder/Plane(면)이면 그 면 전체의 삼각형(월드좌표, 3개씩 짝)이 highlightTriangles에,
    // pointSubKind가 EdgeMidpoint(모서리 스냅)면 그 모서리 전체의 선분(월드좌표, 2개씩
    // 짝)이 highlightEdgeSegments에 채워진다. Vertex 스냅(꼭짓점은 여러 모서리가 만나는
    // 자리라 "면"에 대응하는 개념)이나 지원 안 하는 면 타입은 둘 다 비어있어(기존처럼
    // 점만 하이라이트) 호출자가 빈 벡터를 "이 종류는 확장 하이라이트 없음"으로 다루면 된다.
    std::vector<Vec3> highlightTriangles;
    std::vector<Vec3> highlightEdgeSegments;
};

// § 형상 프리셋(2026-09-18) - FindPatchCandidates 결과를 채택할지 판정하는 공용 임계값.
// RuleEngine(자동 적용 단계)과 UI(프리셋 등록/검색 미리보기)가 같은 값을 써야 "등록할 때
// 보이던 후보 개수"와 "실제 측정에 쓰이는 후보 개수"가 어긋나지 않는다. 초기값 - 실제
// Hook/Flange 표본으로 튜닝 필요(지름 필터의 kDiameterMatchToleranceMm 도입 때와 같은 패턴).
constexpr double kPatchSimilarityThreshold = 0.75;

// Hook/Flange류처럼 원통/평면 판별 하나로는 못 잡는 복합 형상을, 부품 이름도 없는 상태
// (회사 STEP은 전부 UNITE된 단일 ITEM, 사용자 확인)에서 "생김새"로 식별하기 위한 지문.
// 실제 계산(면 인접 확장 + OCCT 지오메트리 질의)은 geometry::FeaturePatch(FeaturePatch.h,
// StepGeometryAdapter.cpp 내부 전용)가 담당하고, 이 구조체 자체는 OCCT 타입을 전혀 안
// 써서 공개 인터페이스에 그대로 노출한다 - DB에도 그대로 직렬화해서 저장(shape_presets).
struct FeaturePatchDescriptor {
    // GeomAbs_SurfaceType(Plane=0, Cylinder=1, Cone=2, Sphere=3, Torus=4,
    // BezierSurface=5, BSplineSurface=6, SurfaceOfRevolution=7, SurfaceOfExtrusion=8,
    // OffsetSurface=9, OtherSurface=10) 별 면 개수.
    std::array<int, 11> surfaceTypeCounts{};
    int faceCount = 0;
    // 패치 로컬 bbox 축별 길이를 최댓값 기준 정규화(0~1) - 스케일 무관, 형상 비율만 남김.
    double bboxRatioX = 1.0;
    double bboxRatioY = 1.0;
    double bboxRatioZ = 1.0;
    // 패치 외곽 경계 총 길이 / bbox 대각선.
    double boundaryLengthRatio = 0.0;
    // 원통/원뿔/토러스 면 중 가장 큰 반지름 / bbox 대각선(없으면 0).
    double dominantRadiusRatio = 0.0;
};

// 뷰어에서 클릭 한 번으로 형상을 "등록"한 결과 - 지문 + 대표 위치(면적 가중 중심).
struct FeaturePatchCapture {
    FeaturePatchDescriptor descriptor;
    Vec3 position;
    bool valid = false; // clickPoint 근처에서 형상을 못 찾으면 false
};

// FindPatchCandidates 결과 하나 - AnchorCandidate와 같은 성격(좌표 + 이번엔 유사도 점수).
struct PatchCandidate {
    Vec3 position;
    double similarity = 0.0;
};

using ModelHandle = int;
constexpr ModelHandle kInvalidModelHandle = -1;

// Core/Viewer는 이 인터페이스만 알고, 실제 구현(Mock 또는 NX/JT)은 모른다.
// 개인PC: MockGeometryAdapter, 회사PC: 실제 JT/NX 연동 어댑터로 교체.
class IGeometryAdapter {
public:
    virtual ~IGeometryAdapter() = default;

    virtual ModelHandle LoadModel(const std::string& filePath) = 0;
    virtual BoundingBox GetBoundingBox(ModelHandle handle) const = 0;
    virtual std::vector<Vec3> GetVertices(ModelHandle handle) const = 0;

    // anchorType + partName으로 후보를 찾는다. Selector가 이 중에서 골라낸다.
    virtual std::vector<AnchorCandidate> FindAnchorCandidates(
        ModelHandle handle, const std::string& anchorType, const std::string& partName) const = 0;

    virtual std::vector<PlaneCandidate> FindPlaneCandidates(
        ModelHandle handle, const std::string& planeType, const std::string& partName) const = 0;

    virtual std::vector<FaceCandidate> FindFaceCandidates(
        ModelHandle handle, const std::string& faceType, const std::string& partName) const = 0;

    // § 3D 클릭 피킹 - 월드좌표 광선(origin+dir)과 형상의 실제 면 사이 정확한 교차를 구해
    // 어떤 면인지 판별한다. 기본 구현은 "아무것도 못 찾음"을 반환 - 실 형상 B-rep이 없는
    // 어댑터(Mock, 미구현 NxJt)는 이 자체가 의미 없어서 override를 강제하지 않는다.
    // StepGeometryAdapter만 OCCT로 override.
    virtual PickResult PickFace(ModelHandle handle, const Vec3& rayOrigin, const Vec3& rayDir) const {
        (void)handle;
        (void)rayOrigin;
        (void)rayDir;
        return PickResult{};
    }

    // § 형상 프리셋 - clickPoint 근처의 면에서 시작해 인접 면으로 확장한 패치의 지문을
    // 계산한다. 실 형상 B-rep이 없는 어댑터(Mock, 미구현 NxJt)는 override 불필요.
    virtual FeaturePatchCapture CaptureFeaturePatch(ModelHandle handle, const Vec3& clickPoint) const {
        (void)handle;
        (void)clickPoint;
        return FeaturePatchCapture{};
    }

    // descriptor와 비슷한 패치를 모델 전체에서 찾는다(같은 모델 내 "나머지 찾기"와 다른
    // 인치 모델 검색에 공용 - 호출 쪽이 결과 중 원본과 가까운 위치를 제외하면 된다).
    virtual std::vector<PatchCandidate> FindPatchCandidates(
        ModelHandle handle, const FeaturePatchDescriptor& descriptor, double similarityThreshold) const {
        (void)handle;
        (void)descriptor;
        (void)similarityThreshold;
        return {};
    }

    // 뷰어가 실제 렌더링에 쓰는 삼각형 메시. 연속된 3개 Vec3가 삼각형 1개(flat 셰이딩,
    // 별도 법선 데이터 없이 화면공간 미분(dFdx/dFdy)으로 면 법선을 계산). 실 형상
    // 테셀레이션이 없는 어댑터(Mock, 미구현 NxJt)를 위해 바운딩박스 12삼각형 상자로
    // 기본 구현을 제공한다 — StepGeometryAdapter는 OCCT 테셀레이션 결과로 override.
    virtual std::vector<Vec3> GetRenderTriangles(ModelHandle handle) const {
        const BoundingBox box = GetBoundingBox(handle);
        const Vec3& lo = box.min;
        const Vec3& hi = box.max;
        const Vec3 corners[8] = {
            {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
            {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
        };
        constexpr int kFaces[6][4] = {
            {0, 1, 2, 3}, {4, 5, 6, 7}, // 아래/윗면
            {0, 1, 5, 4}, {2, 3, 7, 6}, // 앞/뒷면
            {1, 2, 6, 5}, {3, 0, 4, 7}, // 옆면 2개
        };
        std::vector<Vec3> triangles;
        triangles.reserve(6 * 6);
        for (const auto& face : kFaces) {
            triangles.push_back(corners[face[0]]);
            triangles.push_back(corners[face[1]]);
            triangles.push_back(corners[face[2]]);
            triangles.push_back(corners[face[0]]);
            triangles.push_back(corners[face[2]]);
            triangles.push_back(corners[face[3]]);
        }
        return triangles;
    }

    // § 불필요한 선 정리(2026-09-11) - "3D 도면에 불필요한 선들이 오버레이된다"는 피드백.
    // 예전엔 뷰어가 GetRenderTriangles()의 삼각형 외곽선을 그대로 와이어프레임으로
    // 그렸는데, 사각형 면을 둘로 쪼갠 대각선까지 전부 "엣지"로 보여서 실제 형상에 없는
    // 선이 잔뜩 보였다. 이제 뷰어는 이 함수(실제 B-rep 엣지만)로 엣지 오버레이를 그린다 -
    // 연속된 2개 Vec3가 선분 1개(GL_LINES). 실 형상이 없는 어댑터는 바운딩박스
    // 12모서리로 기본 구현.
    virtual std::vector<Vec3> GetRenderEdges(ModelHandle handle) const {
        const BoundingBox box = GetBoundingBox(handle);
        const Vec3& lo = box.min;
        const Vec3& hi = box.max;
        const Vec3 corners[8] = {
            {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
            {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
        };
        constexpr int kEdges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0}, // 아랫면
            {4, 5}, {5, 6}, {6, 7}, {7, 4}, // 윗면
            {0, 4}, {1, 5}, {2, 6}, {3, 7}, // 수직 기둥
        };
        std::vector<Vec3> segments;
        segments.reserve(12 * 2);
        for (const auto& edge : kEdges) {
            segments.push_back(corners[edge[0]]);
            segments.push_back(corners[edge[1]]);
        }
        return segments;
    }
};

} // namespace geometry
