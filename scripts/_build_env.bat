@echo off
rem ============================================================================
rem  _build_env.bat - shared helper for build_app.bat / run_diag_matching.bat
rem  (not meant to be run directly). No setlocal here: the variables it sets must
rem  stay visible to the calling script.
rem
rem    call "%~dp0_build_env.bat" env      - find the build folder, cmake, open the
rem                                          Visual Studio x64 environment if needed
rem    call "%~dp0_build_env.bat" refresh  - re-run CMake configure on the existing
rem                                          build folder (keeps every cached option)
rem    call "%~dp0_build_env.bat" dllpath  - put the app/OCCT/Qt DLL folders on PATH
rem
rem  Sets ENV_ERROR to a message when something is missing (empty = OK).
rem  Settings (override with an environment variable of the same name):
rem    BUILD_NAME     build folder under the repository root   (default: build)
rem    CONFIG         Release / Debug for Visual Studio builds  (default: Release)
rem    EXTRA_DLL_DIR  extra DLL folder if the app/tool cannot start
rem  NOTE: keep this file ASCII only - cmd misreads UTF-8 text in .bat files.
rem ============================================================================
set "ENV_ERROR="
if /i "%~1"=="env" goto :env
if /i "%~1"=="refresh" goto :refresh
if /i "%~1"=="dllpath" goto :dllpath
set "ENV_ERROR=_build_env.bat: unknown command %~1"
goto :eof

:env
if not defined BUILD_NAME set "BUILD_NAME=build"
if not defined CONFIG set "CONFIG=Release"
set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
set "BUILD=%REPO%\%BUILD_NAME%"
set "CACHE=%BUILD%\CMakeCache.txt"
if not exist "%CACHE%" set "ENV_ERROR=%CACHE% not found - set BUILD_NAME to your build folder name"
if defined ENV_ERROR goto :eof

set "CMAKE_EXE="
set "GENERATOR="
set "OCCT_LIB="
set "OCCT_CFG="
set "QT_CFG="
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_COMMAND:" "%CACHE%"') do set "CMAKE_EXE=%%B"
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_GENERATOR:" "%CACHE%"') do set "GENERATOR=%%B"
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"OpenCASCADE_LIBRARY_DIR:" "%CACHE%"') do set "OCCT_LIB=%%B"
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"OpenCASCADE_DIR:" "%CACHE%"') do set "OCCT_CFG=%%B"
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"Qt6_DIR:" "%CACHE%"') do set "QT_CFG=%%B"
if defined CMAKE_EXE set "CMAKE_EXE=%CMAKE_EXE:/=\%"
if defined OCCT_LIB set "OCCT_LIB=%OCCT_LIB:/=\%"
if defined OCCT_CFG set "OCCT_CFG=%OCCT_CFG:/=\%"
if defined QT_CFG set "QT_CFG=%QT_CFG:/=\%"
if not defined CMAKE_EXE set "CMAKE_EXE=cmake"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake"
echo build     : %BUILD%
echo generator : %GENERATOR%
echo cmake     : %CMAKE_EXE%

rem Visual Studio x64 environment - needed by NMake/Ninja builds, harmless otherwise.
where cl >nul 2>nul
if not errorlevel 1 goto :eof
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :eof
set "VSPATH="
rem Not inside "for /f (...)": the ")" of "(x86)" would end the for clause early.
set "VSPATH_TMP=%TEMP%\nxcc_vspath_%RANDOM%.txt"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSPATH_TMP%" 2>nul
set /p VSPATH=<"%VSPATH_TMP%"
del "%VSPATH_TMP%" >nul 2>nul
if not defined VSPATH goto :eof
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" goto :eof
echo Opening Visual Studio x64 environment...
rem vcvars64.bat itself calls vswhere.exe by name.
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
goto :eof

:refresh
rem NMake does not regenerate its makefiles when a single target is requested, so a
rem build folder configured before a target existed fails with "U1073: don't know how
rem to make ...". Passing the existing build folder keeps every cached option.
echo.
echo Refreshing build files ...
"%CMAKE_EXE%" "%BUILD%"
if errorlevel 1 set "ENV_ERROR=CMake configure failed - see the messages above"
goto :eof

:dllpath
rem The app folder already holds the DLLs it was deployed with - reuse it first.
set "APP_EXE="
set "APP_DIR="
if exist "%BUILD%\nx_crosscompare.exe" set "APP_EXE=%BUILD%\nx_crosscompare.exe"
if not defined APP_EXE if exist "%BUILD%\%CONFIG%\nx_crosscompare.exe" set "APP_EXE=%BUILD%\%CONFIG%\nx_crosscompare.exe"
if not defined APP_EXE for /f "delims=" %%E in ('dir /s /b "%BUILD%\nx_crosscompare.exe" 2^>nul') do set "APP_EXE=%%E"
if defined APP_EXE for %%E in ("%APP_EXE%") do set "APP_DIR=%%~dpE"
if defined APP_DIR set "PATH=%APP_DIR%;%PATH%"
if defined OCCT_LIB set "PATH=%OCCT_LIB%\..\bin;%OCCT_LIB%\..\..\bin;%PATH%"
if defined OCCT_CFG set "PATH=%OCCT_CFG%\..\..\bin;%OCCT_CFG%\..\..\..\bin;%PATH%"
if defined QT_CFG set "PATH=%QT_CFG%\..\..\..\bin;%PATH%"
if defined EXTRA_DLL_DIR set "PATH=%EXTRA_DLL_DIR%;%PATH%"
goto :eof
