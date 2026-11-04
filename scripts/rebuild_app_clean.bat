@echo off
rem ============================================================================
rem  rebuild_app_clean.bat - delete all compiled objects, then build and start the app
rem
rem  Use this instead of build_app.bat when the app crashes right after "git pull"
rem  and build (for example "HEAP CORRUPTION DETECTED" or other Debug Error popups).
rem  A build that failed half-way can leave some files compiled against old class
rem  layouts; a clean build recompiles everything. It takes longer than build_app.bat.
rem  NOTE: keep this file ASCII only - cmd misreads UTF-8 text in .bat files.
rem ============================================================================
set "CLEAN=1"
call "%~dp0build_app.bat"
