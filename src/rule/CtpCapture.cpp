#include "rule/CtpCapture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace rule {

namespace {

// 치수선 간격 = 형상 크기(화면 평면에서 큰 쪽)의 이 비율. 라벨 원이 이웃 치수선과 겹치지 않을 만큼 띄운다.
constexpr double kDimensionGapRatio = 0.22;
constexpr double kMinDimensionGapMm = 0.8;
// 형상 + 치수선 범위 바깥 여백(각 변, 범위 크기 대비).
constexpr double kFrameMarginRatio = 0.12;
// 법선이 시선 축과 이 값(코사인)보다 작으면 "시선과 평행한 옆면"으로 센다(약 80도 이상).
constexpr double kSideFaceMaxCos = 0.17;

double Get(const geometry::Vec3& v, int axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

void Set(geometry::Vec3& v, int axis, double value) {
    (axis == 0 ? v.x : (axis == 1 ? v.y : v.z)) = value;
}

double Extent(const geometry::BoundingBox& box, int axis) {
    return Get(box.max, axis) - Get(box.min, axis);
}

double Mid(const geometry::BoundingBox& box, int axis) {
    return (Get(box.min, axis) + Get(box.max, axis)) * 0.5;
}

// 시선 축 k에서 형상 단면 윤곽이 얼마나 잘 보이는지 - 시선과 평행한 평면(옆면)의 넓이 합.
// 압출형 형상(후크/리브)은 압출 방향으로 볼 때 옆면이 전부 시선과 평행해서 이 값이 가장 크다.
double ProfileScore(const std::vector<geometry::PlaneCandidate>& planes, int k) {
    double score = 0.0;
    for (const auto& plane : planes) {
        const auto& n = plane.normal;
        const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len < 1e-12 || !plane.hasFaceBox) {
            continue;
        }
        if (std::abs(Get(n, k)) / len >= kSideFaceMaxCos) {
            continue;
        }
        // 면 넓이 근사: 면 범위에서 법선과 가장 가까운 축을 뺀 나머지 두 변의 곱.
        int normalAxis = 0;
        for (int a = 1; a < 3; ++a) {
            if (std::abs(Get(n, a)) > std::abs(Get(n, normalAxis))) {
                normalAxis = a;
            }
        }
        double area = 1.0;
        for (int a = 0; a < 3; ++a) {
            if (a != normalAxis) {
                area *= std::max(0.0, Extent(plane.faceBox, a));
            }
        }
        score += area;
    }
    return score;
}

// (r, u, v)가 (0,1,2)의 순환 순서면 e_r x e_u = +e_v.
double CrossSign(int r, int u) {
    return ((u - r + 3) % 3 == 1) ? 1.0 : -1.0;
}

struct Bounds2D {
    double minR = std::numeric_limits<double>::max();
    double maxR = std::numeric_limits<double>::lowest();
    double minU = std::numeric_limits<double>::max();
    double maxU = std::numeric_limits<double>::lowest();
    void Add(const geometry::Vec3& p, int r, int u) {
        minR = std::min(minR, Get(p, r));
        maxR = std::max(maxR, Get(p, r));
        minU = std::min(minU, Get(p, u));
        maxU = std::max(maxU, Get(p, u));
    }
};

// 시선/화면 축이 정해졌을 때 치수선을 여백으로 빼고 화면 범위를 정한다.
CtpCapturePlan Layout(const std::vector<CtpDimensionInput>& dims, const geometry::BoundingBox& featureBox,
                      int viewAxis, int rightAxis, int upAxis, double aspect) {
    CtpCapturePlan plan;
    CtpCaptureView& view = plan.view;
    view.viewAxis = viewAxis;
    view.viewSign = -1.0;
    view.upAxis = upAxis;
    view.upSign = 1.0;
    view.rightAxis = rightAxis;
    // (rightSign*e_r) x (e_u) = -viewDir 이어야 오른손 좌표계(카메라가 -z를 본다).
    view.rightSign = -view.viewSign * CrossSign(rightAxis, upAxis);
    view.sectionEnabled = true;
    view.sectionAxis = viewAxis;
    view.sectionCoord = Mid(featureBox, viewAxis);
    view.sectionSign = view.viewSign;

    const double featureSize = std::max({Extent(featureBox, rightAxis), Extent(featureBox, upAxis), 1.0});
    const double gap = std::max(featureSize * kDimensionGapRatio, kMinDimensionGapMm);
    const double depth = Mid(featureBox, viewAxis);

    // 가로(화면 오른쪽 축) 치수는 위로, 세로(화면 위쪽 축) 치수는 화면 오른쪽으로 뺀다. 짧은 치수가 안쪽.
    std::vector<size_t> horizontal;
    std::vector<size_t> vertical;
    plan.dims.resize(dims.size());
    for (size_t i = 0; i < dims.size(); ++i) {
        const auto& in = dims[i];
        auto& out = plan.dims[i];
        out.label = in.label;
        out.axis = in.axis;
        out.measureA = in.measureA;
        out.measureB = in.measureB;
        out.valueMm = in.valueMm;
        out.visible = in.axis != viewAxis;
        if (!out.visible) {
            if (!plan.warning.empty()) {
                plan.warning += ", ";
            }
            plan.warning += in.label;
            continue;
        }
        (in.axis == rightAxis ? horizontal : vertical).push_back(i);
    }
    if (!plan.warning.empty()) {
        plan.warning = "시선 방향과 같은 축이라 이 이미지에 그릴 수 없는 치수: " + plan.warning +
                       " - 다른 페이지(다른 시선)로 나눠 주세요.";
    }
    auto length = [&](size_t i) { return std::abs(Get(dims[i].measureB, dims[i].axis) - Get(dims[i].measureA, dims[i].axis)); };
    auto byLength = [&](size_t a, size_t b) { return length(a) < length(b); };
    std::stable_sort(horizontal.begin(), horizontal.end(), byLength);
    std::stable_sort(vertical.begin(), vertical.end(), byLength);

    auto place = [&](const std::vector<size_t>& group, int offsetAxis, double edge, double direction) {
        for (size_t k = 0; k < group.size(); ++k) {
            auto& out = plan.dims[group[k]];
            const double offset = edge + direction * gap * static_cast<double>(k + 1);
            out.lineA = out.measureA;
            out.lineB = out.measureB;
            for (geometry::Vec3* p : {&out.lineA, &out.lineB}) {
                Set(*p, offsetAxis, offset);
                Set(*p, viewAxis, depth);
            }
            out.extA = out.measureA;
            out.extB = out.measureB;
            const auto& in = dims[group[k]];
            if (in.hasFaceBoxes) {
                auto clampToFace = [&](geometry::Vec3& p, const geometry::BoundingBox& face) {
                    Set(p, offsetAxis, std::clamp(offset, Get(face.min, offsetAxis), Get(face.max, offsetAxis)));
                };
                clampToFace(out.extA, in.faceBoxA);
                clampToFace(out.extB, in.faceBoxB);
            }
        }
    };
    // 화면 위쪽 = +upAxis (upSign=+1) -> 형상 범위의 max 쪽.
    place(horizontal, upAxis, Get(featureBox.max, upAxis), 1.0);
    // 화면 오른쪽 = rightSign 방향.
    place(vertical, rightAxis, view.rightSign > 0 ? Get(featureBox.max, rightAxis) : Get(featureBox.min, rightAxis),
          view.rightSign);

    Bounds2D bounds;
    bounds.Add(featureBox.min, rightAxis, upAxis);
    bounds.Add(featureBox.max, rightAxis, upAxis);
    for (const auto& d : plan.dims) {
        if (!d.visible) {
            continue;
        }
        for (const auto& p : {d.extA, d.extB, d.lineA, d.lineB}) {
            bounds.Add(p, rightAxis, upAxis);
        }
    }
    // 바깥쪽 치수선의 라벨 원이 잘리지 않게 치수선 간격 절반만큼 더 둔다.
    const double labelPad = gap * 0.5;
    if (!horizontal.empty()) {
        bounds.maxU += labelPad;
    }
    if (!vertical.empty()) {
        (view.rightSign > 0 ? bounds.maxR : bounds.minR) += view.rightSign * labelPad;
    }
    const double width = std::max(bounds.maxR - bounds.minR, 1e-3);
    const double height = std::max(bounds.maxU - bounds.minU, 1e-3);
    double halfW = width * (0.5 + kFrameMarginRatio);
    double halfH = height * (0.5 + kFrameMarginRatio);
    const double safeAspect = aspect > 1e-6 ? aspect : 1.0;
    if (halfW / halfH < safeAspect) {
        halfW = halfH * safeAspect;
    } else {
        halfH = halfW / safeAspect;
    }
    view.halfWidth = halfW;
    view.halfHeight = halfH;
    Set(view.center, rightAxis, (bounds.minR + bounds.maxR) * 0.5);
    Set(view.center, upAxis, (bounds.minU + bounds.maxU) * 0.5);
    Set(view.center, viewAxis, depth);
    plan.ok = true;
    return plan;
}

} // namespace

