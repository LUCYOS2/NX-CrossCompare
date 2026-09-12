#pragma once

#include <QDialog>

#include "geometry/IGeometryAdapter.h"

#include <string>
#include <utility>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace ui {

// "포인트 검색" 탭 - anchor_type(+부품명/지름 필터)을 입력하면 현재 로드된 인치 중
// 하나를 골라 실제로 몇 개/어디에 매칭되는지 미리 보여주는 탐색기. 규칙을 저장하기
// 전에 "이 anchor_type/지름 조합이 실제로 뭘 잡는지" 확인하는 용도 - RuleEngine이
// 쓰는 것과 같은 지름 필터(rule::FilterByDiameter)를 그대로 재사용해서, 여기서 보이는
// 결과가 곧 규칙을 저장했을 때 실제로 매칭될 후보와 일치한다.
//
// § 규칙 관리 통합(2026-09-08) - RuleEditorDialog의 Anchor A/B "검색..." 버튼이 이
// 다이얼로그를 현재 입력값으로 미리 채워서 연다. "이 조건 적용"을 누르면 accept()로
// 닫히고, 호출 쪽이 AnchorType()/PartName()/Diameter()로 값을 읽어가 Anchor 입력칸에
// 반영한다 - 검색 폼 자체가 이미 anchor_type/part_name/지름이라 별도 "선택" UI 없이
// 입력값을 그대로 돌려주는 것으로 충분하다.
class AnchorSearchDialog : public QDialog {
    Q_OBJECT

public:
    AnchorSearchDialog(geometry::IGeometryAdapter* adapter,
                        const std::vector<std::pair<std::string, geometry::ModelHandle>>& models,
                        QWidget* parent = nullptr);

    // Anchor A/B "검색..." 버튼에서 연동할 때 현재 입력값으로 미리 채워서 연다.
    void SetInitialCriteria(const std::string& anchorType, const std::string& partName, double diameterMm);

    std::string AnchorType() const;
    std::string PartName() const;
    double Diameter() const;
    // § 리스트 항목 분류(2026-09-11) - "INCH(적용 인치)" 열을 채우려면 검색으로 추가한
    // 포인트도 어느 인치 대상이었는지 알아야 한다.
    geometry::ModelHandle SelectedModelHandle() const;

private slots:
    void onSearchClicked();

private:
    geometry::IGeometryAdapter* adapter_;
    std::vector<std::pair<std::string, geometry::ModelHandle>> models_;

    QComboBox* modelCombo_;
    QLineEdit* anchorType_;
    QLineEdit* partName_;
    QDoubleSpinBox* diameter_;
    QLabel* countLabel_;
    QTableWidget* resultsTable_;
};

} // namespace ui
