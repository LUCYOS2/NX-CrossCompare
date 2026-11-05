// § 뷰어 입력 점검(2026-11-05) - "3D 뷰어 단축키가 하나도 안 먹는다", "형상 프리셋에서 줌/회전하다 멈추고
// 창이 꺼진다" 리포트. 실제 STEP을 불러와 (1) 메인 화면처럼 QMainWindow 중앙에 둔 뷰어, (2) 실제
// 형상 프리셋 창(ShapePresetDialog)에 운영체제 키/마우스 입력을 그대로 흉내 내서(QWindowSystemInterface -
// QShortcut 판정까지 실제 키 입력과 같은 길을 탄다) 단축키가 먹는지, 드래그/휠/호버 한 번에 얼마나
// 걸리는지 찍는다. CTest에는 안 넣는다 - 창이 뜨고 실제 STEP 경로가 필요하다.
//
//   diag_viewer_input <a.stp> [b.stp ...]
//
// 끝까지 가면 "viewer input OK"(단축키 실패가 있으면 "viewer input FAILED")를 찍는다.

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>
#include <utility>
#include <vector>

#include "database/Database.h"
#include "geometry/StepGeometryAdapter.h"
#include "ui/ShapePresetDialog.h"
#include "ui/ViewerDiagnosticsDialog.h"
#include "viewer/DiagnosticsLog.h"
#include "viewer/ModelViewport.h"
#include "viewer/MultiViewportPanel.h"

// --- 멈춤 위치 찾기: WATCHDOG_SEC 초 뒤에도 안 끝나면 메인 스레드 호출 스택을 찍는다(개발PC 전용 진단).
#include <windows.h>
#include <dbghelp.h>
#include <thread>
#pragma comment(lib, "dbghelp.lib")
namespace {
void DumpThreadStack(HANDLE thread) {
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    SuspendThread(thread);
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_FULL;
    GetThreadContext(thread, &ctx);
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = ctx.Rip; frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 60; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || frame.AddrPC.Offset == 0) break;
        char buf[sizeof(SYMBOL_INFO) + 512]{};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO); sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line{}; line.SizeOfStruct = sizeof(line); DWORD ld = 0;
        const bool hasSym = SymFromAddr(process, frame.AddrPC.Offset, &disp, sym);
        const bool hasLine = SymGetLineFromAddr64(process, frame.AddrPC.Offset, &ld, &line);
        std::fprintf(stderr, "  #%02d %s  %s:%lu\n", i, hasSym ? sym->Name : "?", hasLine ? line.FileName : "", hasLine ? line.LineNumber : 0);
    }
    ResumeThread(thread);
}
void StartWatchdog(int seconds) {
    HANDLE mainThread = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    std::thread([mainThread, seconds]() {
        for (int round = 0; round < 3; ++round) {
            Sleep(seconds * 1000);
            std::fprintf(stderr, "=== watchdog: main thread stack (round %d) ===\n", round);
            DumpThreadStack(mainThread);
        }
    }).detach();
}
} // namespace

