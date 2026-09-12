#include "ui/ImportInchDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace ui {

QString FormatInchLabel(int inch) {
    if (inch == kAllInchValue) {
        return "ALL";
    }
    return QString("%1\"").arg(inch);
}

namespace {

// 폴더/파일명 어딘가에 들어있는 "그럴듯한 TV 인치" 숫자를 찾는다. 20~100 범위의
// 2~3자리 숫자만 후보로 보고, 후보가 정확히 하나면 채택 - 0개/여러 개(애매함)면
// 0(미지정)으로 남겨 사용자가 ImportInchDialog에서 직접 채우게 한다.
int GuessInchFromPath(const QString& path) {
    static const QRegularExpression re("\\d{2,3}");
    auto it = re.globalMatch(path);
    int found = 0;
    int matchCount = 0;
    while (it.hasNext()) {
        const auto match = it.next();
        const int value = match.captured(0).toInt();
        if (value >= 20 && value <= 100) {
            ++matchCount;
            found = value;
        }
    }
    return matchCount == 1 ? found : 0;
}

} // namespace

ImportInchDialog::ImportInchDialog(const QStringList& filePaths, QWidget* parent)
    : QDialog(parent), filePaths_(filePaths) {
    setWindowTitle("불러올 인치 확인");
    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(
        "파일명에서 인치를 자동으로 추정했습니다. 틀렸거나 비어있으면(미지정) 직접 입력하세요.",
        this));

    // § 공용부품(ALL) 열 추가(2026-09-12) - "공용부품이면 별도 인치 숫자 입력 없이
    // ALL(전 인치)로 인식해달라"는 요청. 체크하면 인치 스핀박스는 잠기고(의미 없어지므로)
    // Result()가 kAllInchValue를 낸다.
    table_ = new QTableWidget(filePaths.size(), 3, this);
    table_->setHorizontalHeaderLabels({"파일", "인치", "공용(ALL)"});

    for (int row = 0; row < filePaths.size(); ++row) {
        const QString& path = filePaths[row];
        auto* nameItem = new QTableWidgetItem(QFileInfo(path).fileName());
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        nameItem->setToolTip(path);
        table_->setItem(row, 0, nameItem);

        auto* spin = new QSpinBox(table_);
        spin->setRange(0, 999);
        spin->setSpecialValueText("미지정");
        spin->setValue(GuessInchFromPath(path));
        // § 클릭하면 바로 숫자를 칠 수 있게 - 포커스 들어올 때 전체 선택(아래 eventFilter).
        // QAbstractSpinBox::lineEdit()은 protected라 밖에서 못 부르므로 findChild로 접근.
        if (auto* lineEdit = spin->findChild<QLineEdit*>()) {
            lineEdit->installEventFilter(this);
        }
        table_->setCellWidget(row, 1, spin);

        // 체크박스를 셀 중앙에 두려면 컨테이너 위젯 하나로 감싸야 한다(Qt 관례).
        auto* allCheck = new QCheckBox(table_);
        auto* allCheckContainer = new QWidget(table_);
        auto* allCheckLayout = new QHBoxLayout(allCheckContainer);
        allCheckLayout->addWidget(allCheck);
        allCheckLayout->setAlignment(Qt::AlignCenter);
        allCheckLayout->setContentsMargins(0, 0, 0, 0);
        connect(allCheck, &QCheckBox::toggled, spin, [spin](bool checked) {
            // 체크되면 인치 숫자는 의미가 없어지므로 잠그고 "미지정" 표시로 되돌린다.
            spin->setEnabled(!checked);
            if (checked) {
                spin->setValue(spin->minimum());
            }
        });
        table_->setCellWidget(row, 2, allCheckContainer);
    }
    // 순서 중요: 인치(스핀박스) 컬럼 너비를 먼저 내용에 맞게 고정한 다음 파일명
    // 컬럼을 Stretch로 설정해야, 스핀박스가 화면 밖으로 밀려나지 않고 남는 공간을
    // 파일명 컬럼이 가져간다 (반대 순서면 Stretch가 전체 폭을 먼저 차지해버림).
    table_->resizeColumnToContents(1);
    table_->resizeColumnToContents(2);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(table_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    resize(520, 420);
}

std::vector<std::pair<int, std::string>> ImportInchDialog::Result() const {
    std::vector<std::pair<int, std::string>> result;
    for (int row = 0; row < filePaths_.size(); ++row) {
        auto* spin = qobject_cast<QSpinBox*>(table_->cellWidget(row, 1));
        int inch = spin ? spin->value() : 0;
        // § 공용부품(ALL) - 체크박스가 켜져 있으면 스핀박스 값과 무관하게 kAllInchValue.
        if (auto* allCheckContainer = table_->cellWidget(row, 2)) {
            if (auto* allCheck = allCheckContainer->findChild<QCheckBox*>(); allCheck && allCheck->isChecked()) {
                inch = kAllInchValue;
            }
        }
        result.push_back({inch, filePaths_[row].toStdString()});
    }
    // 소형 -> 대형 (요청사항). ALL은 실제 인치들 뒤, 미지정(0)은 맨 뒤로 보낸다
    // (kAllInchValue=1000이 어떤 실제 인치보다도 크고 미지정용 정렬키 100000보다는
    // 작아서 자연스럽게 그 사이에 온다).
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        const int ka = a.first == 0 ? 100000 : a.first;
        const int kb = b.first == 0 ? 100000 : b.first;
        return ka < kb;
    });
    return result;
}

bool ImportInchDialog::eventFilter(QObject* watched, QEvent* event) {
    auto* lineEdit = qobject_cast<QLineEdit*>(watched);
    if (!lineEdit) {
        return QDialog::eventFilter(watched, event);
    }
    // § "미지정 누르면 그 글자가 아예 없어지게" 재요청(2026-09-12) - 전체선택만으로는
    // "미지정" 글자가 여전히 화면에 보여서(선택된 상태로) 부족하다는 피드백. 클릭(포커스)
    // 순간 글자 자체를 지워서 빈 칸에 바로 숫자를 치게 하고, 아무것도 안 치고 포커스를
    // 벗어나면(FocusOut) 값을 다시 최솟값(0)으로 되돌려 "미지정"이 다시 보이게 한다.
    if (event->type() == QEvent::FocusIn) {
        // clear()도 FocusIn 안에서 바로 부르면 스핀박스 내부 로직이 텍스트를 다시 채워
        // 넣는 경우가 있다 - 이벤트 처리가 끝난 뒤(0ms 뒤)로 미루면 확실히 먹는다(Qt에서
        // 흔히 쓰는 우회법).
        QTimer::singleShot(0, lineEdit, &QLineEdit::clear);
    } else if (event->type() == QEvent::FocusOut) {
        if (lineEdit->text().trimmed().isEmpty()) {
            if (auto* spin = qobject_cast<QSpinBox*>(lineEdit->parent())) {
                spin->setValue(spin->minimum()); // "미지정" 특수값 텍스트로 되돌림
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace ui
