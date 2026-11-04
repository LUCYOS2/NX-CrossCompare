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
    // § 형상 프리셋 수동 선택(2026-09-18) - "자동 캡처가 원하는 형상과 다르게 잡힌다"는
    // 피드백. 면을 하나씩 클릭해서 직접 고르는 모드(Shift+클릭=선택 해제).
    void onManualSelectClicked();
    void onFinishManualSelectClicked();
    void onFacePicked(geometry::ModelHandle handle, QVector3D rayOrigin, QVector3D rayDir, bool shiftHeld);
    // § 드래그 영역 다중 선택(2026-09-23) - "전체 덩어리를 드래그해서 선택"할 수 있게
    // 해달라는 요청. onFacePicked(면 하나)와 같은 add/remove 규칙을 여러 면에 한 번에
    // 적용한다.
    void onFacesInRegionPicked(geometry::ModelHandle handle, std::vector<int> faceIndices, bool shiftHeld);
    void onFindSameModelClicked();
    void onSaveClicked();
    void onDeleteClicked();
    void onTableSelectionChanged();

private:
    void RebuildViewer();
    void RefreshPresetsTable();
    void ResetCaptureState();
    // 자동 캡처/수동 완료 둘 다 결과(FeaturePatchCapture)를 똑같이 반영해야 해서 공용화.
    void ApplyCapture(geometry::ModelHandle handle, const geometry::FeaturePatchCapture& capture);
    void UpdateManualSelectionHighlight();
    // § Ctrl+드래그 바로 선택(2026-11-04) - 다른 작업 중이 아니면 피킹 + 사각형 선택을 켜 둔다(cpp 주석 참고).
    void ArmIdleSelection();
    // shiftHeld==false면 manualSelectedFaces_에 없을 때만 추가, true면 있으면 제거 -
    // 클릭 한 번(onFacePicked)과 드래그 영역(onFacesInRegionPicked) 양쪽이 공유.
    void ToggleManualFace(int faceIndex, bool shiftHeld);

    database::Database* db_;
    int projectId_;
    geometry::IGeometryAdapter* adapter_;
    std::vector<std::pair<std::string, geometry::ModelHandle>> models_;

    QComboBox* modelCombo_ = nullptr;
    viewer::MultiViewportPanel* viewerPanel_ = nullptr;
    QWidget* viewerHost_ = nullptr; // viewerPanel_이 들어갈 레이아웃 자리(RebuildViewer가 교체)

    QPushButton* captureButton_ = nullptr;
    QPushButton* manualSelectButton_ = nullptr;
    QPushButton* finishManualSelectButton_ = nullptr;
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

    // § 형상 프리셋 수동 선택 - manualSelectMode_ 동안은 클릭할 때마다 pickArmed_처럼
    // 한 번 쓰고 꺼지는 게 아니라 계속 누적한다("선택 완료" 버튼을 눌러야 끝남).
    bool manualSelectMode_ = false;
    std::vector<int> manualSelectedFaces_;

    std::vector<database::ShapePresetSummary> presets_;
    std::optional<database::ShapePresetSummary> selectedPreset_;
};

} // namespace ui
