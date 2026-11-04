#pragma once

#include "geometry/IGeometryAdapter.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rule {

// § 클릭 기준 좌표(2026-10-30) - "클릭한 위치가 규칙에 저장되지 않는다" 문제 해결. 예전엔
// anchor_type/지름 같은 조건만 저장해서, 조건에 맞는 후보가 여럿이면 어느 것인지 알 수
// 없었다(임의 선택 또는 ambiguous 실패). 클릭한 월드 좌표와 그때 모델의 바운딩박스를 같이
// 저장해두면, 다른 인치 모델에서도 "그 자리"를 예측해 가장 가까운 후보를 고를 수 있다
// (rule::PredictPosition - 크기가 다른 인치 모델은 같은 좌표가 아니므로 bbox 기준으로 옮김).
struct RefGeometry {
    geometry::Vec3 position;
    // 평면을 클릭했을 때만 - 면 방향까지 반영한 바깥쪽 법선(판재 앞/뒷면 구분용).
    std::optional<geometry::Vec3> normal;
    geometry::BoundingBox sourceBox;
};

// 규칙에 등장하는 기준 지오메트리 하나. role은 "A"/"B"(두 anchor 비교) 또는
// "single"(point_to_plane처럼 anchor가 하나뿐인 경우)로 구분한다.
struct Anchor {
    std::string role;
    std::string anchorType; // Hole, Boss_Center, Hook_Tip_Edge, Face, "preset:<이름>", ...
    std::string partName;   // Bezel, Rear_Chassis, ...
    std::optional<std::string> paramKey;   // 예: "diameter"
    std::optional<double> paramValue;      // 예: 2.8
    // § 축 방향 필터(2026-09-09) - paramKey/paramValue(지름)와 별개 필드라 동시에(AND)
    // 적용 가능하다. "X"/"Y"/"Z" 중 하나, 비어있으면 필터 안 씀. 원통 축이 이 축과
    // directionToleranceDeg(기본 5도) 이내로 평행한 후보만 남긴다.
    std::optional<std::string> directionAxis;
    std::optional<double> directionToleranceDeg;
    // § 형상 프리셋(2026-09-18) - anchorType이 "preset:<이름>"이면 채워진다. 값이 있으면
    // RuleEngine이 FindAnchorCandidates 대신 adapter.FindPatchCandidates로 이 지문과
    // 비슷한 형상을 찾고, 유사도 최고점을 자동 선택한다(rule::SelectBestPatchMatch) -
    // Hole/Boss_Center처럼 anchorType 문자열 자체가 아니라 등록 시점에 캡처한 지문이
    // 필요해서 별도 필드로 들고 다닌다.
    std::optional<geometry::FeaturePatchDescriptor> patchDescriptor;
    // 클릭으로 만든 anchor면 채워진다 - 있으면 selector(leftmost/nearest_pair 등) 대신 이
    // 좌표 기준으로 후보를 고른다(RuleEngine). 예전에 저장된 규칙은 nullopt.
    std::optional<RefGeometry> ref;
};

enum class MeasurementType {
    PointToPoint,
    PointToPlane,
    AxisProjection,
    FaceToFaceGap,
    // 전장 사이즈(X/Y/Z) - anchor/selector 탐색 없이 BoundingBox에서 직접 계산하는
    // "기본 세팅" 항목. 다른 측정 타입과 성격이 달라 anchors가 비어있어도 된다.
    OverallSize,
    // 형상 개수 - anchor 1개(role="single")로 찾은 후보 개수를 그대로 값으로 쓴다.
    // 예: 구멍이 몇 개인지. Anchor.paramKey=="diameter"를 지정하면 그 지름(±공차)에
    // 맞는 후보만 세므로 "비슷한 지름의 구멍만 카운트"도 가능하다.
    InstanceCount,
    // 최소 간격(Pitch) - anchor 1개(role="single")로 찾은 후보들을 projection 축
    // (X/Y/Z) 기준 정렬한 뒤, 인접한 후보끼리의 거리 중 최솟값을 값으로 쓴다.
    // 후보가 2개 미만이면 계산 불가(오류로 표시).
    MinPitch,
};