CtpDimensionInput MakeCtpDimension(const PresetDimensionCandidate& candidate, const std::string& label) {
    CtpDimensionInput d;
    d.measureA = candidate.refA;
    d.measureB = candidate.refB;
    d.axis = candidate.axis;
    d.label = label;
    d.valueMm = candidate.valueMm;
    d.hasFaceBoxes = candidate.faceA.hasFaceBox && candidate.faceB.hasFaceBox;
    d.faceBoxA = candidate.faceA.faceBox;
    d.faceBoxB = candidate.faceB.faceBox;
    return d;
}

double CtpImageAspect(int dimensionsOnPage, int inchCount) {
    // 양식 v1 높이(pt): 페이지 768 = 제목·결재 54 + 5 + 정보 칸(22x2 + 15 x max(인치,3)) + 5
    //                    + 이미지 머리 16 + 5 + 이미지 칸 + 표 머리 16 + 30 + 15 x (치수 x 인치) + 5 + 측정 환경 16 + 52.
    // 이미지는 칸 안쪽 여백 8pt를 두고 들어간다(너비 536 - 16).
    const int inches = std::max(1, inchCount);
    const int dims = std::max(1, dimensionsOnPage);
    const double info = 44.0 + 15.0 * std::max(inches, 3);
    const double above = 54.0 + 5.0 + info + 5.0 + 16.0 + 5.0;
    const double below = 16.0 + 30.0 + 15.0 * dims * inches + 5.0 + 16.0 + 52.0;
    const double cellHeight = std::max(120.0, 768.0 - above - below);
    return (536.0 - 16.0) / (cellHeight - 16.0);
}

