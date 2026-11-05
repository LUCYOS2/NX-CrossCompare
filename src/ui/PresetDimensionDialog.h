#pragma once

#include <QDialog>

#include "database/Database.h"
#include "geometry/IGeometryAdapter.h"
#include "rule/PresetDimensions.h"

#include <string>
#include <utility>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace viewer {
class MultiViewportPanel;
}

namespace ui {

// § 프리셋 자동 치수(2026-11-05, 1단계) - "후크 형상을 추가하면 전장 사이즈처럼 잴 만한 포인트와 치수선을
// 자동으로 만들어 달라"는 요청. 규칙 관리의 "프리셋 자동 치수..."에서 프리셋을 고르면 열린다.
//   - 대표 인치 모델에서 그 프리셋과 비슷한 형상을 찾고(여럿이면 콤보로 고름),
//   - 형상 크기(양 끝면 사이)와 외곽 끝면에서의 위치 후보를 체크 목록으로 보여주며(rule::PresetDimensions),
//   - 행을 고르면 내장 뷰어에 측정면 마커 + 여백으로 뺀 치수선을 그린다.
//   - "모든 인치에서 확인"은 저장될 규칙을 그대로 다른 인치에 적용해 값/실패 사유를 열로 보여준다.
//   - 체크한 항목만 face_to_face_gap 규칙으로 한 번에 저장한다(기준 인치에서 주변 지문까지 확정).
class PresetDimensionDialog : public QDialog {
    Q_OBJECT

public:
    PresetDimensionDialog(database::Database* db, int projectId, geometry::IGeometryAdapter* adapter,
                          const std::vector<std::pair<std::string, geometry::ModelHandle>>& models,
                          geometry::ModelHandle targetHandle, const std::string& presetName,
                          const geometry::FeaturePatchDescriptor& descriptor, QWidget* parent = nullptr);

    int SavedRuleCount() const { return savedRuleCount_; }

private:
    void OnShapeChanged();
    void RefreshPreview();
    void OnCheckAllInches();
    void OnSaveClicked();
    // 체크 여부와 무관하게 row 후보를 저장될 모양 그대로(기준 인치 주변 지문까지) 만든다.
    rule::Rule BuildRule(int row) const;
    std::string ChooseOffsetAxis(int measuredAxis) const;

    database::Database* db_;
    int projectId_;
    geometry::IGeometryAdapter* adapter_;
    std::vector<std::pair<std::string, geometry::ModelHandle>> models_;
    geometry::ModelHandle targetHandle_;
    std::string presetName_;

    geometry::BoundingBox modelBox_;
    std::vector<geometry::PlaneCandidate> modelPlanes_;
    std::vector<geometry::PatchCandidate> shapes_;
    std::vector<rule::PresetDimensionCandidate> candidates_;
    int savedRuleCount_ = 0;

    viewer::MultiViewportPanel* viewerPanel_ = nullptr;
    QComboBox* shapeCombo_ = nullptr;
    QTableWidget* table_ = nullptr;
    QDoubleSpinBox* tolerancePlus_ = nullptr;
    QDoubleSpinBox* toleranceMinus_ = nullptr;
    QPushButton* checkAllInchesButton_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};

} // namespace ui
