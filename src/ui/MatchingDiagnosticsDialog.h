#pragma once

#include <QDialog>
#include <QString>

#include <vector>

#include "geometry/IGeometryAdapter.h"
#include "rule/MatchingDiagnostics.h"

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace ui {

// § 매칭 진단 메뉴(2026-11-04) - "앱으로 검증할 수 있게 해달라"는 요청. 예전엔 명령줄 도구
// (diag_measure_matching)에 STEP 파일을 넘겨야 했는데, 이제 앱에서 불러온 인치들로 바로 돌린다.
// 기준 인치를 고르고 실행하면 rule::RunMatchingDiagnostics 결과를 표(종류 × 인치)와 실패 사유
// 목록으로 보여주고, "결과 복사"로 좌표 없이 숫자/사유만 텍스트로 가져갈 수 있다(임계값 조정용).
class MatchingDiagnosticsDialog : public QDialog {
    Q_OBJECT

public:
    struct Model {
        QString label; // "55\"  파일명.stp"
        geometry::ModelHandle handle = geometry::kInvalidModelHandle;
    };

    MatchingDiagnosticsDialog(const geometry::IGeometryAdapter& adapter, std::vector<Model> models,
                              QWidget* parent = nullptr);

private slots:
    void onRunClicked();
    void onCopyClicked();

private:
    void ShowReport(const rule::MatchingDiagnosticsReport& report, int sourceIndex);

    const geometry::IGeometryAdapter& adapter_;
    std::vector<Model> models_;
    QComboBox* sourceCombo_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QPushButton* copyButton_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPlainTextEdit* reasonsText_ = nullptr;
    QString reportText_; // "결과 복사"용 - 표와 사유를 합친 평문
};

} // namespace ui
