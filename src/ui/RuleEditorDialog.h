#pragma once

#include <QDialog>

#include "rule/Rule.h"

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QWidget;

namespace ui {

// 코딩 없이 규칙을 만드는 폼 (1차 버전 - Face/Point 피킹은 2차 버전으로 미룸,
// 개발계획 논의 참고). 여기서 만든 필드가 anchor_type/part_name/selector 등
// rule::Rule 스키마 그대로이므로, 나중에 피킹 UI가 추가되면 이 폼을 자동으로
// 채우는 방식으로 확장하면 된다.
class RuleEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit RuleEditorDialog(QWidget* parent = nullptr);

    // Accepted 이후 호출. anchors/selector/referenceFrame을 폼 입력으로 채운
    // rule::Rule을 반환한다 (id=0, 저장 전 상태).
    rule::Rule BuildRule() const;

private slots:
    void onMeasurementTypeChanged(int index);

private:
    QLineEdit* name_;
    QComboBox* measurementType_;

    QLineEdit* anchorAType_;
    QLineEdit* anchorAPart_;
    QWidget* anchorBGroup_;
    QLineEdit* anchorBType_;
    QLineEdit* anchorBPart_;
    QWidget* planeRefGroup_;
    QLineEdit* planeType_;
    QLineEdit* planePart_;

    QComboBox* selector_;
    QComboBox* projection_;
    QDoubleSpinBox* tolerancePlus_;
    QDoubleSpinBox* toleranceMinus_;
};

} // namespace ui
