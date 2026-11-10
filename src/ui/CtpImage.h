#pragma once

#include <QImage>
#include <QString>

#include "geometry/IGeometryAdapter.h"
#include "rule/CtpCapture.h"

namespace viewer {
class MultiViewportPanel;
}

namespace ui {

// § CTP 캡처 이미지(2026-11-09) - 구도 계산(rule::PlanCtpCapture) 결과를 뷰어 좌표로 옮겨 그린다.
// 이미지 너비는 kCtpImageWidthPx로 고정하고 높이는 plan의 화면 비율에서 정한다(A4 190mm 폭에 약 240dpi).
constexpr int kCtpImageWidthPx = 1800;

QImage RenderCtpPlan(viewer::MultiViewportPanel* panel, geometry::ModelHandle handle, const rule::CtpCapturePlan& plan);

// rule_images 폴더에 "ctp_<시각>.png"로 저장하고 전체 경로를 돌려준다(실패하면 빈 문자열).
QString SaveCtpImage(const QImage& image);

} // namespace ui
