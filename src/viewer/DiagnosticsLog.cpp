#include "viewer/DiagnosticsLog.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QScreen>
#include <QStringList>
#include <QSysInfo>
#include <QTimer>
#include <QtGlobal>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h 다음에 와야 한다.
#include <crtdbg.h>
#include <dbghelp.h>
#include <psapi.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")
#endif

namespace viewer::diag {

namespace {

std::atomic<bool> g_installed{false};
std::mutex g_fileMutex;
std::wstring g_logPathW; // 비정상 종료 처리 중에도 Qt를 거치지 않고 쓰려고 미리 만들어 둔다
QString g_logPath;

std::mutex g_glMutex;
GlInfo g_gl;

std::atomic<long long> g_heartbeatMs{0}; // UI 스레드가 마지막으로 살아 있다고 알린 시각
std::atomic<int> g_qtWarnings{0};
std::atomic<bool> g_quitting{false}; // 종료 중에는 타이머가 멈추므로 멈춤 감시를 끈다
QtMessageHandler g_previousHandler = nullptr;

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void EnsureLogPath() {
    if (!g_logPathW.empty() || !QCoreApplication::instance()) {
        return;
    }
    const QString dir = QDir(QCoreApplication::applicationDirPath()).filePath("diag_logs");
    QDir().mkpath(dir);
    g_logPath = QDir(dir).filePath("app_log.txt");
    g_logPathW = QDir::toNativeSeparators(g_logPath).toStdWString();
}

std::string TimeStamp() {
    char buf[64] = {};
#ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d", st.wYear, st.wMonth, st.wDay, st.wHour,
                  st.wMinute, st.wSecond, st.wMilliseconds);
#endif
    return buf;
}

// 비정상 종료 경로에서도 쓰므로 Qt 객체/할당을 최소로 - 잠금은 잠깐만 기다리고 못 잡으면 그냥 쓴다.
void WriteLineUtf8(const std::string& line) {
    if (g_logPathW.empty()) {
        return;
    }
    std::unique_lock<std::mutex> lock(g_fileMutex, std::defer_lock);
    for (int i = 0; i < 50 && !lock.try_lock(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    FILE* file = nullptr;
#ifdef _WIN32
    if (_wfopen_s(&file, g_logPathW.c_str(), L"ab") != 0) {
        file = nullptr;
    }
#endif
    if (!file) {
        return;
    }
    const std::string stamped = TimeStamp() + "  " + line + "\r\n";
    std::fwrite(stamped.data(), 1, stamped.size(), file);
    std::fclose(file);
}

#ifdef _WIN32

std::string WideToUtf8(const wchar_t* text) {
    if (!text || !*text) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size > 0 ? size - 1 : 0), '\0');
    if (size > 1) {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    }
    return out;
}

constexpr int kMaxFrames = 48;

// 스택을 거슬러 올라가며 복귀 주소만 모은다 - 멈춘(Suspend) 다른 스레드를 볼 때도 써서, 그 스레드가 힙 잠금을
// 쥔 채 멈췄을 수 있으니 여기서는 메모리 할당을 하지 않는다(심볼 이름 찾기는 스레드를 다시 돌린 뒤에 한다).
// C++ 객체가 없는 함수여야 __try를 쓸 수 있다.
int CaptureFramesNoAlloc(CONTEXT ctx, DWORD64* frames, int maxFrames) {
    int count = 0;
    __try {
        while (count < maxFrames && ctx.Rip != 0) {
            frames[count++] = ctx.Rip;
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
            if (!function) {
                // 잎(leaf) 함수 - 복귀 주소가 바로 스택 꼭대기에 있다.
                ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
                ctx.Rsp += 8;
                continue;
            }
            PVOID handlerData = nullptr;
            DWORD64 establisherFrame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, function, &ctx, &handlerData,
                             &establisherFrame, nullptr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return count;
}

std::mutex g_symMutex;
bool g_symReady = false;

// "  #03 nx_crosscompare.exe  viewer::ModelViewport::paintGL  ModelViewport.cpp:703" 형태. 경로는 파일 이름만 남긴다.
std::string SymbolizeFrames(const DWORD64* frames, int count) {
    std::unique_lock<std::mutex> lock(g_symMutex, std::defer_lock);
    for (int i = 0; i < 100 && !lock.try_lock(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    HANDLE process = GetCurrentProcess();
    if (!g_symReady) {
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
        g_symReady = SymInitialize(process, nullptr, TRUE) != FALSE;
    }
    std::string out;
    for (int i = 0; i < count; ++i) {
        const DWORD64 address = frames[i];
        std::string module = "?";
        HMODULE moduleHandle = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(address), &moduleHandle)) {
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(moduleHandle, path, MAX_PATH);
            const wchar_t* base = wcsrchr(path, L'\\');
            module = WideToUtf8(base ? base + 1 : path);
        }
        std::string symbolText = "?";
        alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 512] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 displacement = 0;
        if (g_symReady && SymFromAddr(process, address, &displacement, symbol)) {
            symbolText = symbol->Name;
        }
        // 와이드 문자 버전 - 경로에 한글/특수문자(예: ●)가 있으면 ANSI 버전은 파일 이름 끝이 잘렸다.
        std::string lineText;
        IMAGEHLP_LINEW64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        if (g_symReady && SymGetLineFromAddrW64(process, address, &lineDisplacement, &line) && line.FileName) {
            const wchar_t* file = line.FileName;
            const wchar_t* slash = wcsrchr(file, L'\\');
            const wchar_t* slash2 = wcsrchr(file, L'/');
            if (slash2 && (!slash || slash2 > slash)) {
                slash = slash2;
            }
            lineText = WideToUtf8(slash ? slash + 1 : file) + ":" + std::to_string(line.LineNumber);
        }
        char head[32];
        std::snprintf(head, sizeof(head), "    #%02d ", i);
        out += head + module + "  " + symbolText + (lineText.empty() ? "" : "  " + lineText) + "\r\n";
    }
    return out;
}

std::string StackFromContext(const CONTEXT& ctx) {
    DWORD64 frames[kMaxFrames] = {};
    const int count = CaptureFramesNoAlloc(ctx, frames, kMaxFrames);
    return SymbolizeFrames(frames, count);
}

std::string CurrentThreadStack() {
    CONTEXT ctx = {};
    RtlCaptureContext(&ctx);
    return StackFromContext(ctx);
}

HANDLE g_uiThread = nullptr;

std::string UiThreadStack() {
    if (!g_uiThread) {
        return "    (UI 스레드 핸들 없음)\r\n";
    }
    DWORD64 frames[kMaxFrames] = {};
    int count = 0;
    if (SuspendThread(g_uiThread) != static_cast<DWORD>(-1)) {
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(g_uiThread, &ctx)) {
            count = CaptureFramesNoAlloc(ctx, frames, kMaxFrames);
        }
        ResumeThread(g_uiThread);
    }
    return SymbolizeFrames(frames, count);
}

LONG WINAPI UnhandledExceptionLogger(EXCEPTION_POINTERS* info) {
    char head[160];
    std::snprintf(head, sizeof(head), "[비정상 종료] 처리되지 않은 예외 0x%08lX (주소 %p) - 호출 위치:",
                  info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0UL,
                  info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr);
    std::string text = head;
    text += "\r\n";
    if (info && info->ContextRecord) {
        text += StackFromContext(*info->ContextRecord);
    }
    text += "    " + MemoryUsageText().toStdString();
    WriteLineUtf8(text);
    return EXCEPTION_CONTINUE_SEARCH; // Windows 오류 보고/디버거로 그대로 넘긴다
}

void AbortSignalLogger(int) {
    WriteLineUtf8("[비정상 종료] abort() 호출 - 호출 위치:\r\n" + CurrentThreadStack());
}

void TerminateLogger() {
    std::string what = "알 수 없음";
    if (auto ex = std::current_exception()) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            what = e.what();
        } catch (...) {
        }
    }
    WriteLineUtf8("[비정상 종료] std::terminate (" + what + ") - 호출 위치:\r\n" + CurrentThreadStack());
    std::abort();
}

