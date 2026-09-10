#pragma once

#include <QDialog>
#include <QVector3D>

#include "database/Database.h"
#include "geometry/IGeometryAdapter.h"
#include "rule/Rule.h"

#include <string>
#include <utility>
#include <vector>

class QCheckBox;
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
// 삭제를 다 한다.
//
// § 3D 클릭 피킹(2026-09-09) - "타이핑 대신 실제로 클릭해서 짚는다"는 요청에 따라
// 비모달로 띄운다(MainWindow가 exec() 대신 show()). "지정..." 버튼을 누르면 뷰어가
// 피킹 모드로 전환되고, 다음 클릭이 이 다이얼로그로 돌아와 형상타입/지름(원통) 또는
// 평면 여부를 자동으로 채운다 - 검색(AnchorSearchDialog, 텍스트 기반)은 클릭하기
// 어려운 경우의 보조 수단으로 남겨둔다.
//
// § 레이아웃/포인트 리스트 재구성(2026-09-10) - "CTP 이미지 캡쳐 도구"라는 실제 목적에
// 맞춰 전면 개편:
//   - 좌측: 규칙 리스트(좁게) / 우측: 측정 이미지(크게) -> 규칙 이름 -> 형상 타입(포인트
//     리스트) -> 측정 타입 -> Selector -> Projection -> Tolerance.
//   - "포인트 A/B" 개별 입력칸을 없애고, 클릭(지정...)이나 검색으로 추가한 포인트들이
//     쌓이는 리스트(points_) 하나로 통합했다 - 백엔드 Rule::anchors가 원래
//     std::vector<Anchor>(가변 길이)라 이 UI 변경에 스키마 변경이 필요 없다.
//   - 지름은 더 이상 사용자가 직접 편집하는 입력칸이 아니다("치수 측정 툴이지 치수 입력
//     툴이 아니다") - 클릭/검색으로 얻은 값을 리스트에 읽기 전용으로만 보여주고, 내부
//     필터값으로 계속 쓰인다.
//   - "측정 기준(축방향/기준면)"은 별도 섹션을 새로 만들지 않고 기존 메커니즘으로
//     흡수했다: 축방향은 "다음 추가할 포인트에 적용할 필터" 콤보(nextPointDirection_)로,
//     기준면은 평면을 클릭/검색해서 추가하면 그 포인트의 kind==Plane으로 자동 표시된다
//     (BuildRule()이 point_to_plane일 때 리스트에서 Plane kind 항목을 referencePlane으로
//     라우팅).
//   - 측정 이미지 캡쳐는 메인 뷰어(viewerPanel_)를 그대로 재사용하되, 단면뷰를 굳이
//     메인 툴바로 안 나가고도 켤 수 있도록 이 다이얼로그 안에 단축 체크박스를 뒀다
//     (MultiViewportPanel::setSectionEnabled 경유).
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
    void onRowClicked(int row);
    void onAddOrUpdateClicked();
    void onDeleteClicked();
    void onSearchPointClicked();
    void onPickPointClicked();
    void onDeletePointClicked();
    void onFacePicked(geometry::ModelHandle handle, QVector3D rayOrigin, QVector3D rayDir);
    void onCaptureRuleImageClicked();
    void onSectionViewToggled(bool checked);

protected:
    // 다이얼로그를 닫을 때 피킹 모드가 켜진 채로 남아 뷰어가 계속 회전을 안 하게
    // 되는 걸 막는다 - 사용자가 "지정" 눌러놓고 그냥 닫아버려도 뒤가 깔끔해야 한다.
    void closeEvent(QCloseEvent* event) override;

private:
    // 포인트 리스트의 한 행 - 클릭(피킹) 또는 검색으로 추가된 형상 하나.
    // kind는 BuildRule()이 "이 항목이 anchor(점)인지 referencePlane(평면)인지"를
    // 판단하는 데 쓴다 - 텍스트 검색으로 추가한 항목은 항상 Cylinder(AnchorSearchDialog가
    // 원통만 다루므로), 기존 규칙을 불러온 항목은 어느 Rule 필드에서 왔는지로 정한다.
    struct PointEntry {
        std::string type;
        std::string partName;
        double diameterMm = 0.0;
        std::string directionAxis; // "" = 필터 안 씀
        geometry::PickedFaceKind kind = geometry::PickedFaceKind::None;
    };

    void refreshTable();
    void refreshPointsTable();
    void resetForm();
    rule::Rule BuildRule() const;
    void LoadRuleIntoForm(const rule::Rule& r);
    // 새 포인트를 리스트에 추가 - 최대 2개까지(그 이상 필요한 측정 타입이 없음). 초과
    // 시도하면 안내만 하고 무시한다.
    void AddPoint(const PointEntry& entry);
    // "다음 추가할 포인트"에 적용할 축 방향 필터 값 - "(필터 안 씀)"이면 빈 문자열.
    std::string CurrentDirectionFilter() const;
    // 포인트 A/B가 둘 다 채워졌으면(원통/평면 조합으로) 측정타입을 자동으로 맞춰준다.
    void TryInferMeasurementType();
    // 캡쳐한(또는 불러온) 이미지를 미리보기 라벨에 반영.
    void UpdateImagePreview();

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

    // 지금 폼이 들고 있는 포인트들(최대 2개) - 클릭/검색으로 추가되고, BuildRule()이
    // 측정 타입에 따라 anchors[]/referencePlane으로 나눠 담는다.
    std::vector<PointEntry> points_;
    bool pickArmed_ = false;

    // § 이미지 캡쳐 연동 - 현재 폼이 들고 있는(캡쳐했거나 기존 규칙에서 불러온) 이미지
    // 파일 경로. 비어있으면 이미지 없음.
    std::string ruleImagePath_;

    QTableWidget* table_; // 좌측 - 저장된 규칙 목록

    QLabel* imagePreviewLabel_;
    QPushButton* captureImageButton_;
    QCheckBox* sectionViewCheck_;

    QLineEdit* name_;

    QTableWidget* pointsTable_; // 형상 타입 - 추가된 포인트 리스트
    QComboBox* nextPointDirection_;
    QPushButton* pickPointButton_;
    QPushButton* searchPointButton_;
    QPushButton* deletePointButton_;
    QLabel* pickStatusLabel_;

    QComboBox* measurementType_;
    QComboBox* selector_;
    QComboBox* projection_;
    QDoubleSpinBox* tolerancePlus_;
    QDoubleSpinBox* toleranceMinus_;

    QPushButton* addOrUpdateButton_;
};

} // namespace ui
