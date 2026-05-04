@echo off
setlocal

set "ROOT=%~dp0"
if not defined BUILD_DIR set "BUILD_DIR=build"
if not defined CONFIG set "CONFIG=Release"
if not defined TARGET set "TARGET=workgraph_poc"
if not defined BUILD_PARALLEL set "BUILD_PARALLEL=12"

pushd "%ROOT%" || exit /b 1

set "EXE=%ROOT%%BUILD_DIR%\bin\%CONFIG%\workgraph_poc.exe"

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [configure] Generating Visual Studio build files...
    cmake -G "Visual Studio 17 2022" -A x64 -B "%BUILD_DIR%"
    if errorlevel 1 goto fail
)

echo [build] %TARGET% %CONFIG%
cmake --build "%BUILD_DIR%" --target "%TARGET%" --config "%CONFIG%" --parallel %BUILD_PARALLEL%
if errorlevel 1 goto fail

if not exist "%EXE%" (
    echo [error] Expected executable not found:
    echo "%EXE%"
    goto fail
)

echo [run] "%EXE%"
"%EXE%" %*
set "APP_EXIT=%ERRORLEVEL%"

popd
exit /b %APP_EXIT%

:fail
echo.
echo Build-and-run failed. Press any key to close this window.
pause >nul
popd
exit /b 1
