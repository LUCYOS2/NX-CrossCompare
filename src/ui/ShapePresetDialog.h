#pragma once

#include <QDialog>
#include <QVector3D>

#include "database/Database.h"
#include "geometry/IGeometryAdapter.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace viewer {
class MultiViewportPanel;
}

namespace ui {

// § 형상 프리셋(2026-09-18) - Hook/Flange류처럼 원통/평면 판별 하나로는 못 잡는 복합
// 형상을 이름 붙여 등록해두고 재사용하는 창. 계획서("형상 프리셋" 기능, plan mode에서
// 사용자 승인) §UI/워크플로우 참고 - 사람이 확인하는 지점은 여기(등록 시 1회)뿐이고,
// 이후 다른 인치에 규칙을 적용할 때는 RuleEngine이 자동으로 검색+선택한다.
//
// 흐름: 뷰어에서 형상 클릭 -> IGeometryAdapter::CaptureFeaturePatch로 지문 캡처 ->
// "같은 모델에서 동일 형상 찾기"로 정확도 미리 확인(오탐 있으면 이름을 다르게 등록해서
// 재시도) -> 이름/치수 필터 입력 -> 저장(database::Database::SaveShapePreset).
// RuleEditorDialog의 "프리셋으로 추가..." 버튼이 이 다이얼로그를 열어서 저장된 프리셋
// 중 하나를 고르게 한다 - 그 경우 SelectedPreset()/SelectedDescriptor()로 값을 읽어간다.
class ShapePresetDialog : public QDialog {
    Q_OBJECT

public:
    ShapePresetDialog(database::Database* db, int projectId, geometry::IGeometryAdapter* adapter,
                       const std::vector<std::pair<std::string, geometry::ModelHandle>>& models,
                       QWidget* parent = nullptr);

    // "이 프리셋 사용"으로 닫혔을 때만 값이 있다(accept() 호출 전 목록에서 행을 선택해야
    // 버튼이 활성화된다).
    std::optional<database::ShapePresetSummary> SelectedPreset() const { return selectedPreset_; }
    geometry::FeaturePatchDescriptor SelectedDescriptor();
    geometry::ModelHandle SelectedModelHandle() const;

private slots:
    void onModelComboChanged();
    void onCaptureClicked();
    void onFacePicked(geometry::ModelHandle handle, QVector3D rayOrigin, QVector3D rayDir);
    void onFindSameModelClicked();
    void onSaveClicked();
    void onDeleteClicked();
    void onTableSelectionChanged();

private:
    void RebuildViewer();
    void RefreshPresetsTable();
    void ResetCaptureState();

    database::Database* db_;
    int projectId_;
    geometry::IGeometryAdapter* adapter_;
    std::vector<std::pair<std::string, geometry::ModelHandle>> models_;

    QComboBox* modelCombo_ = nullptr;
    viewer::MultiViewportPanel* viewerPanel_ = nullptr;
    QWidget* viewerHost_ = nullptr; // viewerPanel_이 들어갈 레이아웃 자리(RebuildViewer가 교체)

    QPushButton* captureButton_ = nullptr;
    QLabel* captureStatusLabel_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QComboBox* dimFilterKindCombo_ = nullptr;
    QDoubleSpinBox* dimFilterValueSpin_ = nullptr;
    QPushButton* findSameModelButton_ = nullptr;
    QLabel* sameModelCountLabel_ = nullptr;
    QPushButton* saveButton_ = nullptr;

    QTableWidget* presetsTable_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    QPushButton* usePresetButton_ = nullptr;

    bool pickArmed_ = false;
    bool hasCapturedPatch_ = false;
    geometry::FeaturePatchDescriptor capturedDescriptor_;
    geometry::ModelHandle capturedHandle_ = geometry::kInvalidModelHandle;

    std::vector<database::ShapePresetSummary> presets_;
    std::optional<database::ShapePresetSummary> selectedPreset_;
};

} // namespace ui
