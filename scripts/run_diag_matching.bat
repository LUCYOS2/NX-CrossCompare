@echo off
rem ============================================================================
rem  run_diag_matching.bat - cross-inch matching diagnostic (dev plan section 19, stage 2)
rem
rem  The same check is in the app: menu "verify -> matching diagnostics".
rem  Use this file when you want a log file or many STEP files in one go.
rem
rem  Usage:
rem    Drag and drop STEP files onto this .bat. The FIRST file is the reference inch.
rem    (or: run_diag_matching.bat "43.stp" "55.stp" "65.stp")
rem
rem  What it does:
rem    1. Finds the build folder / cmake, opens the Visual Studio x64 environment if needed
rem    2. Refreshes the build files and builds only diag_measure_matching
rem    3. Runs it and saves the result to scripts\diag_logs\diag_YYYYMMDD_HHMMSS.log
rem       (diag_logs is git-ignored - logs never leave this PC unless copied by hand)
rem
rem  Settings: see _build_env.bat (BUILD_NAME / CONFIG / EXTRA_DLL_DIR).
rem  NOTE: keep this file ASCII only - cmd misreads UTF-8 text in .bat files.
rem ============================================================================
setlocal EnableExtensions

if "%~1"=="" goto :no_args

call "%~dp0_build_env.bat" env
if defined ENV_ERROR goto :failed
call "%~dp0_build_env.bat" refresh
if defined ENV_ERROR goto :failed

echo.
echo Building diag_measure_matching ...
"%CMAKE_EXE%" --build "%BUILD%" --target diag_measure_matching --config %CONFIG%
if errorlevel 1 goto :build_failed

rem NMake/Ninja: tests\, Visual Studio generator: tests\<config>\
set "EXE="
if exist "%BUILD%\src\tests\diag_measure_matching.exe" set "EXE=%BUILD%\src\tests\diag_measure_matching.exe"
if not defined EXE if exist "%BUILD%\src\tests\%CONFIG%\diag_measure_matching.exe" set "EXE=%BUILD%\src\tests\%CONFIG%\diag_measure_matching.exe"
if not defined EXE for /f "delims=" %%E in ('dir /s /b "%BUILD%\diag_measure_matching.exe" 2^>nul') do set "EXE=%%E"
if not defined EXE goto :no_exe

call "%~dp0_build_env.bat" dllpath

if not exist "%~dp0diag_logs" mkdir "%~dp0diag_logs"
set "STAMP="
for /f %%T in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "STAMP=%%T"
if not defined STAMP set "STAMP=latest"
set "LOG=%~dp0diag_logs\diag_%STAMP%.log"

echo.
echo exe : %EXE%
echo log : %LOG%
echo Running... (large models can take a few minutes)
"%EXE%" %* > "%LOG%" 2>&1
set "RC=%ERRORLEVEL%"
rem The tool prints UTF-8 (Korean) text.
chcp 65001 >nul
type "%LOG%"
echo.
if not "%RC%"=="0" echo [ERROR] exit code %RC%. If the log is empty, a DLL was not found - set EXTRA_DLL_DIR in _build_env.bat.
echo Saved: %LOG%
goto :end

:no_args
echo [ERROR] No STEP files given.
echo         Drag and drop STEP files onto this .bat - the FIRST file is the reference inch.
goto :end

:failed
echo.
echo [ERROR] %ENV_ERROR%
goto :end

:build_failed
echo.
echo [ERROR] Build failed - see the messages above.
goto :end

:no_exe
echo [ERROR] diag_measure_matching.exe not found under %BUILD% after the build.
goto :end

:end
echo.
if not defined NO_PAUSE pause
endlocal
