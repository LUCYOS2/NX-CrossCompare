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

echo.
echo Building (this can take several minutes the first time) ...
"%CMAKE_EXE%" --build "%BUILD%" --config %CONFIG%
if errorlevel 1 (
    set "ENV_ERROR=Build failed - see the messages above"
    goto :failed
)

call "%~dp0_build_env.bat" dllpath
if not defined APP_EXE (
    set "ENV_ERROR=nx_crosscompare.exe not found under the build folder"
    goto :failed
)
echo.
echo Build OK: %APP_EXE%

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

:failed
echo.
echo [ERROR] %ENV_ERROR%

:end
echo.
if not defined NO_PAUSE pause
endlocal
