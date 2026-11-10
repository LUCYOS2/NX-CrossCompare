#pragma once

#include "geometry/IGeometryAdapter.h"
#include "rule/PresetDimensions.h"

#include <string>
#include <vector>

namespace rule {

// § CTP 캡처 이미지(2026-11-09) - "자동 치수 포인트 + 그 부위 도면 + 치수 지시선 이미지가 CTP 양식에 딱 들어가야
// 한다"는 요청. 한 페이지에 들어갈 치수(1~4개, 라벨 A~D)를 받아서, 사람이 뷰를 돌려 맞추던 일을 자동으로 한다:
//   - 시선: 측정축과 수직인 월드 축 중 형상 옆면(시선과 평행한 평면)이 가장 넓은 축 = 형상 단면 윤곽이 보이는 방향.
//     모든 치수에 수직인 축이 없으면 시선 방향 치수는 그릴 수 없다고 표시한다(hidden).
//   - 단면: 시선 축 방향으로 형상 중심을 자른다(카메라 쪽 절반 제거) - 다른 벽이 형상을 가리지 않고 단면 윤곽이 보인다.
//   - 치수선: 측정점은 그대로 두고 형상 밖 여백으로 뺀다(가로 치수는 위, 세로 치수는 오른쪽, 짧은 것부터 안쪽).
//   - 확대 범위: 형상 + 치수선이 다 들어오게, 출력 이미지 비율(CTP 양식의 이미지 칸)에 맞춘다.
// GL/Qt가 없는 순수 계산이라 단위 시험을 할 수 있다. 실제 그리기는 viewer::ModelViewport::RenderCtpImage.

struct CtpDimensionInput {
    geometry::Vec3 measureA; // 실제 측정점(측정면 위)
    geometry::Vec3 measureB;
    int axis = 0;            // 측정축 0=X, 1=Y, 2=Z
    std::string label;       // "A", "B", ...
    double valueMm = 0.0;
    // 측정면 범위(있으면) - 보조선을 측정점이 아니라 면에서 치수선에 가장 가까운 끝에서 시작해서, 보조선이 형상을
    // 가로질러 길게 그려지지 않게 한다(도면 관례: 보조선은 형상 외곽에서 뺀다).
    bool hasFaceBoxes = false;
    geometry::BoundingBox faceBoxA;
    geometry::BoundingBox faceBoxB;
};

struct CtpDimensionLayout {
    std::string label;
    int axis = 0;
    bool visible = true;     // 측정축이 시선 방향이면 false(화면에서 한 점으로 찌그러짐)
    geometry::Vec3 measureA;
    geometry::Vec3 measureB;
    geometry::Vec3 lineA;    // 여백으로 뺀 치수선 양 끝(월드 좌표, 시선 축 성분은 형상 중심)
    geometry::Vec3 lineB;
    geometry::Vec3 extA;     // 보조선 시작점(측정면에서 치수선에 가장 가까운 곳, 면 범위가 없으면 측정점)
    geometry::Vec3 extB;
    double valueMm = 0.0;
};

struct CtpCaptureView {
    int viewAxis = 2;        // 시선 축
    double viewSign = -1.0;  // 시선 방향 = viewSign * e[viewAxis] (-1이면 + 쪽에서 내려다봄)
    int rightAxis = 0;       // 화면 오른쪽 = rightSign * e[rightAxis]
    double rightSign = 1.0;
    int upAxis = 1;          // 화면 위쪽 = upSign * e[upAxis]
    double upSign = 1.0;
    geometry::Vec3 center;   // 화면 중심(월드)
    double halfWidth = 1.0;  // 화면 절반 너비/높이(mm)
    double halfHeight = 1.0;
    bool sectionEnabled = true;
    int sectionAxis = 2;
    double sectionCoord = 0.0;
    double sectionSign = -1.0; // 셰이더 관례: (p[axis] - coord) * sign < 0 인 쪽을 버린다
};

struct CtpCapturePlan {
    bool ok = false;
    CtpCaptureView view;
    std::vector<CtpDimensionLayout> dims;
    std::string warning;     // 그릴 수 없는 치수가 있으면 사람이 읽을 설명
};

// 프리셋 자동 치수 후보 -> CTP 이미지 치수(측정점 = 후보의 기준 좌표, 면 범위 = 후보의 두 측정면).
CtpDimensionInput MakeCtpDimension(const PresetDimensionCandidate& candidate, const std::string& label);

// CTP 양식 v1(docs/양식/CTP_양식_디자인_v1.xlsx, A4 세로 한 장)의 이미지 칸 너비/높이 비율. 표가 길수록(페이지의
// 치수 개수 x 인치 수) 이미지 칸이 낮아진다 - 양식 생성기(make_ctp_sample.py)와 같은 높이 계산.
double CtpImageAspect(int dimensionsOnPage, int inchCount);

// featurePlanes는 형상 평면(바깥 법선 + 면 범위) - 시선 축을 고르는 데만 쓴다(비어 있어도 된다).
// aspect = 출력 이미지 너비 / 높이.
CtpCapturePlan PlanCtpCapture(const std::vector<CtpDimensionInput>& dims, const geometry::BoundingBox& featureBox,
                              const std::vector<geometry::PlaneCandidate>& featurePlanes, double aspect);

} // namespace rule
