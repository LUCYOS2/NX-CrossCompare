#include "rule/PresetDimensions.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace rule {

namespace {

// 이 안의 좌표/구간 차이는 같은 자리로 본다(같은 평면 위에 나뉜 면 등).
constexpr double kSameCoordinateToleranceMm = 0.05;
// 외곽 끝면은 모델 범위 끝에서 이만큼 안(모델 크기 비례, 최소 0.2mm).
constexpr double kOutlineEndToleranceMinMm = 0.2;
constexpr double kOutlineEndToleranceRatio = 0.002;

double Component(const geometry::Vec3& v, int axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

void SetComponent(geometry::Vec3& v, int axis, double value) {
    (axis == 0 ? v.x : (axis == 1 ? v.y : v.z)) = value;
}

double Length(const geometry::Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// 법선이 축과 나란하면 그 축 성분의 부호(+1/-1), 아니면 0.
int AxisSign(const geometry::PlaneCandidate& plane, int axis) {
    const double len = Length(plane.normal);
    if (len < 1e-12) {
        return 0;
    }
    const double c = Component(plane.normal, axis) / len;
    const double cosTol = std::cos(kPresetFaceAxisToleranceDeg * 3.14159265358979323846 / 180.0);
    if (c >= cosTol) return 1;
    if (c <= -cosTol) return -1;
    return 0;
}

double Coordinate(const geometry::PlaneCandidate& plane, int axis) {
    return Component(plane.pointOnPlane, axis);
}

// 축에 수직인 면의 넓이 근사(나머지 두 축 범위의 곱) - 같은 자리에 나뉜 끝면이 여럿이면 넓은 쪽.
double CrossArea(const geometry::PlaneCandidate& plane, int axis) {
    if (!plane.hasFaceBox) {
        return 0.0;
    }
    double area = 1.0;
    for (int other = 0; other < 3; ++other) {
        if (other != axis) {
            area *= Component(plane.faceBox.max, other) - Component(plane.faceBox.min, other);
        }
    }
    return area;
}

geometry::Vec3 BoxCenter(const geometry::BoundingBox& box) {
    return geometry::Vec3{(box.min.x + box.max.x) * 0.5, (box.min.y + box.max.y) * 0.5, (box.min.z + box.max.z) * 0.5};
}

// near를 면 범위 안으로 당기고 축 좌표는 그 면의 좌표로 - 규칙에 저장될 "그 면 위의 형상 가까운 점".
geometry::Vec3 PointOnFaceNear(const geometry::PlaneCandidate& plane, int axis, const geometry::Vec3& near) {
    geometry::Vec3 p = near;
    if (plane.hasFaceBox) {
        p.x = std::clamp(p.x, plane.faceBox.min.x, plane.faceBox.max.x);
        p.y = std::clamp(p.y, plane.faceBox.min.y, plane.faceBox.max.y);
        p.z = std::clamp(p.z, plane.faceBox.min.z, plane.faceBox.max.z);
    }
    SetComponent(p, axis, Coordinate(plane, axis));
    return p;
}

// 형상의 한쪽(atMax) 끝면 - 그쪽 바깥을 향한 축 수직 평면 중 가장 바깥 것(같은 자리에 나뉜 면이면 넓은 것).
// 패치 범위 끝과 꼭 일치하지 않아도 된다 - 실제 스냅/후크는 뿌리 필렛(곡면)이 평면 끝면보다 조금 더
// 나와 있는 경우가 많다(샘플 STEP 확인, 2026-11-05).
std::optional<geometry::PlaneCandidate> OutermostFace(const geometry::PatchCandidate& shape, int axis, bool atMax) {
    const int outwardSign = atMax ? 1 : -1;
    const geometry::PlaneCandidate* best = nullptr;
    for (const auto& plane : shape.planes) {
        if (AxisSign(plane, axis) != outwardSign) {
            continue;
        }
        if (!best) {
            best = &plane;
            continue;
        }
        const double c = Coordinate(plane, axis);
        const double bestC = Coordinate(*best, axis);
        if (std::abs(c - bestC) <= kSameCoordinateToleranceMm) {
            if (CrossArea(plane, axis) > CrossArea(*best, axis)) {
                best = &plane;
            }
        } else if (atMax ? c > bestC : c < bestC) {
            best = &plane;
        }
    }
    if (!best) {
        return std::nullopt;
    }
    return *best;
}

// 축 방향에서 봤을 때 두 면 범위가 겹치는 넓이(나머지 두 축 구간 겹침의 곱, 안 겹치면 0) - 버니어 두 날이
// 두 면을 동시에 물 수 있는 자리가 있는지.
double FootprintOverlap(const geometry::PlaneCandidate& a, const geometry::PlaneCandidate& b, int axis) {
    if (!a.hasFaceBox || !b.hasFaceBox) {
        return 0.0;
    }
    double area = 1.0;
    for (int other = 0; other < 3; ++other) {
        if (other == axis) {
            continue;
        }
        const double lo = std::max(Component(a.faceBox.min, other), Component(b.faceBox.min, other));
        const double hi = std::min(Component(a.faceBox.max, other), Component(b.faceBox.max, other));
        if (hi - lo <= kSameCoordinateToleranceMm) {
            return 0.0;
        }
        area *= hi - lo;
    }
    return area;
}

// 형상 크기 - -축을 향한 면 A와 +축을 향한 면 B(바깥 날로 무는 두 면) 중, 축 방향에서 겹치고 가장 멀리
// 떨어진 쌍. 버니어로 잰 형상 크기와 같은 값이다.
std::optional<std::pair<geometry::PlaneCandidate, geometry::PlaneCandidate>> ShapeSizeFaces(
    const geometry::PatchCandidate& shape, int axis) {
    const geometry::PlaneCandidate* bestA = nullptr;
    const geometry::PlaneCandidate* bestB = nullptr;
    double bestGap = 0.0;
    double bestOverlap = 0.0;
    for (const auto& a : shape.planes) {
        if (AxisSign(a, axis) != -1) {
            continue;
        }
        for (const auto& b : shape.planes) {
            if (AxisSign(b, axis) != 1) {
                continue;
            }
            const double gap = Coordinate(b, axis) - Coordinate(a, axis);
            if (gap < kPresetMinDimensionMm) {
                continue;
            }
            const double overlap = FootprintOverlap(a, b, axis);
            if (overlap <= 0.0) {
                continue;
            }
            const bool better = !bestA || gap > bestGap + kSameCoordinateToleranceMm ||
                                (std::abs(gap - bestGap) <= kSameCoordinateToleranceMm && overlap > bestOverlap);
            if (better) {
                bestA = &a;
                bestB = &b;
                bestGap = gap;
                bestOverlap = overlap;
            }
        }
    }
    if (!bestA) {
        return std::nullopt;
    }
    return std::make_pair(*bestA, *bestB);
}

// 나머지 두 축에서 점과 면 범위 사이 거리(축 방향은 무시) - 외곽 끝면이 여럿이면 형상 옆의 것.
double SideDistance(const geometry::PlaneCandidate& plane, int axis, const geometry::Vec3& point) {
    if (!plane.hasFaceBox) {
        return 0.0;
    }
    double sum = 0.0;
    for (int other = 0; other < 3; ++other) {
        if (other == axis) {
            continue;
        }
        const double v = Component(point, other);
        const double d = std::max({Component(plane.faceBox.min, other) - v, 0.0, v - Component(plane.faceBox.max, other)});
        sum += d * d;
    }
    return std::sqrt(sum);
}

// 모델 외곽 한쪽(atMax)의 끝면 - 바깥을 향하고 모델 범위 끝에 있는 축 수직 평면 중 형상 옆의 것.
std::optional<geometry::PlaneCandidate> OutlineEndFace(const geometry::BoundingBox& modelBox,
                                                       const std::vector<geometry::PlaneCandidate>& modelPlanes,
                                                       int axis, bool atMax, const geometry::Vec3& shapeCenter) {
    const double lo = Component(modelBox.min, axis);
    const double hi = Component(modelBox.max, axis);
    const double tol = std::max(kOutlineEndToleranceMinMm, (hi - lo) * kOutlineEndToleranceRatio);
    const geometry::PlaneCandidate* best = nullptr;
    double bestDistance = 0.0;
    for (const auto& plane : modelPlanes) {
        if (AxisSign(plane, axis) != (atMax ? 1 : -1)) {
            continue;
        }
        const double c = Coordinate(plane, axis);
        if (std::abs(c - (atMax ? hi : lo)) > tol) {
            continue;
        }
        const double d = SideDistance(plane, axis, shapeCenter);
        if (!best || d < bestDistance) {
            best = &plane;
            bestDistance = d;
        }
    }
    if (!best) {
        return std::nullopt;
    }
    return *best;
}

} // namespace

std::string AxisName(int axis) {
    return axis == 0 ? "X" : (axis == 1 ? "Y" : "Z");
}

std::vector<PresetDimensionCandidate> ComputePresetDimensionCandidates(
    const geometry::PatchCandidate& shape, const geometry::BoundingBox& modelBox,
    const std::vector<geometry::PlaneCandidate>& modelPlanes) {
    std::vector<PresetDimensionCandidate> result;
    if (!shape.hasBox) {
        return result;
    }
    const geometry::Vec3 center = BoxCenter(shape.box);

    for (int axis = 0; axis < 3; ++axis) {
        const auto faces = ShapeSizeFaces(shape, axis);
        if (!faces) {
            continue;
        }
        PresetDimensionCandidate c;
        c.kind = PresetDimensionKind::ShapeSize;
        c.axis = axis;
        c.faceA = faces->first;
        c.faceB = faces->second;
        c.valueMm = Coordinate(c.faceB, axis) - Coordinate(c.faceA, axis);
        c.refA = PointOnFaceNear(c.faceA, axis, center);
        c.refB = PointOnFaceNear(c.faceB, axis, center);
        result.push_back(std::move(c));
    }

    for (int axis = 0; axis < 3; ++axis) {
        // 가까운 쪽 외곽부터 - 그쪽에 외곽 끝면(평면)이 없으면 반대쪽.
        const double gapToMin = Component(shape.box.min, axis) - Component(modelBox.min, axis);
        const double gapToMax = Component(modelBox.max, axis) - Component(shape.box.max, axis);
        const bool sides[2] = {gapToMax < gapToMin, !(gapToMax < gapToMin)};
        for (const bool atMax : sides) {
            const auto shapeFace = OutermostFace(shape, axis, atMax);
            if (!shapeFace) {
                continue;
            }
            const auto outline = OutlineEndFace(modelBox, modelPlanes, axis, atMax, center);
            if (!outline) {
                continue;
            }
            PresetDimensionCandidate c;
            c.kind = PresetDimensionKind::FromOutline;
            c.axis = axis;
            c.outlineAtMax = atMax;
            c.faceA = *outline;
            c.faceB = *shapeFace;
            c.valueMm = std::abs(Coordinate(c.faceB, axis) - Coordinate(c.faceA, axis));
            if (c.valueMm < kPresetMinDimensionMm) {
                continue;
            }
            c.refA = PointOnFaceNear(c.faceA, axis, center);
            c.refB = PointOnFaceNear(c.faceB, axis, center);
            result.push_back(std::move(c));
            break;
        }
    }
    return result;
}

std::string DefaultPresetDimensionName(const std::string& presetName, const PresetDimensionCandidate& candidate) {
    const std::string axis = AxisName(candidate.axis);
    if (candidate.kind == PresetDimensionKind::ShapeSize) {
        return presetName + " 크기 " + axis;
    }
    return presetName + " 위치 " + axis + " (" + (candidate.outlineAtMax ? "+" : "-") + axis + " 외곽 기준)";
}

std::string DescribePresetDimension(const PresetDimensionCandidate& candidate) {
    const std::string axis = AxisName(candidate.axis);
    if (candidate.kind == PresetDimensionKind::ShapeSize) {
        return "형상의 " + axis + "방향 양 끝면 사이";
    }
    const std::string side = std::string(candidate.outlineAtMax ? "+" : "-") + axis;
    return "제품 " + side + "쪽 외곽 끝면 → 형상의 " + side + "쪽 끝면";
}

Rule MakePresetDimensionRule(const PresetDimensionCandidate& candidate, const std::string& name,
                             const geometry::BoundingBox& sourceBox) {
    Rule r;
    r.name = name;
    r.measurementType = MeasurementType::FaceToFaceGap;
    r.referenceFrame = {"World_Origin"};
    r.projection = AxisName(candidate.axis);
    r.checkPointCategory = "Dimension";
    Anchor a{"A", "Datum_Plane", "", std::nullopt, std::nullopt};
    a.ref = RefGeometry{candidate.refA, candidate.faceA.normal, sourceBox};
    Anchor b{"B", "Datum_Plane", "", std::nullopt, std::nullopt};
    b.ref = RefGeometry{candidate.refB, candidate.faceB.normal, sourceBox};
    r.anchors = {a, b};
    return r;
}

} // namespace rule
