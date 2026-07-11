#pragma once

#include <QMainWindow>

#include <memory>
#include <vector>

#include "export/ComparisonExporter.h"
#include "geometry/IGeometryAdapter.h"

namespace ui {

// Phase2: 중앙 영역은 MultiViewportPanel(Mock 데이터, 6개 인치)로 채워짐.
// Phase4/4b: 하단 영역은 내장 규칙 5종(BuiltInRules)을 RuleEngine으로 평가한
// 결과로 채워짐.
// Phase5: 하단 영역에 Excel Export 버튼 추가 (CSV로 저장, 서식은 Python이 담당).
// 개발계획_v2.md §10 UI 구조 참고: 좌측/중앙/우측/하단 4분할.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void exportComparisonCsv();

private:
    void setupLeftPanel();
    void setupRightPanel();
    void setupCentralViewer();
    void setupComparisonTable();

    std::unique_ptr<geometry::IGeometryAdapter> adapter_;
    std::vector<report::RuleReport> lastReports_;
};

} // namespace ui