std::string ToString(MeasurementType type);
MeasurementType MeasurementTypeFromString(const std::string& s);

// 인치 하나 + role(A/B) 하나에 대한 좌표 샘플. 합성 데이터 생성과 DB 저장/조회 양쪽에서
// 공통으로 쓰는 최소 단위.
struct PointSample {
    int inch = 0;
    std::string role;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// point_to_plane 측정의 기준 평면 참조. anchors와 별개 필드로 둔 이유는 카탈로그
// 002(후크 높이)처럼 "anchor 1개 + 기준 평면 1개" 형태가 anchor 2개 비교와
// 성격이 달라서다 (개발계획_v2.md §6 참고).
struct PlaneRef {
    std::string planeType; // Datum_Plane 등
    std::string partName;
    // § 법선 방향 필터(2026-09-09) - Anchor.directionAxis와 같은 취지, 평면 후보가
    // 여럿일 때(§17 as1_pe.stp 실측 - Ø254mm 원통 58개처럼 평면도 후보가 많이 겹칠 수
    // 있음) 법선이 이 축과 평행한 것만 남긴다.
    std::optional<std::string> normalAxis;
    std::optional<double> normalToleranceDeg;
    // § 클릭 기준 좌표 - 기준 평면을 클릭한 위치/법선. 없으면 예전처럼 첫 평면 후보를 쓴다.
    std::optional<RefGeometry> ref;
};

// 개발계획_v2.md §7 Rule Schema. Anchor / Reference Frame / Selector / Measurement 4계층.
struct Rule {
    int id = 0;
    std::string name;
    std::vector<Anchor> anchors;
    std::optional<PlaneRef> referencePlane;  // measurementType == PointToPlane일 때만 사용
    std::vector<std::string> referenceFrame; // 우선순위 리스트, 1순위 = World_Origin (§5)
    std::vector<std::string> selector;       // 우선순위 리스트
    MeasurementType measurementType = MeasurementType::PointToPoint;
    std::string projection; // "3D" | "X" | "Y" | "Z" | "normal"
    double tolerancePlusMm = 0.0;
    double toleranceMinusMm = 0.0;
    // § 이미지 캡쳐 연동(2026-09-09) - CTQ 관리서처럼 규칙별 측정 포인트 그림을 남기기
    // 위해 3D 뷰 캡쳐본 파일 경로를 규칙에 붙여둔다. 없으면 nullopt(이미지 없음).
    std::optional<std::string> imagePath;
    // § CTQ 관리항목 코드(2026-09-11) - "치수 표시선 옆에 CTQ 관리항목 D 이런식으로 입력할
    // 수 있는 칸이 있으면 좋겠다"는 요청. CTQ 문서의 항목 기호(D, E-1 등)를 규칙에 붙여둔다.
    // 없으면 nullopt.
    std::optional<std::string> ctqCode;
    // § Check Point 분류(2026-09-11) - "Dimension/Height/Angle 등으로 분류하자"는 요청.
    // 포인트별이 아니라 규칙(측정 항목) 전체에 하나만 붙는다(사용자 확인 완료).
    std::optional<std::string> checkPointCategory;
    // § 도면 스펙(2026-10-30) - "합격/불합격 판정하려면 규칙에 도면 치수가 필요하고, 사이즈별로
    // 값이 다르면 사이즈별 값도 필요하다"는 요청. 인치 -> 도면 스펙(mm). "도면 스펙"과 "nominal"은
    // 같은 개념이다(사용자 확인, 2026-10-30) - 코드/화면/엑셀 모두 "도면 스펙(spec)"으로 통일한다.
    // 값이 있는 인치는 |측정값 - 도면 스펙|을 tolerancePlusMm/toleranceMinusMm로 판정하고, 없는
    // 인치는 예전처럼 "가장 작은 인치 대비 편차"로 비교만 한다(비교 모드 - RuleEngine 참고).
    std::map<int, double> specByInch;
};

} // namespace rule