namespace {

using Sync = QWindowSystemInterface::SynchronousDelivery;
int g_failures = 0;

// 이벤트 루프 한 번 돌리는 데 걸린 가장 긴 시간 = 화면이 멈춰 보인 가장 긴 시간.
qint64 g_longestStallMs = 0;

void Pump(int ms = 0) {
    QElapsedTimer timer;
    timer.start();
    do {
        QElapsedTimer one;
        one.start();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        g_longestStallMs = std::max(g_longestStallMs, one.elapsed());
    } while (timer.elapsed() < ms);
}

void Activate(QWidget* top) {
    top->show();
    top->raise();
    top->activateWindow();
    Pump(150);
    if (QApplication::activeWindow() != top && top->windowHandle()) {
        // 운영체제가 앞으로 가져오기를 막는 경우(원격/백그라운드 실행) - Qt 쪽 활성 창만 맞춘다.
        QWindowSystemInterface::handleFocusWindowChanged<Sync>(top->windowHandle(), Qt::ActiveWindowFocusReason);
        Pump(50);
    }
    std::printf("  active window ok: %s\n", QApplication::activeWindow() == top ? "yes" : "NO");
}

void Key(QWidget* top, int key) {
    QWindow* window = top->windowHandle();
    QWindowSystemInterface::handleKeyEvent<Sync>(window, QEvent::KeyPress, key, Qt::NoModifier);
    QWindowSystemInterface::handleKeyEvent<Sync>(window, QEvent::KeyRelease, key, Qt::NoModifier);
    Pump(30);
}

// Qt 키 코드 없이(0) 가상 키 코드/글자만 오는 키 - 원격 접속 프로그램이 글자로만 보내거나 Qt가 키를 모를 때.
// QShortcut은 이걸 못 알아보므로 보조 처리(ViewerKeyFallbackFilter)가 받아야 한다.
void RawKey(QWidget* top, quint32 virtualKey, const QString& text) {
    QWindow* window = top->windowHandle();
    QWindowSystemInterface::handleExtendedKeyEvent(window, QEvent::KeyPress, 0, Qt::NoModifier, 0, virtualKey,
                                                         0, text);
    QWindowSystemInterface::handleExtendedKeyEvent(window, QEvent::KeyRelease, 0, Qt::NoModifier, 0, virtualKey,
                                                         0, text);
    QWindowSystemInterface::flushWindowSystemEvents();
    Pump(30);
}


QPointF InWindow(QWidget* widget, QPoint local) {
    return QPointF(widget->mapTo(widget->window(), local));
}

void Mouse(QWidget* widget, QPoint local, Qt::MouseButtons state, Qt::MouseButton button, QEvent::Type type,
           Qt::KeyboardModifiers mods = Qt::NoModifier) {
    // QWindowSystemInterface는 운영체제가 주는 "실제 장치 픽셀" 좌표를 받는다 - 화면 배율(예: 200%)을 곱해야
    // 위젯 좌표와 같은 지점이 된다(안 곱하면 배율만큼 왼쪽 위로 쏠려 엉뚱한 위젯이 받는다).
    const qreal dpr = widget->devicePixelRatioF();
    const QPointF pos = InWindow(widget, local) * dpr;
    QWindowSystemInterface::handleMouseEvent<Sync>(widget->window()->windowHandle(), pos,
                                                    QPointF(widget->mapToGlobal(local)) * dpr, state, button, type,
                                                    mods);
}

// 왼쪽 버튼 드래그 - steps번 나눠 움직이고 매번 이벤트 루프를 돌려 실제로 다시 그리게 한다. 한 번당 평균 ms.
double Drag(QWidget* viewport, QPoint from, QPoint to, int steps, Qt::MouseButton button = Qt::LeftButton,
            Qt::KeyboardModifiers mods = Qt::NoModifier) {
    Mouse(viewport, from, button, button, QEvent::MouseButtonPress, mods);
    Pump();
    QElapsedTimer timer;
    timer.start();
    for (int i = 1; i <= steps; ++i) {
        const QPoint p = from + (to - from) * i / steps;
        Mouse(viewport, p, button, Qt::NoButton, QEvent::MouseMove, mods);
        Pump();
    }
    const double perStep = static_cast<double>(timer.elapsed()) / steps;
    Mouse(viewport, to, Qt::NoButton, button, QEvent::MouseButtonRelease, mods);
    Pump();
    return perStep;
}

double Wheel(QWidget* viewport, QPoint at, int clicks, int sign) {
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < clicks; ++i) {
        const qreal dpr = viewport->devicePixelRatioF();
        QWindowSystemInterface::handleWheelEvent(viewport->window()->windowHandle(), InWindow(viewport, at) * dpr,
                                                        QPointF(viewport->mapToGlobal(at)) * dpr, QPoint(),
                                                        QPoint(0, 120 * sign));
        QWindowSystemInterface::flushWindowSystemEvents();
        Pump();
    }
    return static_cast<double>(timer.elapsed()) / clicks;
}

void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

