#include "ui/ViewerDiagnosticsDialog.h"

#include "viewer/DiagnosticsLog.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>

namespace ui {

namespace {

QString KeyName(const QKeyEvent* key) {
    if (key->key() == 0 || key->key() == Qt::Key_unknown) {
        return "(Qt가 모르는 키)";
    }
    const QString name = QKeySequence(key->key()).toString(QKeySequence::PortableText);
    return name.isEmpty() ? QString("0x%1").arg(key->key(), 0, 16) : name;
}

// 예전 Qt(6.2 미만)에도 있는 함수만 쓴다 - QFlags::toInt()는 6.2부터라 회사PC Qt에 없을 수 있다.
QString ModifierText(Qt::KeyboardModifiers modifiers) {
    QStringList names;
    if (modifiers & Qt::ShiftModifier) {
        names << "Shift";
    }
    if (modifiers & Qt::ControlModifier) {
        names << "Ctrl";
    }
    if (modifiers & Qt::AltModifier) {
        names << "Alt";
    }
    if (modifiers & Qt::MetaModifier) {
        names << "Win";
    }
    if (modifiers & Qt::KeypadModifier) {
        names << "숫자패드";
    }
    return names.isEmpty() ? QString() : QString(", 조합키 %1").arg(names.join('+'));
}

QString Describe(const QKeyEvent* key, const QObject* receiver) {
    QString text = key->text();
    QString shown;
    for (const QChar c : text) {
        shown += c.isPrint() ? QString(c) : QString("\\x%1").arg(static_cast<uint>(c.unicode()), 2, 16, QChar('0'));
    }
    return QString("%1  (Qt 0x%2, VK 0x%3, 스캔 0x%4, 글자 '%5'%6, 받은 곳 %7)")
        .arg(KeyName(key))
        .arg(key->key(), 0, 16)
        .arg(key->nativeVirtualKey(), 0, 16)
        .arg(key->nativeScanCode(), 0, 16)
        .arg(shown)
        .arg(ModifierText(key->modifiers()))
        .arg(receiver ? receiver->metaObject()->className() : "?");
}

// 같은 키 누름 한 번이 ShortcutOverride -> KeyPress(위젯 -> 부모로 전달)처럼 여러 번 보이므로 한 줄만 남긴다.
ulong g_lastTimestamp = 0;
int g_lastKey = -1;

} // namespace

ViewerDiagnosticsDialog::ViewerDiagnosticsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("뷰어·키 입력 점검");
    resize(820, 640);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* guide = new QLabel(
        "이 창을 띄워 둔 채로 메인 화면(또는 형상 프리셋 창)의 3D 화면을 한 번 클릭하고 F1, F2, F3, F4, H, F, I를 "
        "눌러 보세요. 아래에 키가 앱까지 왔는지, 뷰어 단축키로 실행됐는지가 찍힙니다. "
        "\"결과 복사\"로 붙여 넣어 보내 주세요(도면 좌표/치수는 들어가지 않습니다).",
        this);
    guide->setWordWrap(true);

    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    environmentText_ = new QPlainTextEdit(this);
    environmentText_->setReadOnly(true);
    environmentText_->setFont(mono);
    environmentText_->setMaximumHeight(190);

    keyLog_ = new QPlainTextEdit(this);
    keyLog_->setReadOnly(true);
    keyLog_->setFont(mono);
    keyLog_->setPlaceholderText("(아직 받은 키가 없습니다)");

    auto* refreshButton = new QPushButton("환경 다시 읽기", this);
    connect(refreshButton, &QPushButton::clicked, this, &ViewerDiagnosticsDialog::refreshEnvironment);
    auto* clearButton = new QPushButton("키 기록 지우기", this);
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        keyLog_->clear();
        keyLines_ = 0;
    });
    auto* openLogButton = new QPushButton("진단 로그 폴더 열기", this);
    connect(openLogButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(viewer::diag::LogFilePath()).absolutePath()));
    });
    auto* copyButton = new QPushButton("결과 복사", this);
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        refreshEnvironment();
        QApplication::clipboard()->setText("[실행 환경]\n" + environmentText_->toPlainText() + "\n\n[키 입력 기록]\n" +
                                           keyLog_->toPlainText());
    });

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(refreshButton);
    buttonRow->addWidget(clearButton);
    buttonRow->addWidget(openLogButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(copyButton);
    auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonRow->addWidget(closeBox);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(guide);
    layout->addWidget(new QLabel("실행 환경", this));
    layout->addWidget(environmentText_);
    layout->addWidget(new QLabel("키 입력 기록 (이 창이 열려 있는 동안 앱 전체)", this));
    layout->addWidget(keyLog_, 1);
    layout->addLayout(buttonRow);

    refreshEnvironment();
    connect(viewer::diag::Hub::instance(), &viewer::diag::Hub::viewerShortcutTriggered, this,
            [this](const QString& keyName, const QString& action, const QString& via) {
                appendKeyLine(QString("    => 뷰어 단축키 실행: %1 -> %2 [%3]").arg(keyName, action, via));
            });
    connect(viewer::diag::Hub::instance(), &viewer::diag::Hub::glInfoReady, this,
            &ViewerDiagnosticsDialog::refreshEnvironment);
    qApp->installEventFilter(this);
}

ViewerDiagnosticsDialog::~ViewerDiagnosticsDialog() {
    qApp->removeEventFilter(this);
}

void ViewerDiagnosticsDialog::refreshEnvironment() {
    environmentText_->setPlainText(viewer::diag::EnvironmentSummary() + "\n진단 로그: " +
                                   QDir::toNativeSeparators(viewer::diag::LogFilePath()));
}

void ViewerDiagnosticsDialog::appendKeyLine(const QString& line) {
    if (keyLines_ >= 400) {
        return; // 너무 길어지지 않게
    }
    ++keyLines_;
    keyLog_->appendPlainText(QTime::currentTime().toString("HH:mm:ss.zzz") + "  " + line);
}

bool ViewerDiagnosticsDialog::eventFilter(QObject* watched, QEvent* event) {
    if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) && watched->isWidgetType()) {
        const auto* key = static_cast<const QKeyEvent*>(event);
        if (key->timestamp() != g_lastTimestamp || key->key() != g_lastKey) {
            g_lastTimestamp = key->timestamp();
            g_lastKey = key->key();
            const QWidget* window = static_cast<QWidget*>(watched)->window();
            appendKeyLine(QString("키 받음: %1, 창 \"%2\"").arg(Describe(key, watched),
                                                              window ? window->windowTitle() : QString()));
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace ui
