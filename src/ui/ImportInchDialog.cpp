#include "ui/ImportInchDialog.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace ui {

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

    table_ = new QTableWidget(filePaths.size(), 2, this);
    table_->setHorizontalHeaderLabels({"파일", "인치"});

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
        table_->setCellWidget(row, 1, spin);
    }
    // 순서 중요: 인치(스핀박스) 컬럼 너비를 먼저 내용에 맞게 고정한 다음 파일명
    // 컬럼을 Stretch로 설정해야, 스핀박스가 화면 밖으로 밀려나지 않고 남는 공간을
    // 파일명 컬럼이 가져간다 (반대 순서면 Stretch가 전체 폭을 먼저 차지해버림).
    table_->resizeColumnToContents(1);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
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
        const int inch = spin ? spin->value() : 0;
        result.push_back({inch, filePaths_[row].toStdString()});
    }
    // 소형 -> 대형 (요청사항). 미지정(0)은 뒤로 보낸다.
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        const int ka = a.first == 0 ? 100000 : a.first;
        const int kb = b.first == 0 ? 100000 : b.first;
        return ka < kb;
    });
    return result;
}

} // namespace ui
