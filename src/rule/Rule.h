#pragma once

#include <optional>
#include <string>
#include <vector>

namespace rule {

// 규칙에 등장하는 기준 지오메트리 하나. role은 "A"/"B"(두 anchor 비교) 또는
// "single"(point_to_plane처럼 anchor가 하나뿐인 경우)로 구분한다.
struct Anchor {
    std::string role;
    std::string anchorType; // Hole, Boss_Center, Hook_Tip_Edge, Face, ...
    std::string partName;   // Bezel, Rear_Chassis, ...
    std::optional<std::string> paramKey;   // 예: "diameter"
    std::optional<double> paramValue;      // 예: 2.8
};

enum class MeasurementType {
    PointToPoint,
    PointToPlane,
    AxisProjection,
    FaceToFaceGap
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
};

} // namespace rule