// 보조 처리 경로 점검 - Qt 키 코드 없이 VK만(F3/F1), 글자만(h, VK_PACKET), 한글 자모만(ㅗ) 보낸다.
void CheckFallbackKeys(QWidget* top, viewer::MultiViewportPanel* panel, viewer::ModelViewport* viewport) {
    const viewer::Camera& camera = panel->sharedCamera();
    const QPoint c(viewport->width() / 2, viewport->height() / 2);
    RawKey(top, 0x70, QString()); // VK_F1 -> 이동
    float yaw = camera.yawDeg;
    float panX = camera.panX;
    Drag(viewport, c, c + QPoint(60, 0), 6);
    Check(std::abs(camera.panX - panX) > 1.0f && std::abs(camera.yawDeg - yaw) < 0.01f, "fallback VK_F1 -> pans");
    RawKey(top, 0x72, QString()); // VK_F3 -> 회전
    yaw = camera.yawDeg;
    Drag(viewport, c, c + QPoint(60, 0), 6);
    Check(std::abs(camera.yawDeg - yaw) > 1.0f, "fallback VK_F3 -> rotates");
    const bool before = camera.sectionEnabled;
    RawKey(top, 0xE7, "h"); // VK_PACKET + 글자 h
    Check(camera.sectionEnabled != before, "fallback text 'h' -> section toggles");
    RawKey(top, 0xE5, QString::fromUtf8("ㅗ")); // 한글 상태의 H 자리
    Check(camera.sectionEnabled == before, "fallback hangul 'ㅗ' -> section toggles back");
}

// 단축키 묶음 점검 - F4/I/F1/F3(카메라로 결과 확인). top = 단축키가 걸린 톱레벨 창.
void CheckShortcuts(QWidget* top, viewer::MultiViewportPanel* panel, viewer::ModelViewport* viewport) {
    const viewer::Camera& camera = panel->sharedCamera();
    panel->resetToTrimetricView();
    Pump(30);
    Key(top, Qt::Key_F4);
    const bool snapped = std::fmod(std::abs(camera.yawDeg), 90.0f) < 0.01f &&
                         std::fmod(std::abs(camera.pitchDeg), 90.0f) < 0.01f;
    Check(snapped, "F4 -> nearest orthographic view");

    Key(top, Qt::Key_I);
    Check(std::abs(camera.yawDeg - 45.0f) < 0.01f, "I -> ISO view");

    const QPoint c(viewport->width() / 2, viewport->height() / 2);
    Key(top, Qt::Key_F1);
    float yaw = camera.yawDeg;
    float panX = camera.panX;
    Drag(viewport, c, c + QPoint(60, 0), 6);
    Check(std::abs(camera.panX - panX) > 1.0f && std::abs(camera.yawDeg - yaw) < 0.01f, "F1 -> left drag pans");

    Key(top, Qt::Key_F2);
    const float distance = camera.distance;
    Drag(viewport, c, c + QPoint(0, -60), 6);
    Check(camera.distance < distance * 0.9f, "F2 -> left drag zooms");

    Key(top, Qt::Key_F3);
    yaw = camera.yawDeg;
    Drag(viewport, c, c + QPoint(60, 0), 6);
    Check(std::abs(camera.yawDeg - yaw) > 1.0f, "F3 -> left drag rotates");

    Key(top, Qt::Key_H);
    Check(camera.sectionEnabled, "H -> section on");
    Key(top, Qt::Key_H);
    Check(!camera.sectionEnabled, "H -> section off");
}

void TimeInteraction(const char* label, viewer::MultiViewportPanel* panel, viewer::ModelViewport* viewport) {
    const viewer::Camera& camera = panel->sharedCamera();
    const QPoint c(viewport->width() / 2, viewport->height() / 2);
    g_longestStallMs = 0;
    const float yaw = camera.yawDeg;
    const double rotateMs = Drag(viewport, c - QPoint(150, 80), c + QPoint(150, 80), 60);
    const bool rotated = std::abs(camera.yawDeg - yaw) > 1.0f;
    const float panX = camera.panX;
    const double panMs = Drag(viewport, c, c + QPoint(-120, 60), 40, Qt::RightButton);
    const bool panned = std::abs(camera.panX - panX) > 1.0f;
    const float distance = camera.distance;
    const double wheelInMs = Wheel(viewport, c, 15, -1);
    const bool zoomed = std::abs(camera.distance - distance) > 1e-3f;
    const double wheelOutMs = Wheel(viewport, c, 15, 1);
    std::printf("  %s (viewport %dx%d): rotate %.1f ms/move%s, pan %.1f ms/move%s, wheel %.1f / %.1f ms/click%s, "
                "longest stall %lld ms\n",
                label, viewport->width(), viewport->height(), rotateMs, rotated ? "" : " (NOT ROTATED)", panMs,
                panned ? "" : " (NOT PANNED)", wheelInMs, wheelOutMs, zoomed ? "" : " (NOT ZOOMED)",
                static_cast<long long>(g_longestStallMs));
}

