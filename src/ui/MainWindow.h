#pragma once

#include <QMainWindow>

#include <memory>
#include <vector>

#include "database/Database.h"
#include "export/ComparisonExporter.h"
#include "geometry/IGeometryAdapter.h"

class QTableWidget;

namespace ui {

// Phase2: 중앙 영역은 MultiViewportPanel(Mock 데이터, 6개 인치)로 채워짐.
// Phase4/4b: 하단 영역은 내장 규칙(BuiltInRules) + 사용자가 추가한 규칙(DB 저장)을
// RuleEngine으로 평가한 결과로 채워짐.
// Phase5: 하단 영역에 Excel Export 버튼 추가 (CSV로 저장, 서식은 Python이 담당).
// 규칙 추가(1차 버전): Face/Point 피킹 대신 폼(RuleEditorDialog)으로 anchor_type/
// selector를 입력 - 인치 하나에서 anchor_type 기반으로 정의하면 RuleEngine이
// 나머지 인치에도 자동 적용한다(별도 처리 불필요, Rule 구조 자체가 이미 그렇게 동작).
// 개발계획_v2.md §10 UI 구조 참고: 좌측/중앙/우측/하단 4분할.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void exportComparisonCsv();
    void onAddRuleClicked();

private:
    void setupLeftPanel();
    void setupRightPanel();
    void setupCentralViewer();
    void setupComparisonTable();
    void refreshComparisonTable();

    std::unique_ptr<geometry::IGeometryAdapter> adapter_;
    database::Database db_;
    int projectId_ = 0;

    QTableWidget* comparisonTable_ = nullptr;
    std::vector<report::RuleReport> lastReports_;
};

} // namespace ui