#ifdef _DEBUG
// Debug CRT의 "HEAP CORRUPTION DETECTED" 같은 오류 창이 뜰 때 그 메시지와 호출 위치를 같이 남긴다.
int CrtReportLogger(int reportType, wchar_t* message, int* returnValue) {
    if (reportType == _CRT_ERROR || reportType == _CRT_ASSERT) {
        WriteLineUtf8("[CRT 오류] " + WideToUtf8(message) + " - 호출 위치:\r\n" + CurrentThreadStack());
    }
    if (returnValue) {
        *returnValue = 0;
    }
    return FALSE; // 원래 오류 창은 그대로 띄운다
}
#endif

#endif // _WIN32

void QtMessageLogger(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    // 앱 자체 경고(분류 "default")는 인치 라벨/파일 이름이 섞일 수 있어 남기지 않는다 - Qt 내부 경고(qt.* 분류)와
    // 오류/치명 오류만 남긴다.
    const bool appOwnWarning = type == QtWarningMsg && (!context.category || std::strcmp(context.category, "default") == 0);
    if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) && !appOwnWarning) {
        // 같은 경고가 끝없이 쌓이지 않게 실행당 200개까지만.
        if (type == QtFatalMsg || g_qtWarnings.fetch_add(1) < 200) {
            const char* kind = type == QtWarningMsg ? "Qt 경고" : (type == QtCriticalMsg ? "Qt 오류" : "Qt 치명 오류");
            std::string line = std::string("[") + kind + "] " + message.toStdString();
            if (context.category && std::strcmp(context.category, "default") != 0) {
                line += std::string(" (") + context.category + ")";
            }
#ifdef _WIN32
            if (type == QtFatalMsg) {
                line += " - 호출 위치:\r\n" + CurrentThreadStack();
            }
#endif
            WriteLineUtf8(line);
        }
    }
    if (g_previousHandler) {
        g_previousHandler(type, context, message);
    }
}

