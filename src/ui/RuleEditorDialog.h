#pragma once

#include <QDialog>
#include <QPixmap>
#include <QVector3D>

#include "database/Database.h"
#include "geometry/IGeometryAdapter.h"
#include "rule/Rule.h"
#include "viewer/MultiViewportPanel.h" // ModelViewport::CaptureInfo 필요

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
class QVBoxLayout;
class QWidget;

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
//
// § 캡쳐 화질/구도 개선(2026-09-11) - "뷰어 전체를 캡쳐하니 배경만 많고 도면은 작다"는
// 피드백에 따라: (1) captureImageButton_이 이제 패널 전체가 아니라 활성 뷰포트 하나만
// 잘라 찍고(MultiViewportPanel::grabActiveViewport), (2) captureRegionButton_으로
// 사용자가 직접 사각형을 드래그해서 그 영역만 캡쳐할 수 있다
// (MultiViewportPanel::setCaptureRegionModeActive + captureRegionGrabbed 시그널).
// 구도를 맞추기 쉽도록 메인 툴바에 추가한 XY/-XY/YZ/ISO 뷰 프리셋도 이 다이얼로그
// 안에서 바로 누를 수 있게 같이 뒀다.
//
// § 정지 이미지 클릭 피킹 폐기, 라이브 뷰어 내장으로 전환(2026-09-11) - "캡쳐된 사진 위
// 클릭"(이전 리비전)으로 한 번 개선했었지만, 그 다음 라운드 피드백이 "이미지 박스 안에서
// 줌/팬/회전이 다 됐으면 좋겠다"였다 - 회전은 정지 사진 위에서 원천적으로 불가능하므로
// (사용자 확인 후 결정), 정지 이미지 + 얼려둔 카메라로 광선을 역산하던 방식을 통째로
// 버리고 대신 이 다이얼로그가 자기 소유의 MultiViewportPanel(editorViewerPanel_)을
// "측정 이미지" 자리에 직접 내장한다. MultiViewportPanel이 마우스 회전/팬/줌, 6뷰+ISO
// 프리셋, 단면뷰, 피킹 모드, 뷰포트 캡쳐를 전부 이미 갖추고 있어서(메인 창이 쓰는 것과
// 완전히 같은 컴포넌트) 새로 구현할 게 거의 없다 - 그냥 "메인 창 뒤에 숨어있던 뷰어"를
// 다이얼로그 안으로 옮겨온 셈이다. 그래서 더 이상 메인 창의 공유 viewerPanel_이 필요
// 없고(생성자 인자에서 뺐다), 캡쳐는 순수하게 "현재 라이브 뷰를 PNG로 저장"(CTQ 문서용)
// 역할만 남았다 - 클릭 피킹은 항상 라이브로 바로 일어나므로 캡쳐가 선행될 필요가 없다.
class RuleEditorDialog : public QDialog {
    Q_OBJECT

public:
    RuleEditorDialog(database::Database* db, int projectId, geometry::IGeometryAdapter* adapter,
                      const std::vector<std::pair<std::string, geometry::ModelHandle>>& models,
                      QWidget* parent = nullptr);

