#include "ui/MatchingDiagnosticsDialog.h"

#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <utility>

namespace ui {

namespace {

QTableWidgetItem* NumberItem(int value) {
    auto* item = new QTableWidgetItem(QString::number(value));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

void AppendReasons(QString& text, const rule::DiagnosticTally& tally) {
    for (const auto& [reason, count] : tally.reasons) {
        text += QString("      - %1: %2\n").arg(QString::fromStdString(reason)).arg(count);
    }
}

} // namespace

MatchingDiagnosticsDialog::MatchingDiagnosticsDialog(
    const geometry::IGeometryAdapter& adapter, std::vector<Model> models, QWidget* parent)
    : QDialog(parent), adapter_(adapter), models_(std::move(models)) {
    setWindowTitle("매칭 진단");
    resize(980, 640);

    auto* intro = new QLabel(
        "기준 인치 도면의 모든 구멍 · 꼭짓점 · 모서리 중간점 · 원 중심 · 평면을 \"그 자리를 클릭했다\"고 보고, "
        "규칙을 만들 때와 같은 방법으로 기준 인치와 나머지 인치에서 다시 찾아봅니다.\n"
        "· 기준 인치 \"오답\"은 반드시 0이어야 합니다(다른 형상을 잡아 틀린 값을 내는 경우).\n"
        "· 다른 인치의 \"실패\" 사유가 한쪽에 몰려 있으면 매칭 기준값을 조정해야 한다는 신호입니다. "
        "\"결과 복사\"로 숫자와 사유만(좌표 없이) 가져갈 수 있습니다.",
        this);
    intro->setWordWrap(true);

    sourceCombo_ = new QComboBox(this);
    for (const auto& model : models_) {
        sourceCombo_->addItem(model.label);
    }
    runButton_ = new QPushButton("실행", this);
    connect(runButton_, &QPushButton::clicked, this, &MatchingDiagnosticsDialog::onRunClicked);
    auto* sourceRow = new QHBoxLayout();
    sourceRow->addWidget(new QLabel("기준 인치:", this));
    sourceRow->addWidget(sourceCombo_, 1);
    sourceRow->addWidget(runButton_);

    summaryLabel_ = new QLabel("기준 인치를 고르고 \"실행\"을 누르세요. 큰 도면은 몇 분 걸릴 수 있습니다.", this);
    summaryLabel_->setWordWrap(true);

    table_ = new QTableWidget(this);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);

    reasonsText_ = new QPlainTextEdit(this);
    reasonsText_->setReadOnly(true);
    reasonsText_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    reasonsText_->setPlaceholderText("실패 사유별 개수가 여기에 나옵니다.");

    copyButton_ = new QPushButton("결과 복사", this);
    copyButton_->setEnabled(false);
    connect(copyButton_, &QPushButton::clicked, this, &MatchingDiagnosticsDialog::onCopyClicked);
    auto* closeButton = new QPushButton("닫기", this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    buttonRow->addWidget(copyButton_);
    buttonRow->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(sourceRow);
    layout->addWidget(summaryLabel_);
    layout->addWidget(table_, 3);
    layout->addWidget(new QLabel("실패 사유", this));
    layout->addWidget(reasonsText_, 2);
    layout->addLayout(buttonRow);
}

void MatchingDiagnosticsDialog::onRunClicked() {
    const int sourceIndex = sourceCombo_->currentIndex();
    if (sourceIndex < 0) {
        return;
    }
    std::vector<geometry::ModelHandle> others;
    for (int i = 0; i < static_cast<int>(models_.size()); ++i) {
        if (i != sourceIndex) {
            others.push_back(models_[static_cast<size_t>(i)].handle);
        }
    }

    // 계산은 이 스레드에서 하고, 검사 한 건마다 진행 창을 갱신하면서 이벤트를 처리한다(취소 버튼 포함).
    // 어댑터가 스레드 안전하지 않아서 별도 스레드로 돌리지 않는다.
    QProgressDialog progressDialog("매칭 진단 중...", "취소", 0, 100, this);
    progressDialog.setWindowModality(Qt::WindowModal);
    progressDialog.setMinimumDuration(0);
    progressDialog.setValue(0);
    runButton_->setEnabled(false);
    const auto report = rule::RunMatchingDiagnostics(
        adapter_, models_[static_cast<size_t>(sourceIndex)].handle, others,
        [&progressDialog](std::size_t done, std::size_t total) {
            progressDialog.setMaximum(static_cast<int>(total));
            progressDialog.setValue(static_cast<int>(done));
            QApplication::processEvents();
            return !progressDialog.wasCanceled();
        });
    progressDialog.reset();
    runButton_->setEnabled(true);
    ShowReport(report, sourceIndex);
}

void MatchingDiagnosticsDialog::ShowReport(const rule::MatchingDiagnosticsReport& report, int sourceIndex) {
    std::vector<int> otherIndices;
    for (int i = 0; i < static_cast<int>(models_.size()); ++i) {
        if (i != sourceIndex) {
            otherIndices.push_back(i);
        }
    }
    const QString sourceLabel = models_[static_cast<size_t>(sourceIndex)].label;

    QStringList headers{"종류", "검사 수", "기준 인치 정답", "실패", "오답"};
    for (int index : otherIndices) {
        const QString label = models_[static_cast<size_t>(index)].label.section("  ", 0, 0);
        headers << QString("%1 값을 냄").arg(label) << QString("%1 실패").arg(label);
    }
    table_->clear();
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);
    table_->setRowCount(static_cast<int>(report.sections.size()));