// 화면 멈춤 감시 - UI 스레드가 4초 넘게 심장 박동을 못 보내면 그때 UI 스레드가 어디서 무엇을 하고 있었는지
// 남긴다(10초마다 최대 3번). 다시 살아나면 얼마나 멈췄는지도 남긴다.
void HangWatchdogLoop() {
    constexpr long long kStallMs = 4000;
    constexpr long long kRepeatMs = 10000;
    long long stalledBeat = -1;
    int reports = 0;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const long long beat = g_heartbeatMs.load();
        if (beat == 0 || g_quitting.load()) {
            continue;
        }
        const long long lag = NowMs() - beat;
        if (lag >= kStallMs) {
            if (stalledBeat != beat) {
                stalledBeat = beat;
                reports = 0;
            }
            if (reports < 3 && lag >= kStallMs + reports * kRepeatMs) {
                std::string text = "[멈춤] 화면이 " + std::to_string(lag / 1000) + "초째 응답 없음 - 그때 UI 스레드 호출 위치:\r\n";
#ifdef _WIN32
                text += UiThreadStack();
#endif
                text += "    " + MemoryUsageText().toStdString();
                WriteLineUtf8(text);
                ++reports;
            }
        } else if (stalledBeat != -1) {
            if (reports > 0) {
                WriteLineUtf8("[멈춤 끝] 다시 응답함");
            }
            stalledBeat = -1;
            reports = 0;
        }
    }
}

} // namespace

QString LogFilePath() {
    EnsureLogPath();
    return g_logPath;
}

void Log(const QString& line) {
    EnsureLogPath();
    WriteLineUtf8(line.toStdString());
}

void SetGlInfo(const GlInfo& info) {
    bool first = false;
    {
        std::lock_guard<std::mutex> lock(g_glMutex);
        if (!g_gl.valid) {
            g_gl = info;
            g_gl.valid = true;
            first = true;
        }
    }
    if (first) {
        Log(QString("[그래픽] %1 / %2 / OpenGL %3%4")
                .arg(info.vendor, info.renderer, info.version,
                     info.software ? "  <- 그래픽 가속 없음(소프트웨어 렌더링)" : ""));
        Hub::instance()->NotifyGlInfoReady();
    }
}