CtpCapturePlan PlanCtpCapture(const std::vector<CtpDimensionInput>& dims, const geometry::BoundingBox& featureBox,
                              const std::vector<geometry::PlaneCandidate>& featurePlanes, double aspect) {
    if (dims.empty()) {
        CtpCapturePlan empty;
        empty.warning = "그릴 치수가 없습니다.";
        return empty;
    }

    // 시선 축: (1) 시선 방향과 겹치는 치수가 가장 적고 (2) 옆면 넓이(단면 윤곽이 잘 보임)가 가장 큰 축.
    std::array<int, 3> dimsAlong{0, 0, 0};
    for (const auto& d : dims) {
        if (d.axis >= 0 && d.axis < 3) {
            ++dimsAlong[static_cast<size_t>(d.axis)];
        }
    }
    int viewAxis = 0;
    double bestScore = std::numeric_limits<double>::lowest();
    int bestHidden = std::numeric_limits<int>::max();
    for (int k = 0; k < 3; ++k) {
        const int hidden = dimsAlong[static_cast<size_t>(k)];
        // 옆면 정보가 없으면 형상이 가장 긴 축(압출 방향일 가능성이 큼)을 고른다.
        const double score = featurePlanes.empty() ? Extent(featureBox, k) : ProfileScore(featurePlanes, k);
        if (hidden < bestHidden || (hidden == bestHidden && score > bestScore)) {
            bestHidden = hidden;
            bestScore = score;
            viewAxis = k;
        }
    }

    // 화면 위쪽 축: 두 가지 배치 중 내용 비율이 출력 이미지 비율에 더 가까운 쪽(동률이면 Z, 그다음 Y를 위로).
    const int a = (viewAxis + 1) % 3;
    const int b = (viewAxis + 2) % 3;
    const int upFirst = std::max(a, b); // Z > Y > X 순으로 위쪽 선호
    const int upSecond = std::min(a, b);
    CtpCapturePlan p1 = Layout(dims, featureBox, viewAxis, upSecond, upFirst, aspect);
    CtpCapturePlan p2 = Layout(dims, featureBox, viewAxis, upFirst, upSecond, aspect);
    // 같은 출력 비율로 맞춘 뒤의 화면 크기가 작을수록 형상이 크게 보인다.
    return (p2.view.halfHeight < p1.view.halfHeight * 0.98) ? p2 : p1;
}

} // namespace rule