// 마우스를 버튼 없이 옮기고 멈춰서 호버 미리보기(피킹 모드일 때만 PickFace)가 돌게 한 뒤 걸린 시간.
double HoverPause(viewer::ModelViewport* viewport, QPoint at) {
    QElapsedTimer timer;
    timer.start();
    g_longestStallMs = 0;
    Mouse(viewport, at, Qt::NoButton, Qt::NoButton, QEvent::MouseMove);
    Pump(400); // 60ms 지연 + 계산 + 다시 그리기 - 그동안 가장 길게 멈춘 시간(=PickFace 한 번)
    (void)timer;
    return static_cast<double>(g_longestStallMs);
}

} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    viewer::diag::InstallCrashAndHangReporter();
    if (const char* wd = std::getenv("WATCHDOG_SEC")) {
        StartWatchdog(std::atoi(wd));
    }

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        std::fprintf(stderr, "usage: diag_viewer_input <a.stp> [b.stp ...]\n");
        return 1;
    }
    geometry::StepGeometryAdapter adapter;
    std::vector<std::pair<std::string, geometry::ModelHandle>> models;
    for (int i = 1; i < args.size(); ++i) {
        try {
            models.emplace_back("model " + std::to_string(i), adapter.LoadModel(args[i].toStdString()));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "LoadModel failed: %s\n", e.what());
            return 1;
        }
    }
    std::printf("loaded %zu model(s)\n", models.size());
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect a = screen->availableGeometry();
        std::printf("screen available %dx%d at %d,%d, dpr %.2f\n", a.width(), a.height(), a.x(), a.y(),
                    screen->devicePixelRatio());
    }

    // ---- 1) 메인 화면과 같은 배치: QMainWindow 중앙 뷰어 + 비교 테이블 dock(포커스를 테이블에 둔다) ----
    {
        std::printf("[main window]\n");
        QMainWindow window;
        window.resize(1400, 900);
        auto* panel = new viewer::MultiViewportPanel(&adapter, models, &window);
        window.setCentralWidget(panel);
        auto* dock = new QDockWidget("table", &window);
        auto* table = new QTableWidget(3, 3, dock);
        dock->setWidget(table);
        window.addDockWidget(Qt::BottomDockWidgetArea, dock);
        Activate(&window);
        auto* viewport = panel->findChildren<viewer::ModelViewport*>().front();
        // 검증 → 뷰어·키 입력 점검 창을 띄운 채로 키를 누르면 기록이 남아야 한다.
        QPointer<ui::ViewerDiagnosticsDialog> diagDialog = new ui::ViewerDiagnosticsDialog(&window);
        diagDialog->show();
        Activate(&window);
        CheckShortcuts(&window, panel, viewport);
        if (diagDialog) {
            const auto edits = diagDialog->findChildren<QPlainTextEdit*>();
            const QString keyLog = edits.size() >= 2 ? edits[1]->toPlainText() : QString();
            Check(keyLog.contains("F1") && keyLog.contains(QString::fromUtf8("뷰어 단축키 실행: F3")),
                  "diagnostics dialog logs keys and shortcut runs");
            std::printf("  diag env: %s\n", qPrintable(edits.isEmpty() ? QString() : edits[0]->toPlainText().section('\n', 0, 0)));
            diagDialog->close();
            Pump(30);
        }
        table->setFocus();
        Pump(30);
        Key(&window, Qt::Key_I);
        panel->resetToTrimetricView();
        Key(&window, Qt::Key_F4);
        Check(std::fmod(std::abs(panel->sharedCamera().yawDeg), 90.0f) < 0.01f, "F4 with focus in table");
        TimeInteraction("main viewer", panel, viewport);
    }

    // ---- 2) 실제 형상 프리셋 창 ----
    {
        std::printf("[shape preset dialog]\n");
        const QString dbPath = QDir::temp().filePath("diag_viewer_input.db");
        QFile::remove(dbPath);
        database::Database db(dbPath.toStdString());
        db.EnsureSchema();
        const int projectId = db.FindOrCreateProject("diag_project");
        ui::ShapePresetDialog dialog(&db, projectId, &adapter, models, nullptr);
        if (std::getenv("DIALOG_SMALL")) {
            dialog.resize(1000, 600);
            dialog.move(50, 50);
        }
        Activate(&dialog);
        std::printf("  dialog frame %dx%d at %d,%d, client %dx%d, window handle %dx%d\n", dialog.frameGeometry().width(),
                    dialog.frameGeometry().height(), dialog.frameGeometry().x(), dialog.frameGeometry().y(), dialog.width(),
                    dialog.height(), dialog.windowHandle()->width(), dialog.windowHandle()->height());
        auto* panel = dialog.findChild<viewer::MultiViewportPanel*>();
        auto* viewport = panel->findChildren<viewer::ModelViewport*>().front();
        std::printf("  focus widget: %s\n",
                    QApplication::focusWidget() ? QApplication::focusWidget()->metaObject()->className() : "(none)");
        CheckShortcuts(&dialog, panel, viewport);

        TimeInteraction("preset idle", panel, viewport);
        panel->setSectionEnabled(true);
        Pump(30);
        TimeInteraction("preset idle + section", panel, viewport);
        panel->setSectionEnabled(false);

        QPushButton* manual = nullptr;
        for (auto* button : dialog.findChildren<QPushButton*>()) {
            if (button->text().startsWith(QString::fromUtf8("면 직접 선택"))) {
                manual = button;
            }
        }
        if (manual) {
            manual->click();
            Pump(30);
            TimeInteraction("preset manual-select", panel, viewport);
            double worst = 0.0;
            double sum = 0.0;
            const int n = 12;
            for (int i = 0; i < n; ++i) {
                const QPoint p(viewport->width() * (i + 1) / (n + 1), viewport->height() / 2 + (i % 3 - 1) * 40);
                const double ms = HoverPause(viewport, p);
                worst = std::max(worst, ms);
                sum += ms;
            }
            std::printf("  hover preview stall: avg %.1f ms, worst %.1f ms\n", sum / n, worst);
            // Ctrl+드래그 사각형 선택 - 뗄 때 모든 면을 화면에 투영한다.
            const QPoint c(viewport->width() / 2, viewport->height() / 2);
            QElapsedTimer timer;
            timer.start();
            g_longestStallMs = 0;
            Drag(viewport, c - QPoint(200, 150), c + QPoint(200, 150), 10, Qt::LeftButton, Qt::ControlModifier);
            std::printf("  ctrl+drag region select: %lld ms total, longest stall %lld ms\n",
                        static_cast<long long>(timer.elapsed()), static_cast<long long>(g_longestStallMs));
        }
        // 다른 칸(이름 입력)에 포커스가 있어도 F 키 단축키는 먹어야 한다.
        if (auto* edit = dialog.findChild<QLineEdit*>()) {
            edit->setFocus();
            Pump(30);
            panel->resetToTrimetricView();
            Key(&dialog, Qt::Key_F4);
            Check(std::fmod(std::abs(panel->sharedCamera().yawDeg), 90.0f) < 0.01f, "F4 with focus in name edit");
            // 이름 칸에 h를 치면 글자가 들어가야 하고 단면이 바뀌면 안 된다(보조 처리가 가로채면 안 됨).
            const bool sectionBefore = panel->sharedCamera().sectionEnabled;
            edit->clear();
            QWindowSystemInterface::handleKeyEvent<Sync>(dialog.windowHandle(), QEvent::KeyPress, Qt::Key_H,
                                                         Qt::NoModifier, "h");
            QWindowSystemInterface::handleKeyEvent<Sync>(dialog.windowHandle(), QEvent::KeyRelease, Qt::Key_H,
                                                         Qt::NoModifier, "h");
            RawKey(&dialog, 0xE7, "x");
            Pump(30);
            Check(edit->text() == "hx" && panel->sharedCamera().sectionEnabled == sectionBefore,
                  "typing h/x in name edit stays text");
            viewport->setFocus();
            Pump(30);
        }
        CheckFallbackKeys(&dialog, panel, viewport);

        // 멈춤 기록 점검 - UI 스레드를 5초 붙잡아 진단 로그에 [멈춤]과 호출 위치가 남는지 본다.
        if (std::getenv("HANG_TEST")) {
            std::printf("  hang test: blocking UI thread 5.5s...\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(5500));
            Pump(800);
            std::printf("  log file: %s\n", qPrintable(viewer::diag::LogFilePath()));
        }
        dialog.hide();
        Pump(50);
    }

    std::printf(g_failures == 0 ? "viewer input OK\n" : "viewer input FAILED (%d)\n", g_failures);
    return g_failures == 0 ? 0 : 2;
}