GlInfo CurrentGlInfo() {
    std::lock_guard<std::mutex> lock(g_glMutex);
    return g_gl;
}

bool IsRemoteSession() {
#ifdef _WIN32
    if (GetSystemMetrics(SM_REMOTESESSION) != 0) {
        return true;
    }
    const QString session = qEnvironmentVariable("SESSIONNAME");
    return session.startsWith("RDP-", Qt::CaseInsensitive);
#else
    return false;
#endif
}

QString MemoryUsageText() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters = {};
    counters.cb = sizeof(counters);
    double privateMb = 0.0;
    double workingMb = 0.0;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        privateMb = static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
        workingMb = static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
    }
    MEMORYSTATUSEX status = {};
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    const double gb = 1024.0 * 1024.0 * 1024.0;
    return QString("앱 메모리 %1MB(작업 집합 %2MB), 시스템 남은 메모리 %3/%4GB")
        .arg(privateMb, 0, 'f', 0)
        .arg(workingMb, 0, 'f', 0)
        .arg(static_cast<double>(status.ullAvailPhys) / gb, 0, 'f', 1)
        .arg(static_cast<double>(status.ullTotalPhys) / gb, 0, 'f', 1);
#else
    return QString();
#endif
}

QString EnvironmentSummary() {
    QStringList lines;
#ifdef _DEBUG
    lines << "빌드: Debug (Release보다 계산이 몇 배 느림)";
#else
    lines << "빌드: Release";
#endif
    lines << QString("Qt %1 (빌드 %2), %3").arg(qVersion(), QT_VERSION_STR, QSysInfo::prettyProductName());
    lines << QString("원격 접속 세션: %1").arg(IsRemoteSession() ? "예" : "아니오");
    const auto screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        const QRect g = screens[i]->geometry();
        lines << QString("화면 %1: %2x%3, 배율 %4%").arg(i + 1).arg(g.width()).arg(g.height())
                     .arg(qRound(screens[i]->devicePixelRatio() * 100.0));
    }
    const GlInfo gl = CurrentGlInfo();
    if (gl.valid) {
        lines << QString("그래픽: %1 / %2 / OpenGL %3").arg(gl.vendor, gl.renderer, gl.version);
        if (gl.software) {
            lines << "  -> 그래픽 가속 없이 CPU로 그리는 중(소프트웨어 렌더링) - 큰 도면 회전/확대가 매우 느리고 멈출 수 있음";
        }
    } else {
        lines << "그래픽: (아직 3D 화면이 한 번도 안 그려짐)";
    }
    lines << QString("CPU 스레드 %1개").arg(std::thread::hardware_concurrency());
    lines << MemoryUsageText();
    return lines.join("\n");
}

Hub* Hub::instance() {
    static Hub* hub = new Hub;
    return hub;
}

void InstallCrashAndHangReporter() {
    if (g_installed.exchange(true)) {
        return;
    }
    EnsureLogPath();
    WriteLineUtf8("==================== 앱 시작 ====================");
    for (const QString& line : EnvironmentSummary().split('\n')) {
        WriteLineUtf8("  " + line.toStdString());
    }
#ifdef _WIN32
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_uiThread, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);
    SetUnhandledExceptionFilter(UnhandledExceptionLogger);
    std::signal(SIGABRT, AbortSignalLogger);
    std::set_terminate(TerminateLogger);
#ifdef _DEBUG
    _CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, CrtReportLogger);
#endif
#endif
    g_previousHandler = qInstallMessageHandler(QtMessageLogger);

    // UI 스레드 심장 박동 - 모달 창(exec) 안에서도 타이머는 돈다.
    auto* heartbeat = new QTimer(QCoreApplication::instance());
    heartbeat->setInterval(250);
    QObject::connect(heartbeat, &QTimer::timeout, []() { g_heartbeatMs.store(NowMs()); });
    heartbeat->start();
    g_heartbeatMs.store(NowMs());
    std::thread(HangWatchdogLoop).detach();
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                     []() {
                         g_quitting.store(true);
                         WriteLineUtf8("앱 정상 종료");
                     });
}

} // namespace viewer::diag
