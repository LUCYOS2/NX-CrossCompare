#pragma once

#include <QMainWindow>

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "database/Database.h"
#include "export/ComparisonExporter.h"
#include "geometry/IGeometryAdapter.h"
#include "viewer/Camera.h"

class QTableWidget;

namespace viewer {
class MultiViewportPanel;
}

namespace ui {

class RuleEditorDialog;

// Phase2: 중앙 영역은 MultiViewportPanel(Mock 데이터, 6개 인치)로 채워짐 - 뷰어 공간을
// 최대한 넓게 쓰기 위해 좌/우 메뉴는 도킹 패널이 아니라 상단 메뉴바 한 줄로 통합했다.
// Phase4/4b: 하단 영역은 내장 규칙(BuiltInRules) + 사용자가 추가한 규칙(DB 저장)을
// RuleEngine으로 평가한 결과로 채워짐. "규칙" 헤더를 클릭하면 표시할 항목을
// 체크박스로 고를 수 있다 (hiddenRuleNames_에 없는 것만 테이블에 표시).
// Phase5: 하단 영역에 Excel Export 버튼 추가 (CSV로 저장, 서식은 Python이 담당).
// 규칙 추가(1차 버전): Face/Point 피킹 대신 폼(RuleEditorDialog)으로 anchor_type/
// selector를 입력 - 인치 하나에서 anchor_type 기반으로 정의하면 RuleEngine이
// 나머지 인치에도 자동 적용한다(별도 처리 불필요, Rule 구조 자체가 이미 그렇게 동작).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void exportComparisonCsv();
    void onAddRuleClicked();
    void onRuleHeaderClicked(int section);
    void onImportStepClicked();
    void onCaptureImageClicked();
    void onSearchAnchorsClicked();

private:
    void setupMenuBar();
    void setupCentralViewer();
    void setupComparisonTable();
    void refreshComparisonTable();
    // 화면설정에서 고른 렌더모드/조작모드를 지금 패널에 적용 - setupCentralViewer()가
    // STEP 재로드마다 패널을 새로 만들기 때문에, 재로드 후에도 사용자가 고른 설정이
    // 유지되도록 매번 다시 걸어준다(currentRenderMode_/currentSyncedManipulation_ 참고).
    void applyDisplaySettingsToCurrentPanel();
    // setupCentralViewer()와 onSearchAnchorsClicked()가 둘 다 "인치별 라벨+handle
    // 목록"이 필요해서 공유하는 헬퍼.
    std::vector<std::pair<std::string, geometry::ModelHandle>> BuildLoadedModelList() const;

    std::unique_ptr<geometry::IGeometryAdapter> adapter_;
    database::Database db_;
    int projectId_ = 0;

    // 인치 여러 개를 한번에 불러올 수 있게 됨(사용자 피드백: "43/65/85 FRAME류를
    // 한번에 입고" + "인치 지정 없이 불러오고 싶다"). inch<=0은 파일명에서 자동인식
    // 못한 "미지정" 상태 - ImportInchDialog에서 직접 입력해야 뷰어/비교표에 반영된다.
    // handle은 onImportStepClicked에서 딱 한 번만 LoadModel한 결과를 들고 있다가
    // setupCentralViewer/refreshComparisonTable이 재사용한다 - 예전엔 같은 파일을
    // 화면당 최대 3번(검증/뷰어/비교표) 다시 파싱해서 여러 인치를 한번에 불러오면
    // 그만큼 무거워지는 문제가 있었다.
    struct LoadedInch {
        int inch = 0;
        std::string filePath;
        geometry::ModelHandle handle = geometry::kInvalidModelHandle;
    };
    std::vector<LoadedInch> inchFiles_;

    QTableWidget* comparisonTable_ = nullptr;
    std::vector<report::RuleReport> lastReports_;
    std::set<std::string> hiddenRuleNames_;

    // § 화면설정 - setupCentralViewer()가 STEP 재로드마다 새 MultiViewportPanel을 만들기
    // 때문에, 마지막으로 고른 화면 모드/조작 모드를 여기 기억해뒀다가 재로드 직후 다시
    // 적용한다(applyDisplaySettingsToCurrentPanel). 안 그러면 재로드할 때마다 기본값으로
    // 되돌아가 사용자가 고른 설정이 사라진다 - 예전에 단축키에서 겪었던 것과 같은 종류의
    // 함정이라 처음부터 이렇게 설계함.
    viewer::MultiViewportPanel* viewerPanel_ = nullptr;
    viewer::RenderMode currentRenderMode_ = viewer::RenderMode::SolidEdge;
    bool currentSyncedManipulation_ = true;

    // § 3D 클릭 피킹 - 비모달로 바뀌면서 동시에 두 개 뜨는 걸 막고, STEP 재로드 시
    // RewireViewerPanel로 다시 연결해주기 위해 열려있는 인스턴스를 계속 들고 있는다.
    RuleEditorDialog* activeRuleDialog_ = nullptr;
};

} // namespace ui
