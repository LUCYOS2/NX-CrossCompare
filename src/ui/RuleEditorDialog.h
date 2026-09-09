#pragma once

#include <QDialog>
#include <QVector3D>

#include "database/Database.h"
#include "geometry/IGeometryAdapter.h"
#include "rule/Rule.h"

#include <string>
#include <utility>
#include <vector>

class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QWidget;

namespace viewer {
class MultiViewportPanel;
}

namespace ui {

// § 규칙 관리 통합(2026-09-08, "포인트 그룹" 폐기) - 포인트 그룹과 새 규칙 추가가
// anchor_type/part_name/지름 필드를 그대로 중복해서 갖고 있어 역할이 겹친다는 지적을
// 받아, 별도 그룹 저장소 없이 이 다이얼로그 하나로 규칙 목록 조회 + 생성 + 수정 +
// 삭제를 다 한다(AnchorGroupDialog에서 검증한 "행 클릭 -> 폼 로드 -> 추가/수정 버튼
// 토글" 패턴 재사용).
//
// § 3D 클릭 피킹(2026-09-09) - "타이핑 대신 실제로 클릭해서 짚는다"는 요청에 따라
// 비모달로 띄운다(MainWindow가 exec() 대신 show() - 사용자가 이 창을 열어둔 채로
// 뒤의 3D 뷰포트를 클릭할 수 있어야 하므로). "포인트 지정" 버튼을 누르면 뷰어가 피킹
// 모드로 전환되고, 다음 클릭이 이 다이얼로그로 돌아와 anchor_type/지름(원통) 또는
// 기준평면(평면)을 자동으로 채운다 - 검색(AnchorSearchDialog, 텍스트 기반)은 클릭하기
// 어려운 경우의 보조 수단으로 계속 남겨둔다.
class RuleEditorDialog : public QDialog {
    Q_OBJECT

public:
    RuleEditorDialog(database::Database* db, int projectId, geometry::IGeometryAdapter* adapter,
                      viewer::MultiViewportPanel* viewerPanel,
                      const std::vector<std::pair<std::string, geometry::ModelHandle>>& models,
                      QWidget* parent = nullptr);

    // STEP을 다시 불러오면 MainWindow가 MultiViewportPanel을 통째로 새로 만드는데,
    // 이 다이얼로그가 비모달로 열려있는 동안 그 일이 일어나면 이전 패널 포인터가
    // 댕글링된다 - MainWindow가 재로드 직후 이걸 호출해 새 패널로 다시 연결해준다.
    void RewireViewerPanel(viewer::MultiViewportPanel* panel);

signals:
    // 추가/수정/삭제가 즉시 DB에 반영된 뒤 쏜다 - MainWindow는 이걸 받아서 비교
    // 테이블만 새로 고치면 된다(비모달이라 exec() 반환을 기다릴 수 없어서 필요).
    void rulesChanged();

private slots:
    void onMeasurementTypeChanged(int index);
    void onRowClicked(int row);
    void onAddOrUpdateClicked();
    void onDeleteClicked();
    void onSearchAnchorAClicked();
    void onSearchAnchorBClicked();
    void onPickAnchorAClicked();
    void onPickAnchorBClicked();
    void onPickPlaneClicked();
    void onFacePicked(geometry::ModelHandle handle, QVector3D rayOrigin, QVector3D rayDir);

protected:
    // 다이얼로그를 닫을 때 피킹 모드가 켜진 채로 남아 뷰어가 계속 회전을 안 하게
    // 되는 걸 막는다 - 사용자가 "지정" 눌러놓고 그냥 닫아버려도 뒤가 깔끔해야 한다.
    void closeEvent(QCloseEvent* event) override;

private:
    // "포인트 지정" 버튼 중 어느 걸 눌렀는지 - onFacePicked가 결과를 어느 필드에
    // 채울지 판단하는 데 쓴다.
    enum class PickTarget { None, AnchorA, AnchorB, ReferencePlane };

    void refreshTable();
    void resetForm();
    rule::Rule BuildRule() const;
    void LoadRuleIntoForm(const rule::Rule& r);
    // 세 "포인트 지정" 버튼이 공유하는 준비 동작 - 대상만 다르다.
    void ArmPicking(PickTarget target, const QString& statusText);

    database::Database* db_;
    int projectId_;
    geometry::IGeometryAdapter* adapter_;
    viewer::MultiViewportPanel* viewerPanel_ = nullptr;
    std::vector<std::pair<std::string, geometry::ModelHandle>> models_;
    // 0 = "규칙 추가" 모드. 0이 아니면 그 id의 사용자 규칙을 수정하는 중.
    int editingRuleId_ = 0;
    // 목록 테이블에 보이는 사용자(DB 저장) 규칙만 - 내장 규칙(BuiltInCatalog)은 코드에
    // 있는 것이라 여기서 수정/삭제 대상이 아니다.
    std::vector<rule::Rule> rules_;

    PickTarget pickTarget_ = PickTarget::None;

    QTableWidget* table_;

    QLineEdit* name_;
    QComboBox* measurementType_;

    QLineEdit* anchorAType_;
    QLineEdit* anchorAPart_;
    QDoubleSpinBox* anchorADiameter_;
    QPushButton* searchAButton_;
    QPushButton* pickAButton_;

    QWidget* anchorBGroup_;
    QLineEdit* anchorBType_;
    QLineEdit* anchorBPart_;
    QDoubleSpinBox* anchorBDiameter_;
    QPushButton* searchBButton_;
    QPushButton* pickBButton_;

    QWidget* planeRefGroup_;
    QLineEdit* planeType_;
    QLineEdit* planePart_;
    QPushButton* pickPlaneButton_;

    QLabel* pickStatusLabel_;

    QComboBox* selector_;
    QComboBox* projection_;
    QDoubleSpinBox* tolerancePlus_;
    QDoubleSpinBox* toleranceMinus_;

    QPushButton* addOrUpdateButton_;
};

} // namespace ui
