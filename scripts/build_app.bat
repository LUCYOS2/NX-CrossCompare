@echo off
rem ============================================================================
rem  build_app.bat - build NX CrossCompare and start it
rem
rem  Double-click this file. It
rem    1. finds the existing build folder (build\) and its cmake, opens the Visual
rem       Studio x64 environment if needed (NMake/Ninja builds)
rem    2. refreshes the build files (new source files after "git pull" are picked up)
rem    3. builds everything (app + tests + diagnostic tools)
rem    4. asks whether to start the app (working folder = the app folder, so the
rem       app keeps using the nx_crosscompare.db that is already there)
rem
rem  Settings (edit here or set an environment variable with the same name):
rem    BUILD_NAME     build folder under the repository root   (default: build)
rem    CONFIG         Release / Debug for Visual Studio builds  (default: Release)
rem    EXTRA_DLL_DIR  extra DLL folder if the app cannot start
rem  NOTE: keep this file ASCII only - cmd misreads UTF-8 text in .bat files.
rem ============================================================================
setlocal EnableExtensions

call "%~dp0_build_env.bat" env
if defined ENV_ERROR goto :failed
call "%~dp0_build_env.bat" refresh
if defined ENV_ERROR goto :failed

rem CLEAN=1 (rebuild_app_clean.bat) deletes every compiled object first. Use it when
rem the app crashes right after an update (e.g. "HEAP CORRUPTION DETECTED"): a build
rem that failed half-way can leave objects compiled against old class layouts.
if not "%CLEAN%"=="1" goto :build
echo.
echo Cleaning old build outputs ...
"%CMAKE_EXE%" --build "%BUILD%" --target clean --config %CONFIG%
if errorlevel 1 (
    set "ENV_ERROR=Clean failed - see the messages above"
    goto :failed
)

:build
rem A running app locks nx_crosscompare.exe and the link step fails (LNK1168, U1077).
tasklist /FI "IMAGENAME eq nx_crosscompare.exe" 2>nul | find /I "nx_crosscompare.exe" >nul
if not errorlevel 1 (
    set "ENV_ERROR=The app nx_crosscompare.exe is still running - close it, then run this file again"
    goto :failed
)
rem Step 1: the app alone. Step 2: everything else (tests, diagnostic tools).
rem A failure in step 2 does not stop the app from being used.
echo.
echo [1/2] Building the app (this can take several minutes the first time) ...
set "BUILD_PART=app"
"%CMAKE_EXE%" --build "%BUILD%" --config %CONFIG% --target nx_crosscompare
if errorlevel 1 goto :buildfailed

:buildtools
echo.
echo [2/2] Building tests and diagnostic tools ...
set "BUILD_PART=tools"
"%CMAKE_EXE%" --build "%BUILD%" --config %CONFIG%
if errorlevel 1 goto :buildfailed

:buildok
call "%~dp0_build_env.bat" dllpath
if not defined APP_EXE (
    set "ENV_ERROR=nx_crosscompare.exe not found under the build folder"
    goto :failed
)
echo.
if defined TOOLS_FAILED (
    echo App build OK: %APP_EXE%
    echo [WARNING] A test/diagnostic tool did not build - send the lines above. The app itself is fine.
) else (
    echo Build OK: %APP_EXE%
)

rem RUN_APP=1 / RUN_APP=0 skips the question.
if "%RUN_APP%"=="1" goto :run
if "%RUN_APP%"=="0" goto :end
choice /c YN /m "Start the app now"
if errorlevel 2 goto :end

:run
echo Starting the app ...
rem Drop the trailing "\" so the closing quote of /D "..." is not swallowed.
set "RUN_DIR=%APP_DIR:~0,-1%"
start "" /D "%RUN_DIR%" "%APP_EXE%"
goto :end

:buildfailed
rem The real cause is far above the final "U1077" line. Build once more (only the
rem failed part is redone, so it is quick), save everything to a log file and show
rem just the error lines.
set "BUILD_LOG=%REPO%\scripts\diag_logs\build_log.txt"
if not exist "%REPO%\scripts\diag_logs" mkdir "%REPO%\scripts\diag_logs"
echo.
echo Build failed (%BUILD_PART%). Collecting the error lines ...
set "TARGET_ARG="
if "%BUILD_PART%"=="app" set "TARGET_ARG=--target nx_crosscompare"
"%CMAKE_EXE%" --build "%BUILD%" --config %CONFIG% %TARGET_ARG% > "%BUILD_LOG%" 2>&1
if not errorlevel 1 (
    rem The second try passed - the first failure was temporary (a file locked by
    rem antivirus/indexer, etc.). Carry on as a normal successful build.
    echo The second try succeeded - the first failure was temporary.
    if "%BUILD_PART%"=="app" goto :buildtools
    goto :buildok
)
echo.
echo ---------------- error lines ----------------
findstr /i /c:"error" /c:"cannot open" /c:"failed" "%BUILD_LOG%"
echo ----------------------------------------------
rem Also show the end of the log - the failing step is always printed last, even
rem when its message does not contain the word "error".
for /f %%N in ('find /c /v "" ^< "%BUILD_LOG%"') do set "LOG_LINES=%%N"
set /a LOG_SKIP=LOG_LINES-25
if %LOG_SKIP% LSS 0 set "LOG_SKIP=0"
echo ---------------- last lines of the log ----------------
more +%LOG_SKIP% "%BUILD_LOG%"
echo -------------------------------------------------------
echo Full log: %BUILD_LOG%
if "%BUILD_PART%"=="tools" (
    set "TOOLS_FAILED=1"
    goto :buildok
)
set "ENV_ERROR=Build failed - send the error lines above (file name, error code, message)"
goto :failed

:failed
echo.
echo [ERROR] %ENV_ERROR%

:end
echo.
if not defined NO_PAUSE pause
endlocal