    // STEP을 다시 불러오면 MainWindow가 handle을 전부 새로 만드는데, 이 다이얼로그가
    // 비모달로 열려있는 동안 그 일이 일어나면 내장 뷰어(editorViewerPanel_)가 들고 있던
    // 이전 handle들이 댕글링된다 - MainWindow가 재로드 직후 새 모델 목록으로 이걸 호출하면
    // 내장 패널을 통째로 새로 만들어 갈아끼운다(RebuildEditorPanel).
    void RewireModels(const std::vector<std::pair<std::string, geometry::ModelHandle>>& models);

signals:
    // 추가/수정/삭제가 즉시 DB에 반영된 뒤 쏜다 - MainWindow는 이걸 받아서 비교
    // 테이블만 새로 고치면 된다(비모달이라 exec() 반환을 기다릴 수 없어서 필요).
    void rulesChanged();

private slots:
    void onRowClicked(int row);
    void onAddOrUpdateClicked();
    // § 같은 캡쳐에서 여러 CTQ 항목 만들기(2026-09-11) - "한 캡쳐에 측정 항목이 여러 개일
    // 수도 있는데, 그때마다 복사해서 수정하는 게 나을지" 질문에 대한 답 - 규칙 하나当
    // 측정 하나(점 최대 2개) 전제가 RuleEngine 전체(Selector/측정 로직/DB 스키마)에 깊이
    // 박혀있어 "포인트 3~4개까지" 확장은 위험 부담이 큰 반면, 이 방식은 기존 구조를
    // 그대로 두고 "지금 폼 내용을 새 규칙으로 하나 더 저장"만 하면 되어 훨씬 안전하다.
    // editingRuleId_와 무관하게 항상 새로 INSERT하고, 저장 뒤 폼은 리셋되지만 뷰어
    // 카메라 위치는 그대로 남는다(resetForm이 카메라를 안 건드림) - 다음 항목의 포인트를
    // 바로 이어서 찍을 수 있다(다만 캡쳐 이미지는 그 항목의 마커를 새로 반영해야 하니
    // "현재 화면 캡쳐"는 항목마다 다시 눌러야 함).
    void onSaveAsClicked();
    void onDeleteClicked();
    void onSearchPointClicked();
    void onPickPointClicked();
    void onDeletePointClicked();
    void onFacePicked(geometry::ModelHandle handle, QVector3D rayOrigin, QVector3D rayDir);
    void onCaptureRuleImageClicked();
    void onCaptureRegionClicked();
    void onCaptureRegionGrabbed(QPixmap pixmap, viewer::ModelViewport::CaptureInfo info);

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
        // kind==Point일 때만 의미 있음 - "종류" 열에 꼭짓점/모서리 중간점을 구분해 보여준다.
        geometry::PointSubKind pointSubKind = geometry::PointSubKind::None;
        // § NX 스타일 포인트 스냅(2026-09-11) - 라이브 뷰에서 클릭으로 찍은 포인트만
        // 유효(hasPosition==true) - 뷰어에 마커로 다시 표시하는 데 쓴다. 검색으로
        // 추가했거나 DB에서 불러온 포인트는 원래 좌표를 안 들고 있어 hasPosition==false.
        bool hasPosition = false;
        geometry::Vec3 position;
        // § 도면 설계법 치수선(2026-09-12) - "치수 표시선을 도면 여백으로 빼달라"는 요청
        // 이후, 전장 사이즈 자동 포인트처럼 "실제 측정 지점"(위 position)과 "치수선이
        // 그려지는 위치"(도면 여백으로 오프셋)가 다른 경우에만 쓴다. 세팅되면
        // RefreshMarkers()가 이 둘을 잇는 가이드선(연장선)을 추가로 그린다. 수동 피킹
        // 포인트는 이 필드가 비어있어(hasDimensionOffset==false) 예전처럼 포인트 위치에
        // 바로 치수선이 그려진다.
        bool hasDimensionOffset = false;
        geometry::Vec3 dimensionOffsetPosition;
        // § 리스트 항목 분류(2026-09-11) - "INCH(적용 인치)" 열에 쓴다. 클릭/검색 둘 다
        // 어느 모델(인치)이 대상이었는지 알 수 있다(onFacePicked의 handle 인자,
        // AnchorSearchDialog::SelectedModelHandle()). DB에서 불러온 포인트는 모델 정보가
        // 없어(kInvalidModelHandle) INCH 열이 "-"로 남는다.
        geometry::ModelHandle modelHandle = geometry::kInvalidModelHandle;
    };

    void refreshTable();
    void refreshPointsTable();
    void resetForm();
    // 이름/포인트 개수/point_to_plane 평면 조건 확인 - onAddOrUpdateClicked()와
    // onSaveAsClicked()가 공유한다. 문제가 있으면 안내 메시지를 띄우고 false.
    bool ValidateFormForSave();
    rule::Rule BuildRule() const;
    void LoadRuleIntoForm(const rule::Rule& r);
    // 새 포인트를 리스트에 추가 - 최대 2개까지(그 이상 필요한 측정 타입이 없음). 초과
    // 시도하면 안내만 하고 무시한다.
    void AddPoint(const PointEntry& entry);
    // "다음 추가할 포인트"에 적용할 축 방향 필터 값 - "(필터 안 씀)"이면 빈 문자열.
    std::string CurrentDirectionFilter() const;
    // 포인트 A/B가 둘 다 채워졌으면(원통/평면 조합으로) 측정타입을 자동으로 맞춰준다.
    void TryInferMeasurementType();
    // "현재 화면 캡쳐"/드래그 영역 캡쳐 공통 로직 - PNG로 저장하고 ruleImagePath_를
    // 갱신한다. 더 이상 클릭 피킹이 이 이미지에 의존하지 않으므로(항상 라이브 뷰에서
    // 바로 피킹) "미리보기 갱신"은 필요 없고 저장 결과만 pickStatusLabel_로 알려준다.
    void SaveCapturedImage(const QPixmap& pixmap);
    // editorViewerPanel_을 targetModelCombo_가 가리키는 모델 "하나"로 (다시) 만든다 -
    // 생성자와 RewireModels()/콤보 선택 변경 셋 다 이걸 호출한다. 기존 패널이 있으면
    // rightLayout_ 안에서 자리를 바꿔치기하고 폐기한다.
    void RebuildEditorPanel();
    // § 대표 인치 선택 - models_ 기준으로 targetModelCombo_ 항목을 다시 채운다. 이전에
    // 선택했던 handle이 새 목록에도 있으면 그 항목을 유지한다(RewireModels로 STEP을
    // 다시 불러온 경우 handle 값 자체는 바뀌므로 대개는 못 찾고 첫 항목으로 돌아간다).
    void PopulateTargetModelCombo();
    // § NX 스타일 포인트 스냅 - points_ 중 hasPosition인 것들만 모아 editorViewerPanel_에
    // 마커로 반영한다. points_가 바뀌는 모든 지점(AddPoint/삭제/폼 리셋/패널 재생성)에서
    // 호출해야 한다.
    void RefreshMarkers();
    // § 전장 사이즈 자동 포인트(2026-09-12) - "전장사이즈는 그 축 방향으로 제일 최외곽
    // 포인트로 자동 지정해달라"는 요청. OverallSize는 원래 anchor/포인트 없이 바운딩박스로
    // 직접 계산되는 측정이라 points_가 항상 비어있었다(뷰어에 마커/치수선이 안 뜸). 이
    // 함수는 대상 모델의 BoundingBox에서 해당 축의 min/max 지점(다른 두 축은 중앙값)을
    // 골라 포인트 2개를 만든다 - 두 점 사이 거리가 RuleEngine이 실제로 계산하는 값과
    // 정확히 같아서(측정값=시각화가 일치), 그 축을 가로지르는 직선이 모델 중앙을 관통하는
    // 자연스러운 치수선이 된다. axis가 X/Y/Z가 아니거나 handle이 유효하지 않으면 빈 벡터.
    // offsetAxis는 치수선을 밀어낼 방향(도면 여백 쪽) - ChooseOverallSizeOffsetAxis() 참고.
    std::vector<PointEntry> AutoDetectOverallSizePoints(
        geometry::ModelHandle handle, const std::string& axis, const std::string& offsetAxis) const;
    // § 카메라 회전 시 치수선 축 자동 재선택(2026-09-13) - "화면뷰가 바뀔 때마다 최적
    // 위치로 업데이트해달라"는 요청. measuredAxis가 아닌 나머지 두 축 중, 현재 카메라
    // 시선 방향과 가장 수직인(=화면에 가장 크게 벌어져 보이는, 안 보이는 방향이 아닌)
    // 축을 골라 반환한다 - 그래야 치수선이 화면과 거의 나란해져 안 보이는 각도를 피한다.
    // editorViewerPanel_이 없으면(모델 없음) 첫 후보를 그냥 반환.
    std::string ChooseOverallSizeOffsetAxis(const std::string& measuredAxis) const;
    // § 위 함수를 호출해야 할 시점(측정타입을 overall_size로 바꿨을 때/Projection 축을
    // 바꿨을 때/그런 규칙을 목록에서 불러왔을 때)마다 공통으로 거치는 진입점 - 현재
    // measurementType_/targetModelCombo_ 값과 ChooseOverallSizeOffsetAxis()로 고른 오프셋
    // 축을 읽어 points_를 다시 채우고 화면(표/마커)을 갱신한다. 조건이 안 맞으면
    // (overall_size가 아니거나 축 미지정) 아무것도 하지 않는다. 호출될 때마다
    // dimensionOffsetManuallyAdjusted_도 초기화(false)한다 - "새로 자동 계산했다"는 뜻이라
    // 이전 수동 드래그 조정은 더 이상 유효하지 않다.
    void AutoDetectOverallSizePointsIfApplicable();
    // § 치수선 드래그(2026-09-13) - editorViewerPanel_::dimensionOffsetDragged에 연결.
    // 오프셋을 아는(hasDimensionOffset) 포인트들의 dimensionOffsetPosition을 현재 오프셋
    // 축 방향으로 deltaWorld만큼 이동시킨다.
    void onDimensionOffsetDragged(float deltaWorld);

    database::Database* db_;
    int projectId_;
    geometry::IGeometryAdapter* adapter_;
    std::vector<std::pair<std::string, geometry::ModelHandle>> models_;
    // 0 = "규칙 추가" 모드. 0이 아니면 그 id의 사용자 규칙을 수정하는 중.
    int editingRuleId_ = 0;
    // 목록 테이블에 보이는 사용자(DB 저장) 규칙만 - 내장 규칙(BuiltInCatalog)은 코드에
    // 있는 것이라 여기서 수정/삭제 대상이 아니다.
    std::vector<rule::Rule> rules_;

    // 지금 폼이 들고 있는 포인트들(최대 2개) - 클릭/검색으로 추가되고, BuildRule()이
    // 측정 타입에 따라 anchors[]/referencePlane으로 나눠 담는다.
    std::vector<PointEntry> points_;
    // § 치수선 드래그/카메라 자동 재선택(2026-09-13) - 지금 points_[i].dimensionOffsetPosition이
    // 어느 월드 축("X"/"Y"/"Z") 방향으로 오프셋되어 있는지 기억해둔다. onDimensionOffsetDragged가
    // 이 축 방향으로 이동시키고, editorViewerPanel_->SetDimensionOffsetAxis()에도 이 축의
    // 단위벡터를 전달해야 뷰어가 드래그 방향을 올바르게 계산한다. 비어있으면(overall_size가
    // 아닌 일반 수동 피킹) 오프셋/드래그 기능 자체가 꺼져 있는 상태.
    std::string currentOverallSizeOffsetAxis_;
    // 사용자가 치수선을 직접 드래그해서 옮긴 적이 있으면 true - 켜져 있는 동안은 카메라
    // 회전에 따른 자동 축 재선택(ChooseOverallSizeOffsetAxis)을 건너뛰어 사용자가 잡아둔
    // 위치를 존중한다. AutoDetectOverallSizePointsIfApplicable()이 실제로 새로 계산할
    // 때마다 다시 false로 초기화된다(측정타입/축을 바꾸거나 다른 규칙을 불러오면 리셋).
    bool dimensionOffsetManuallyAdjusted_ = false;
    // § 수동 2점 축 정렬 치수선 드래그(2026-09-13) - "노란 텍스트 박스가 드래그로 안
    // 움직인다"는 재확인 요청. 전장 사이즈 자동 포인트와 달리 수동 피킹 2점은
    // dimensionOffsetPosition을 안 쓰므로(hasDimensionOffset==false), 드래그로 쌓인
    // 이동량을 이 스칼라에 따로 누적해뒀다가 RefreshMarkers()가 축 정렬 치수선을 계산할
    // 때 더해준다. 다른 규칙을 불러오거나 포인트를 다시 지정하면 0으로 리셋.
    float manualDimensionOffsetDelta_ = 0.0f;
    bool pickArmed_ = false;
    // § "지정 누르면 연달아 포인트 2개" 요청(2026-09-11) - onFacePicked가 포인트 1개를
    // 성공적으로 추가한 뒤 points_.size() < 2면, 사용자가 "지정..."을 다시 누를 필요 없이
    // 스스로 피킹 모드를 다시 켠다(true인 동안만). instance_count/min_pitch처럼 포인트
    // 1개만 필요한 드문 경우엔 "선택 포인트 삭제"로 자동 추가된 2번째 점을 지우면 된다.
    bool autoChainSecondPoint_ = false;

    // § 이미지 캡쳐 연동 - 현재 폼이 들고 있는(캡쳐했거나 기존 규칙에서 불러온) 이미지
    // 파일 경로. 비어있으면 이미지 없음.
    std::string ruleImagePath_;

    QTableWidget* table_; // 좌측 - 저장된 규칙 목록

    // § 대표 인치 선택(2026-09-11) - "여러 인치를 다 보여주니 화면이 헷갈리고, 어차피
    // 대표 인치 하나만 이미지로 쓸 거다"라는 피드백에 따라, 내장 뷰어는 이제 항상 이
    // 콤보에서 고른 인치 하나만 보여준다(editorViewerPanel_이 models_ 전체가 아니라
    // 이 선택 하나만 담아 다시 만들어짐 - RebuildEditorPanel 참고).
    QComboBox* targetModelCombo_ = nullptr;
    // § 라이브 뷰어 내장(2026-09-11) - "측정 이미지" 자리에 내장된, 이 다이얼로그가 직접
    // 소유하는 MultiViewportPanel. 메인 창의 viewerPanel_과는 완전히 별개 인스턴스라(같은
    // adapter_/handle을 공유할 뿐) 독립적으로 회전/팬/줌할 수 있다. RewireModels()나
    // targetModelCombo_ 선택이 바뀔 때마다 통째로 다시 만든다.
    viewer::MultiViewportPanel* editorViewerPanel_ = nullptr;
    QVBoxLayout* rightLayout_ = nullptr; // RebuildEditorPanel()이 replaceWidget()에 쓴다.
    QPushButton* captureImageButton_;
    // § 캡쳐 영역 드래그 지정(2026-09-11) - "뷰어 전체 캡쳐라 배경이 너무 많다"는 피드백에
    // 따라, captureImageButton_(현재 보이는 뷰포트 전체)과 별개로 사용자가 직접 사각형을
    // 드래그해서 원하는 영역만 잘라 캡쳐할 수 있게.
    QPushButton* captureRegionButton_;
    // § 뷰어 카메라 잠금(2026-09-11) - "캡쳐 전후 카메라 on/off"가 있으면 좋겠다는 요청.
    // 체크되면 editorViewerPanel_->setCameraLocked(true)로 좌클릭 회전/우클릭 팬/휠 줌을
    // 막는다(피킹은 그대로 됨). RebuildEditorPanel()이 패널을 새로 만들 때마다 이 체크
    // 상태를 다시 적용해준다(새 패널은 항상 잠금 해제로 시작하므로).
    QCheckBox* cameraLockCheck_ = nullptr;

    QLineEdit* name_;
    // § Point(부위) = 치수 이니셜(2026-09-11) - "Point 부위는 측정 2개 포인트가 만들어낸
    // 치수 자체의 이름 이니셜로, 사용자가 직접 입력하게 해달라"는 요청. 예전엔 CTQ
    // 코드(D 등)였는데 이제 그 역할과 합쳐졌다 - "3) 관리 항목" 표의 "Point(부위)" 열에
    // 이 입력칸이 직접 꽂혀 있다(Rule::ctqCode에 저장).
    QLineEdit* ctqCode_;
    // § "종류" 분류(2026-09-11, 관리항목→종류로 개명) - Dimension/Height/Angle 등 -
    // 포인트별이 아니라 규칙 전체에 하나(사용자 확인 완료, Rule::checkPointCategory).
    // "3) 관리 항목" 표의 "종류" 열에 이 드롭다운이 직접 꽂혀 있다.
    QComboBox* checkPointCategory_;
    // § 포인트 진행 상태 표시(2026-09-11) - "포인트 0/2 선택됨" 같은 간단한 안내 텍스트.
    // 예전엔 A/B 클릭형 링크였는데, "지정 누르면 2개를 연달아 찍는다"는 새 플로우에선
    // 개별 포인트를 따로 가리킬 일이 없어져 단순 텍스트로 줄였다.
    QLabel* pointProgressLabel_;

    QTableWidget* pointsTable_; // "3) 관리 항목" - 지금 규칙(치수) 요약, 항상 1행
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
    // § 같은 캡쳐에서 여러 CTQ 항목 만들기 - onSaveAsClicked() 참고.
    QPushButton* saveAsButton_;
};

} // namespace ui