    int totalWrong = 0;
    reportText_ = QString("매칭 진단 - 기준: %1\n").arg(sourceLabel);
    QString reasons;
    for (int row = 0; row < static_cast<int>(report.sections.size()); ++row) {
        const auto& section = report.sections[static_cast<size_t>(row)];
        const QString label = QString::fromStdString(section.label);
        table_->setItem(row, 0, new QTableWidgetItem(label));
        table_->setItem(row, 1, new QTableWidgetItem(
                                    QString("%1 / %2").arg(section.sampled).arg(section.candidateCount)));
        table_->setItem(row, 2, NumberItem(section.self.correct));
        table_->setItem(row, 3, NumberItem(section.self.failed));
        auto* wrongItem = NumberItem(section.self.wrong);
        if (section.self.wrong > 0) {
            wrongItem->setBackground(QBrush(QColor(255, 150, 150)));
        }
        table_->setItem(row, 4, wrongItem);
        totalWrong += section.self.wrong;

        reportText_ += QString("\n%1 (검사 %2 / 후보 %3)\n  기준 인치: 정답 %4, 실패 %5, 오답 %6\n")
                           .arg(label)
                           .arg(section.sampled)
                           .arg(section.candidateCount)
                           .arg(section.self.correct)
                           .arg(section.self.failed)
                           .arg(section.self.wrong);
        AppendReasons(reportText_, section.self);
        if (!section.self.reasons.empty()) {
            reasons += QString("[%1] 기준 인치\n").arg(label);
            AppendReasons(reasons, section.self);
        }
        for (size_t i = 0; i < section.others.size() && i < otherIndices.size(); ++i) {
            const auto& tally = section.others[i];
            const int column = 5 + static_cast<int>(i) * 2;
            table_->setItem(row, column, NumberItem(tally.correct));
            table_->setItem(row, column + 1, NumberItem(tally.failed));
            const QString otherLabel = models_[static_cast<size_t>(otherIndices[i])].label;
            reportText_ += QString("  %1: 값을 냄 %2, 실패 %3\n").arg(otherLabel).arg(tally.correct).arg(tally.failed);
            AppendReasons(reportText_, tally);
            if (!tally.reasons.empty()) {
                reasons += QString("[%1] %2\n").arg(label, otherLabel);
                AppendReasons(reasons, tally);
            }
        }
    }
    table_->resizeColumnsToContents();
    reasonsText_->setPlainText(reasons);

    QString summary = report.cancelled ? QString("취소됨 - 그때까지의 결과입니다. ") : QString();
    summary += totalWrong == 0 ? QString("기준 인치 오답 0건 ✓") : QString("⚠ 기준 인치 오답 %1건 - 확인 필요").arg(totalWrong);
    summary += QString("   (소요 시간 %1초)").arg(report.seconds, 0, 'f', 1);
    summaryLabel_->setText(summary);
    reportText_ += QString("\n%1\n").arg(summary);
    copyButton_->setEnabled(true);
}

void MatchingDiagnosticsDialog::onCopyClicked() {
    QApplication::clipboard()->setText(reportText_);
    summaryLabel_->setText(summaryLabel_->text().section("   [", 0, 0) + "   [클립보드에 복사됨]");
}

} // namespace ui
