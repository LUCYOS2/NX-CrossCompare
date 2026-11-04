@echo off
rem ============================================================================
rem  run_diag_matching.bat - cross-inch matching diagnostic (dev plan section 19, stage 2)
rem
rem  Usage:
rem    Drag and drop STEP files onto this .bat. The FIRST file is the reference inch.
rem    (or: run_diag_matching.bat "43.stp" "55.stp" "65.stp")
rem
rem  What it does:
rem    1. Reads cmake / OCCT paths from build\CMakeCache.txt
rem    2. Opens the Visual Studio x64 build environment if needed (NMake/Ninja builds)
rem    3. Builds only the diag_measure_matching target
rem    4. Runs it and saves the result to scripts\diag_logs\diag_YYYYMMDD_HHMMSS.log
rem       (diag_logs is git-ignored - logs never leave this PC unless copied by hand)
rem
rem  If the build folder is not "build", or DLLs are elsewhere, edit the SETTINGS below.
rem  NOTE: keep this file ASCII only - cmd misreads UTF-8 text in .bat files.
rem ============================================================================
setlocal EnableExtensions

rem ----- SETTINGS (an environment variable with the same name overrides these) --
rem Build folder name under the repository root.
if not defined BUILD_NAME set "BUILD_NAME=build"
rem Build configuration for Visual Studio generators (ignored by NMake/Ninja).
if not defined CONFIG set "CONFIG=Release"
rem Extra DLL folder (OCCT bin etc.) if the tool cannot start. Leave empty if not needed.
if not defined EXTRA_DLL_DIR set "EXTRA_DLL_DIR="
rem ----------------------------------------------------------------------------

set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
set "BUILD=%REPO%\%BUILD_NAME%"
set "CACHE=%BUILD%\CMakeCache.txt"

if "%~1"=="" goto :no_args
if not exist "%CACHE%" goto :no_cache

rem ----- 1. cmake and OCCT paths from CMakeCache ------------------------------
set "CMAKE_EXE="
set "GENERATOR="
set "OCCT_LIB="
set "OCCT_CFG="
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_COMMAND:" "%CACHE%"') do set "CMAKE_EXE=%%B"
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_GENERATOR:" "%CACHE%"') do set "GENERATOR=%%B"
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"OpenCASCADE_LIBRARY_DIR:" "%CACHE%"') do set "OCCT_LIB=%%B"
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"OpenCASCADE_DIR:" "%CACHE%"') do set "OCCT_CFG=%%B"
if defined CMAKE_EXE set "CMAKE_EXE=%CMAKE_EXE:/=\%"
if defined OCCT_LIB set "OCCT_LIB=%OCCT_LIB:/=\%"
if defined OCCT_CFG set "OCCT_CFG=%OCCT_CFG:/=\%"
if not defined CMAKE_EXE set "CMAKE_EXE=cmake"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake"
echo build     : %BUILD%
echo generator : %GENERATOR%
echo cmake     : %CMAKE_EXE%

rem ----- 2. Visual Studio x64 environment (needed for NMake / Ninja) -----------
where cl >nul 2>nul
if not errorlevel 1 goto :env_ready
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :env_ready
set "VSPATH="
rem Not inside "for /f (...)": the ")" of "(x86)" would end the for clause early.
set "VSPATH_TMP=%TEMP%\nxcc_vspath_%RANDOM%.txt"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSPATH_TMP%" 2>nul
set /p VSPATH=<"%VSPATH_TMP%"
del "%VSPATH_TMP%" >nul 2>nul
if not defined VSPATH goto :env_ready
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" goto :env_ready
echo Opening Visual Studio x64 environment...
rem vcvars64.bat itself calls vswhere.exe by name.
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
:env_ready

rem ----- 3. Build the diagnostic tool -----------------------------------------
echo.
echo Building diag_measure_matching ...
"%CMAKE_EXE%" --build "%BUILD%" --target diag_measure_matching --config %CONFIG%
if errorlevel 1 goto :build_failed

rem ----- 4. Find the exe (NMake/Ninja: tests\, VS generator: tests\<config>\) ----
set "EXE="
if exist "%BUILD%\src\tests\diag_measure_matching.exe" set "EXE=%BUILD%\src\tests\diag_measure_matching.exe"
if not defined EXE if exist "%BUILD%\src\tests\%CONFIG%\diag_measure_matching.exe" set "EXE=%BUILD%\src\tests\%CONFIG%\diag_measure_matching.exe"
if not defined EXE for /f "delims=" %%E in ('dir /s /b "%BUILD%\diag_measure_matching.exe" 2^>nul') do set "EXE=%%E"
if not defined EXE goto :no_exe

rem ----- 5. DLL search path ----------------------------------------------------
rem The app folder already has every DLL the app needs (OCCT included), so reuse it.
set "APP_DIR="
for /f "delims=" %%E in ('dir /s /b "%BUILD%\nx_crosscompare.exe" 2^>nul') do set "APP_DIR=%%~dpE"
if defined APP_DIR set "PATH=%APP_DIR%;%PATH%"
if defined OCCT_LIB set "PATH=%OCCT_LIB%\..\bin;%OCCT_LIB%\..\..\bin;%PATH%"
if defined OCCT_CFG set "PATH=%OCCT_CFG%\..\..\bin;%OCCT_CFG%\..\..\..\bin;%PATH%"
if defined EXTRA_DLL_DIR set "PATH=%EXTRA_DLL_DIR%;%PATH%"

rem ----- 6. Run ----------------------------------------------------------------
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
if not "%RC%"=="0" echo [ERROR] exit code %RC%. If the log is empty, a DLL was not found - set EXTRA_DLL_DIR in this file.
echo Saved: %LOG%
goto :end

:no_args
echo [ERROR] No STEP files given.
echo         Drag and drop STEP files onto this .bat - the FIRST file is the reference inch.
goto :end

:no_cache
echo [ERROR] %CACHE% not found.
echo         Build the app once first, or set BUILD_NAME in this file to your build folder.
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
